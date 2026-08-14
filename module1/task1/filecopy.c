#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define BLOCK_SIZE 4096
#define COPY_SUFFIX ".copy"

#define MSG_FILE 1u
#define MSG_END  2u
#define MSG_READY 0x52454144u /* 'READ' */

typedef struct {
    uint32_t type;
    uint64_t file_size;
    char file_name[PATH_MAX];
} Message;

static int write_all(int fd, const void *buffer, size_t count)
{
    const unsigned char *p = buffer;

    while (count > 0) {
        ssize_t n = write(fd, p, count);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        p += (size_t)n;
        count -= (size_t)n;
    }
    return 0;
}

static int read_exact(int fd, void *buffer, size_t count)
{
    unsigned char *p = buffer;

    while (count > 0) {
        ssize_t n = read(fd, p, count);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (n == 0) {
            errno = EPIPE;
            return -1;
        }
        p += (size_t)n;
        count -= (size_t)n;
    }
    return 0;
}

static void print_errno(const char *prefix, const char *name)
{
    if (name != NULL)
        dprintf(STDERR_FILENO, "%s '%s': %s\n", prefix, name, strerror(errno));
    else
        dprintf(STDERR_FILENO, "%s: %s\n", prefix, strerror(errno));
}

static int wait_ready(int ready_fd)
{
    uint32_t code;

    if (read_exact(ready_fd, &code, sizeof(code)) < 0)
        return -1;

    if (code != MSG_READY) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

static int child_loop(int data_fd, int ready_fd)
{
    unsigned char buffer[BLOCK_SIZE];
    int had_error = 0;

    for (;;) {
        uint32_t ready = MSG_READY;
        Message msg;

        if (write_all(ready_fd, &ready, sizeof(ready)) < 0) {
            print_errno("child: cannot send READY", NULL);
            return 1;
        }

        if (read_exact(data_fd, &msg, sizeof(msg)) < 0) {
            print_errno("child: cannot read message", NULL);
            return 1;
        }

        if (msg.type == MSG_END)
            return had_error ? 1 : 0;

        if (msg.type != MSG_FILE) {
            dprintf(STDERR_FILENO, "child: unknown message type: %u\n", msg.type);
            return 1;
        }

        msg.file_name[PATH_MAX - 1] = '\0';

        char copy_name[PATH_MAX + sizeof(COPY_SUFFIX)];
        int n = snprintf(copy_name, sizeof(copy_name), "%s%s",
                         msg.file_name, COPY_SUFFIX);
        int out_fd = -1;
        int file_error = 0;

        if (n < 0 || (size_t)n >= sizeof(copy_name)) {
            dprintf(STDERR_FILENO,
                    "child: destination path is too long for '%s'\n",
                    msg.file_name);
            file_error = 1;
            had_error = 1;
        } else {
            out_fd = open(copy_name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (out_fd < 0) {
                print_errno("child: cannot create", copy_name);
                file_error = 1;
                had_error = 1;
            }
        }

        uint64_t remaining = msg.file_size;
        while (remaining > 0) {
            size_t chunk = remaining > BLOCK_SIZE
                               ? BLOCK_SIZE
                               : (size_t)remaining;

            if (read_exact(data_fd, buffer, chunk) < 0) {
                print_errno("child: error while receiving file data", msg.file_name);
                if (out_fd >= 0)
                    close(out_fd);
                return 1;
            }

            if (out_fd >= 0 && write_all(out_fd, buffer, chunk) < 0) {
                print_errno("child: error while writing", copy_name);
                close(out_fd);
                out_fd = -1;
                file_error = 1;
                had_error = 1;
                
            }

            remaining -= (uint64_t)chunk;
        }

        if (out_fd >= 0 && close(out_fd) < 0) {
            print_errno("child: error while closing", copy_name);
            file_error = 1;
            had_error = 1;
        }

        if (!file_error)
            dprintf(STDOUT_FILENO, "created: %s\n", copy_name);
    }
}


static int parent_send_file(int data_fd, int ready_fd, const char *name)
{
    int fd = open(name, O_RDONLY);
    if (fd < 0) {
        print_errno("parent: cannot open", name);
        return 1;
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        print_errno("parent: fstat failed for", name);
        close(fd);
        return 1;
    }

    if (!S_ISREG(st.st_mode)) {
        dprintf(STDERR_FILENO, "parent: '%s' is not a regular file\n", name);
        close(fd);
        return 1;
    }

    if (st.st_size < 0) {
        dprintf(STDERR_FILENO, "parent: invalid file size for '%s'\n", name);
        close(fd);
        return 1;
    }

    if (strlen(name) >= PATH_MAX) {
        dprintf(STDERR_FILENO, "parent: file name is too long: '%s'\n", name);
        close(fd);
        return 1;
    }

    if (wait_ready(ready_fd) < 0) {
        print_errno("parent: cannot receive READY", NULL);
        close(fd);
        return -1;
    }

    Message msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_FILE;
    msg.file_size = (uint64_t)st.st_size;
    memcpy(msg.file_name, name, strlen(name) + 1);

    if (write_all(data_fd, &msg, sizeof(msg)) < 0) {
        print_errno("parent: cannot send file header", name);
        close(fd);
        return -1;
    }

    unsigned char buffer[BLOCK_SIZE];
    uint64_t remaining = msg.file_size;

    while (remaining > 0) {
        size_t want = remaining > BLOCK_SIZE ? BLOCK_SIZE : (size_t)remaining;
        ssize_t n;

        do {
            n = read(fd, buffer, want);
        } while (n < 0 && errno == EINTR);

        if (n < 0) {
            print_errno("parent: read failed for", name);
            close(fd);
            return -1;
        }
        if (n == 0) {
            dprintf(STDERR_FILENO,
                    "parent: unexpected EOF while reading '%s'\n", name);
            close(fd);
            return -1;
        }

        if (write_all(data_fd, buffer, (size_t)n) < 0) {
            print_errno("parent: cannot send file data", name);
            close(fd);
            return -1;
        }

        remaining -= (uint64_t)n;
    }

    if (close(fd) < 0)
        print_errno("parent: close failed for", name);

    return 0;
}

static int make_fifo(const char *path)
{
    if (mkfifo(path, 0600) < 0) {
        print_errno("cannot create FIFO", path);
        return -1;
    }
    return 0;
}

static void usage(const char *prog)
{
    dprintf(STDERR_FILENO,
            "Usage: %s [-p fifo_name] file1 [file2 ...]\n",
            prog);
}

int main(int argc, char *argv[])
{
    char *fifo_name = NULL;
    char **files = calloc((size_t)argc, sizeof(*files));
    int file_count = 0;

    if (files == NULL) {
        print_errno("calloc failed", NULL);
        return EXIT_FAILURE;
    }

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-p") == 0) {
            if (fifo_name != NULL || i + 1 >= argc) {
                usage(argv[0]);
                free(files);
                return EXIT_FAILURE;
            }
            fifo_name = argv[++i];
            if (fifo_name[0] == '\0') {
                usage(argv[0]);
                free(files);
                return EXIT_FAILURE;
            }
        } else {
            files[file_count++] = argv[i];
        }
    }

    if (file_count == 0) {
        usage(argv[0]);
        free(files);
        return EXIT_FAILURE;
    }

    signal(SIGPIPE, SIG_IGN);

    int p2c[2] = {-1, -1};
    int c2p[2] = {-1, -1};
    char ack_fifo[PATH_MAX];
    int named = fifo_name != NULL;

    if (!named) {
        if (pipe(p2c) < 0 || pipe(c2p) < 0) {
            print_errno("pipe failed", NULL);
            if (p2c[0] >= 0) close(p2c[0]);
            if (p2c[1] >= 0) close(p2c[1]);
            if (c2p[0] >= 0) close(c2p[0]);
            if (c2p[1] >= 0) close(c2p[1]);
            free(files);
            return EXIT_FAILURE;
        }
    } else {
        if (strlen(fifo_name) >= PATH_MAX) {
            dprintf(STDERR_FILENO, "FIFO name is too long\n");
            free(files);
            return EXIT_FAILURE;
        }

        int n = snprintf(ack_fifo, sizeof(ack_fifo), "%s.ack", fifo_name);
        if (n < 0 || (size_t)n >= sizeof(ack_fifo)) {
            dprintf(STDERR_FILENO, "FIFO acknowledgement name is too long\n");
            free(files);
            return EXIT_FAILURE;
        }

        if (make_fifo(fifo_name) < 0) {
            free(files);
            return EXIT_FAILURE;
        }
        if (make_fifo(ack_fifo) < 0) {
            unlink(fifo_name);
            free(files);
            return EXIT_FAILURE;
        }
    }

    pid_t pid = fork();
    if (pid < 0) {
        print_errno("fork failed", NULL);
        if (!named) {
            close(p2c[0]); close(p2c[1]);
            close(c2p[0]); close(c2p[1]);
        } else {
            unlink(fifo_name);
            unlink(ack_fifo);
        }
        free(files);
        return EXIT_FAILURE;
    }

    if (pid == 0) {
        int data_fd;
        int ready_fd;

        if (!named) {
            close(p2c[1]);
            close(c2p[0]);
            data_fd = p2c[0];
            ready_fd = c2p[1];
        } else {
            data_fd = open(fifo_name, O_RDONLY);
            if (data_fd < 0) {
                print_errno("child: cannot open FIFO for reading", fifo_name);
                _exit(EXIT_FAILURE);
            }
            ready_fd = open(ack_fifo, O_WRONLY);
            if (ready_fd < 0) {
                print_errno("child: cannot open acknowledgement FIFO", ack_fifo);
                close(data_fd);
                _exit(EXIT_FAILURE);
            }
        }

        int rc = child_loop(data_fd, ready_fd);
        close(data_fd);
        close(ready_fd);
        free(files);
        _exit(rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
    }

    int data_fd;
    int ready_fd;

    if (!named) {
        close(p2c[0]);
        close(c2p[1]);
        data_fd = p2c[1];
        ready_fd = c2p[0];
    } else {
        data_fd = open(fifo_name, O_WRONLY);
        if (data_fd < 0) {
            print_errno("parent: cannot open FIFO for writing", fifo_name);
            kill(pid, SIGTERM);
            waitpid(pid, NULL, 0);
            unlink(fifo_name);
            unlink(ack_fifo);
            free(files);
            return EXIT_FAILURE;
        }
        ready_fd = open(ack_fifo, O_RDONLY);
        if (ready_fd < 0) {
            print_errno("parent: cannot open acknowledgement FIFO", ack_fifo);
            close(data_fd);
            kill(pid, SIGTERM);
            waitpid(pid, NULL, 0);
            unlink(fifo_name);
            unlink(ack_fifo);
            free(files);
            return EXIT_FAILURE;
        }
    }

    int fatal_error = 0;
    int had_file_error = 0;
    for (int i = 0; i < file_count; ++i) {
        int rc = parent_send_file(data_fd, ready_fd, files[i]);
        if (rc > 0)
            had_file_error = 1;
        if (rc < 0) {
            fatal_error = 1;
            break;
        }
    }

    if (!fatal_error) {
        if (wait_ready(ready_fd) < 0) {
            print_errno("parent: cannot receive final READY", NULL);
            fatal_error = 1;
        } else {
            Message end_msg;
            memset(&end_msg, 0, sizeof(end_msg));
            end_msg.type = MSG_END;
            if (write_all(data_fd, &end_msg, sizeof(end_msg)) < 0) {
                print_errno("parent: cannot send termination message", NULL);
                fatal_error = 1;
            }
        }
    }

    close(data_fd);
    close(ready_fd);

    int status;
    if (waitpid(pid, &status, 0) < 0) {
        print_errno("waitpid failed", NULL);
        fatal_error = 1;
    } else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        dprintf(STDERR_FILENO, "parent: child process terminated with an error\n");
        fatal_error = 1;
    }

    if (named) {
        if (unlink(fifo_name) < 0 && errno != ENOENT)
            print_errno("cannot remove FIFO", fifo_name);
        if (unlink(ack_fifo) < 0 && errno != ENOENT)
            print_errno("cannot remove FIFO", ack_fifo);
    }

    free(files);
    return (fatal_error || had_file_error) ? EXIT_FAILURE : EXIT_SUCCESS;
}
