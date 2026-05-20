#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>

#define MAX_DRIVERS 100
#define BUFFER_SIZE 256
#define FIFO_CMD_TEMPLATE "/tmp/taxi_%d_cmd"
#define FIFO_RESP_TEMPLATE "/tmp/taxi_%d_resp"

/* Driver status structure (parent side) */
typedef struct {
    pid_t pid;
    int cmd_fd;          /* write to driver */
    int resp_fd;         /* read from driver */
    char status[16];     /* "Available" or "Busy" */
    time_t start_time;   /* when task started (for Busy) */
    int duration;        /* task duration in seconds (for Busy) */
} driver_info_t;

static driver_info_t drivers[MAX_DRIVERS];
static int driver_count = 0;
static int epoll_fd = -1;

/* Helper: remove FIFO files */
static void remove_fifo(pid_t pid) {
    char path[256];
    snprintf(path, sizeof(path), FIFO_CMD_TEMPLATE, pid);
    unlink(path);
    snprintf(path, sizeof(path), FIFO_RESP_TEMPLATE, pid);
    unlink(path);
}

/* Cleanup all drivers and FIFOs */
static void cleanup(void) {
    for (int i = 0; i < driver_count; i++) {
        if (drivers[i].pid > 0) {
            kill(drivers[i].pid, SIGTERM);
            waitpid(drivers[i].pid, NULL, WNOHANG);
            close(drivers[i].cmd_fd);
            close(drivers[i].resp_fd);
            remove_fifo(drivers[i].pid);
        }
    }
    if (epoll_fd >= 0) close(epoll_fd);
}

/* Signal handler to reap children */
static void sigchld_handler(int sig) {
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

/* Initialize signal handlers */
static void init_signals(void) {
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);
}

/* Remove driver from list (called when resp_fd hangs up) */
static void remove_driver(int idx) {
    if (idx < 0 || idx >= driver_count) return;
    close(drivers[idx].cmd_fd);
    close(drivers[idx].resp_fd);
    remove_fifo(drivers[idx].pid);
    /* shift remaining drivers */
    for (int i = idx; i < driver_count - 1; i++) {
        drivers[i] = drivers[i+1];
    }
    driver_count--;
}

/* Send a message to driver (write to its cmd FIFO) */
static int send_to_driver(pid_t pid, const char *msg) {
    for (int i = 0; i < driver_count; i++) {
        if (drivers[i].pid == pid) {
            ssize_t len = write(drivers[i].cmd_fd, msg, strlen(msg));
            if (len == (ssize_t)strlen(msg)) return 0;
            return -1;
        }
    }
    return -1;
}

/* Find driver index by pid */
static int find_driver_index(pid_t pid) {
    for (int i = 0; i < driver_count; i++) {
        if (drivers[i].pid == pid) return i;
    }
    return -1;
}

/* Handle incoming message from a driver (via resp_fd) */
static void handle_driver_message(int idx) {
    char buf[BUFFER_SIZE];
    ssize_t n = read(drivers[idx].resp_fd, buf, sizeof(buf)-1);
    if (n <= 0) {
        /* driver closed pipe or error */
        remove_driver(idx);
        return;
    }
    buf[n] = '\0';
    /* strip newline */
    char *nl = strchr(buf, '\n');
    if (nl) *nl = '\0';

    /* Parse message: "AVAILABLE" or "BUSY <seconds>" */
    if (strcmp(buf, "AVAILABLE") == 0) {
        strcpy(drivers[idx].status, "Available");
        drivers[idx].duration = 0;
        drivers[idx].start_time = 0;
        printf("Driver %d is now Available.\n", drivers[idx].pid);
        fflush(stdout);
    } else if (strncmp(buf, "BUSY", 4) == 0) {
        int secs = 0;
        if (sscanf(buf, "BUSY %d", &secs) == 1) {
            strcpy(drivers[idx].status, "Busy");
            drivers[idx].duration = secs;
            drivers[idx].start_time = time(NULL);
            printf("Driver %d started task for %d seconds.\n", drivers[idx].pid, secs);
            fflush(stdout);
        }
    }
}

