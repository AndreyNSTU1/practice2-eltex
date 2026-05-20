#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

MODULE_AUTHOR("Белетков Андрей");
MODULE_DESCRIPTION("Модуль ядра Hello World");
MODULE_LICENSE("BelletkovFreeLicense v1.0");

static int __init hello_init(void)
{
    printk(KERN_INFO "Hello, World! Модуль загружен.\n");
    printk(KERN_INFO "Автор: Белетков Андрей");
    return 0;
}

static void __exit hello_exit(void)
{
    printk(KERN_INFO "Goodbye, World! Модуль выгружен.\n");
}

module_init(hello_init);
module_exit(hello_exit);
