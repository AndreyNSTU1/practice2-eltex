#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/version.h>

#define PROC_FILENAME "my_proc_module"
#define PROC_PERMISSIONS 0666
#define PROC_PARENT_DIR NULL
#define BUFFER_SIZE 256

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Белетков Андрей");
MODULE_DESCRIPTION("Kernel module for exchanging data with userspace via procfs");
MODULE_VERSION("1.0");

static struct proc_dir_entry *proc_entry;

static char module_buffer[BUFFER_SIZE] = "Hello from kernel module!\n";
static size_t module_buffer_size = sizeof("Hello from kernel module!\n") - 1;

static ssize_t proc_read(struct file *file, char __user *user_buffer,
                         size_t count, loff_t *offset)
{
    if (*offset > 0)
        return 0;

    if (count > module_buffer_size)
        count = module_buffer_size;

    if (copy_to_user(user_buffer, module_buffer, count))
        return -EFAULT;

    *offset = count;

    pr_info("proc_module: data was read from /proc/%s\n", PROC_FILENAME);

    return count;
}

static ssize_t proc_write(struct file *file, const char __user *user_buffer,
                          size_t count, loff_t *offset)
{
    size_t bytes_to_copy;

    bytes_to_copy = min(count, (size_t)(BUFFER_SIZE - 1));

    if (copy_from_user(module_buffer, user_buffer, bytes_to_copy))
        return -EFAULT;

    module_buffer[bytes_to_copy] = '\0';
    module_buffer_size = bytes_to_copy;

    pr_info("proc_module: data was written to /proc/%s: %s\n",
            PROC_FILENAME, module_buffer);

    return count;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0)

static const struct proc_ops proc_file_ops = {
    .proc_read = proc_read,
    .proc_write = proc_write,
};

#else

static const struct file_operations proc_file_ops = {
    .read = proc_read,
    .write = proc_write,
};

#endif

static int __init proc_module_init(void)
{
    proc_entry = proc_create(PROC_FILENAME,
                             PROC_PERMISSIONS,
                             PROC_PARENT_DIR,
                             &proc_file_ops);

    if (proc_entry == NULL) {
        pr_err("proc_module: failed to create /proc/%s\n", PROC_FILENAME);
        return -ENOMEM;
    }

    pr_info("proc_module: loaded successfully, created /proc/%s\n",
            PROC_FILENAME);

    return 0;
}

static void __exit proc_module_exit(void)
{
    proc_remove(proc_entry);

    pr_info("proc_module: unloaded, removed /proc/%s\n", PROC_FILENAME);
}

module_init(proc_module_init);
module_exit(proc_module_exit);