/* Driver process main function */
static void driver_main(pid_t my_pid, const char *cmd_path, const char *resp_path) {
    int cmd_fd, resp_fd, timer_fd;
    struct epoll_event ev, events[2];
    int epfd;
    char buf[BUFFER_SIZE];

    /* Open FIFOs */
    cmd_fd = open(cmd_path, O_RDONLY | O_NONBLOCK);
    if (cmd_fd < 0) {
        perror("driver: open cmd FIFO");
        exit(1);
    }
    resp_fd = open(resp_path, O_WRONLY);
    if (resp_fd < 0) {
        perror("driver: open resp FIFO");
        close(cmd_fd);
        exit(1);
    }

    /* Create timerfd (initially disabled) */
    timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (timer_fd < 0) {
        perror("timerfd_create");
        close(cmd_fd); close(resp_fd);
        exit(1);
    }

    /* epoll for cmd_fd and timer_fd */
    epfd = epoll_create1(0);
    if (epfd < 0) {
        perror("epoll_create1");
        close(cmd_fd); close(resp_fd); close(timer_fd);
        exit(1);
    }

    ev.events = EPOLLIN;
    ev.data.fd = cmd_fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, cmd_fd, &ev);
    ev.data.fd = timer_fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, timer_fd, &ev);

    /* Inform parent that driver is ready */
    write(resp_fd, "AVAILABLE\n", 10);

    /* Driver state */
    int busy = 0;          /* 0 = available, 1 = busy */
    int remaining = 0;     /* remaining seconds when busy */

    while (1) {
        int nfds = epoll_wait(epfd, events, 2, -1);
        if (nfds < 0 && errno != EINTR) break;

        for (int i = 0; i < nfds; i++) {
            if (events[i].data.fd == cmd_fd) {
                ssize_t n = read(cmd_fd, buf, sizeof(buf)-1);
                if (n <= 0) {
                    /* parent closed pipe -> exit */
                    close(cmd_fd); close(resp_fd); close(timer_fd); close(epfd);
                    exit(0);
                }
                buf[n] = '\0';
                /* parse command: "TASK <seconds>" */
                int secs;
                if (sscanf(buf, "TASK %d", &secs) == 1) {
                    if (!busy) {
                        /* start task */
                        busy = 1;
                        remaining = secs;
                        /* arm timerfd */
                        struct itimerspec its = {0};
                        its.it_value.tv_sec = secs;
                        timerfd_settime(timer_fd, 0, &its, NULL);
                        /* notify parent that we are busy */
                        char resp[BUFFER_SIZE];
                        snprintf(resp, sizeof(resp), "BUSY %d\n", secs);
                        write(resp_fd, resp, strlen(resp));
                    } else {
                        /* driver is busy, reply with remaining time */
                        char resp[BUFFER_SIZE];
                        snprintf(resp, sizeof(resp), "BUSY %d\n", remaining);
                        write(resp_fd, resp, strlen(resp));
                    }
                }
            } else if (events[i].data.fd == timer_fd) {
                /* timer expired */
                uint64_t exp;
                read(timer_fd, &exp, sizeof(exp));
                busy = 0;
                remaining = 0;
                /* disarm timer */
                struct itimerspec its = {0};
                timerfd_settime(timer_fd, 0, &its, NULL);
                /* notify parent */
                write(resp_fd, "AVAILABLE\n", 10);
            }
        }
    }

    close(cmd_fd); close(resp_fd); close(timer_fd); close(epfd);
    exit(0);
}

/* CLI command: create_driver */
static void create_driver(void) {
    if (driver_count >= MAX_DRIVERS) {
        printf("Maximum number of drivers reached.\n");
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return;
    }

    if (pid == 0) {
        /* Child: driver process */
        char cmd_path[256], resp_path[256];
        snprintf(cmd_path, sizeof(cmd_path), FIFO_CMD_TEMPLATE, getpid());
        snprintf(resp_path, sizeof(resp_path), FIFO_RESP_TEMPLATE, getpid());
        /* FIFOs already created by parent, just open and run */
        driver_main(getpid(), cmd_path, resp_path);
        exit(0);
    } else {
        /* Parent: create FIFOs before child uses them */
        char cmd_path[256], resp_path[256];
        snprintf(cmd_path, sizeof(cmd_path), FIFO_CMD_TEMPLATE, pid);
        snprintf(resp_path, sizeof(resp_path), FIFO_RESP_TEMPLATE, pid);
        /* Remove old FIFOs if exist */
        unlink(cmd_path);
        unlink(resp_path);
        if (mkfifo(cmd_path, 0666) < 0 || mkfifo(resp_path, 0666) < 0) {
            perror("mkfifo");
            kill(pid, SIGTERM);
            return;
        }
        /* Open FIFOs */
        int cmd_fd = open(cmd_path, O_WRONLY);
        int resp_fd = open(resp_path, O_RDONLY | O_NONBLOCK);
        if (cmd_fd < 0 || resp_fd < 0) {
            perror("open FIFOs");
            if (cmd_fd >= 0) close(cmd_fd);
            if (resp_fd >= 0) close(resp_fd);
            kill(pid, SIGTERM);
            remove_fifo(pid);
            return;
        }
        /* Add to drivers list */
        drivers[driver_count].pid = pid;
        drivers[driver_count].cmd_fd = cmd_fd;
        drivers[driver_count].resp_fd = resp_fd;
        strcpy(drivers[driver_count].status, "Unknown");
        drivers[driver_count].start_time = 0;
        drivers[driver_count].duration = 0;
        driver_count++;

        /* Add resp_fd to epoll */
        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLRDHUP;
        ev.data.ptr = &drivers[driver_count-1]; /* store pointer for convenience */
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, resp_fd, &ev) < 0) {
            perror("epoll_ctl add driver resp");
            remove_driver(driver_count-1);
            return;
        }
        printf("Driver created with PID %d\n", pid);
    }
}

/* CLI command: send_task <pid> <timer> */
static void send_task(pid_t pid, int timer_sec) {
    int idx = find_driver_index(pid);
    if (idx < 0) {
        printf("Driver %d not found.\n", pid);
        return;
    }
    if (strcmp(drivers[idx].status, "Busy") == 0) {
        /* calculate remaining time */
        time_t now = time(NULL);
        int elapsed = (int)(now - drivers[idx].start_time);
        int remaining = drivers[idx].duration - elapsed;
        if (remaining < 0) remaining = 0;
        printf("Busy %d\n", remaining);
        return;
    }
    /* Driver is available: send task */
    char msg[BUFFER_SIZE];
    snprintf(msg, sizeof(msg), "TASK %d\n", timer_sec);
    if (send_to_driver(pid, msg) < 0) {
        printf("Failed to send task to driver %d.\n", pid);
        return;
    }
    /* Update parent's state immediately (optimistic) */
    strcpy(drivers[idx].status, "Busy");
    drivers[idx].start_time = time(NULL);
    drivers[idx].duration = timer_sec;
    printf("Task of %d seconds sent to driver %d.\n", timer_sec, pid);
}

/* CLI command: get_status <pid> */
static void get_status(pid_t pid) {
    int idx = find_driver_index(pid);
    if (idx < 0) {
        printf("Driver %d not found.\n", pid);
        return;
    }
    if (strcmp(drivers[idx].status, "Busy") == 0) {
        time_t now = time(NULL);
        int elapsed = (int)(now - drivers[idx].start_time);
        int remaining = drivers[idx].duration - elapsed;
        if (remaining < 0) remaining = 0;
        printf("Busy %d\n", remaining);
    } else {
        printf("Available\n");
    }
}

/* CLI command: get_drivers */
static void get_drivers(void) {
    for (int i = 0; i < driver_count; i++) {
        printf("PID %d: ", drivers[i].pid);
        if (strcmp(drivers[i].status, "Busy") == 0) {
            time_t now = time(NULL);
            int elapsed = (int)(now - drivers[i].start_time);
            int remaining = drivers[i].duration - elapsed;
            if (remaining < 0) remaining = 0;
            printf("Busy %d\n", remaining);
        } else {
            printf("Available\n");
        }
    }
}

/* Main CLI loop with epoll */
int main(void) {
    init_signals();
    atexit(cleanup);

    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        perror("epoll_create1");
        return 1;
    }

    /* Add stdin to epoll */
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = STDIN_FILENO;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, STDIN_FILENO, &ev) < 0) {
        perror("epoll_ctl stdin");
        close(epoll_fd);
        return 1;
    }

    printf("Taxi dispatcher CLI started.\n");
    printf("Commands: create_driver\n");
    printf("          send_task <pid> <task_timer>\n");
    printf("          get_status <pid>\n");
    printf("          get_drivers\n");
    printf("          exit\n");

    while (1) {
        struct epoll_event events[MAX_DRIVERS + 1];
        int nfds = epoll_wait(epoll_fd, events, MAX_DRIVERS + 1, -1);
        if (nfds < 0) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < nfds; i++) {
            if (events[i].data.fd == STDIN_FILENO) {
                /* Read command from user */
                char line[BUFFER_SIZE];
                if (!fgets(line, sizeof(line), stdin)) {
                    /* EOF */
                    goto exit;
                }
                /* Remove newline */
                line[strcspn(line, "\n")] = '\0';
                /* Parse command */
                char cmd[32];
                if (sscanf(line, "%31s", cmd) != 1) continue;

                if (strcmp(cmd, "create_driver") == 0) {
                    create_driver();
                } else if (strcmp(cmd, "send_task") == 0) {
                    pid_t pid;
                    int timer;
                    if (sscanf(line, "%*s %d %d", &pid, &timer) == 2) {
                        send_task(pid, timer);
                    } else {
                        printf("Usage: send_task <pid> <task_timer>\n");
                    }
                } else if (strcmp(cmd, "get_status") == 0) {
                    pid_t pid;
                    if (sscanf(line, "%*s %d", &pid) == 1) {
                        get_status(pid);
                    } else {
                        printf("Usage: get_status <pid>\n");
                    }
                } else if (strcmp(cmd, "get_drivers") == 0) {
                    get_drivers();
                } else if (strcmp(cmd, "exit") == 0) {
                    goto exit;
                } else {
                    printf("Unknown command.\n");
                }
                fflush(stdout);
            } else {
                /* Event from a driver's response FIFO */
                /* We stored pointer to driver_info_t in ev.data.ptr during add */
                driver_info_t *drv = (driver_info_t*)events[i].data.ptr;
                if (events[i].events & (EPOLLIN | EPOLLRDHUP)) {
                    int idx = drv - drivers; /* compute index */
                    if (idx >= 0 && idx < driver_count && drivers[idx].pid == drv->pid) {
                        handle_driver_message(idx);
                    } else {
                        /* stale pointer, remove from epoll and ignore */
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, events[i].data.fd, NULL);
                        close(events[i].data.fd);
                    }
                }
            }
        }
    }
exit:
    printf("Shutting down...\n");
    cleanup();
    return 0;
}
