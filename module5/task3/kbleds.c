/*
 * kbleds_sysfs.c - Blink keyboard LEDs using ioctl(KDSETLED)
 * Control LED mask through sysfs.
 *
 * Sysfs path:
 *   /sys/kernel/kbleds/led_mask
 *
 * Values:
 *   0 - restore normal keyboard LED state
 *   1 - first LED  001
 *   2 - second LED 010
 *   4 - third LED  100
 *   7 - all LEDs   111
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/tty.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <linux/console_struct.h>
#include <linux/vt_kern.h>
#include <linux/mutex.h>

#define MODULE_NAME        "kbleds"
#define SYSFS_DIR_NAME     "kbleds"
#define SYSFS_FILE_NAME    "led_mask"

#define BLINK_DELAY        (HZ / 5)
#define LED_MASK_MIN       0
#define LED_MASK_MAX       7
#define RESTORE_LEDS       0xFF

MODULE_DESCRIPTION("Keyboard LED blinking module controlled through sysfs");
MODULE_AUTHOR("Белетков Андрей");
MODULE_LICENSE("GPL");

static struct timer_list kbleds_timer;
static struct tty_driver *kbleds_driver;
static struct tty_struct *kbleds_tty;
static struct kobject *kbleds_kobject;

static DEFINE_MUTEX(kbleds_lock);

static int led_mask = LED_MASK_MAX;
static int current_led_state = RESTORE_LEDS;

static void kbleds_set_leds(int value)
{
 if (!kbleds_driver || !kbleds_driver->ops || !kbleds_driver->ops->ioctl)
  return;

 if (!kbleds_tty)
  return;

 kbleds_driver->ops->ioctl(kbleds_tty, KDSETLED, value);
}

static void kbleds_timer_func(struct timer_list *timer)
{
 int mask;

 mutex_lock(&kbleds_lock);
 mask = led_mask;

 if (mask == LED_MASK_MIN) {
  current_led_state = RESTORE_LEDS;
  kbleds_set_leds(RESTORE_LEDS);
 } else {
  if (current_led_state == mask)
   current_led_state = RESTORE_LEDS;
  else
   current_led_state = mask;

  kbleds_set_leds(current_led_state);
 }

 mutex_unlock(&kbleds_lock);

 mod_timer(&kbleds_timer, jiffies + BLINK_DELAY);
}

static ssize_t led_mask_show(struct kobject *kobj,
        struct kobj_attribute *attr,
        char *buf)
{
 int mask;

 mutex_lock(&kbleds_lock);
 mask = led_mask;
 mutex_unlock(&kbleds_lock);

 return sysfs_emit(buf, "%d\n", mask);
}

static ssize_t led_mask_store(struct kobject *kobj,
         struct kobj_attribute *attr,
         const char *buf,
         size_t count)
{
 int value;
 int ret;

 ret = kstrtoint(buf, 10, &value);
 if (ret)
  return ret;

 if (value < LED_MASK_MIN || value > LED_MASK_MAX)
  return -EINVAL;

 mutex_lock(&kbleds_lock);

 led_mask = value;

 if (led_mask == LED_MASK_MIN) {
  current_led_state = RESTORE_LEDS;
  kbleds_set_leds(RESTORE_LEDS);
 } else {
  current_led_state = led_mask;
  kbleds_set_leds(current_led_state);
 }

 mutex_unlock(&kbleds_lock);

 return count;
}

static struct kobj_attribute led_mask_attribute =
 __ATTR(led_mask, 0664, led_mask_show, led_mask_store);

static int __init kbleds_init(void)
{
 int error;

 pr_info("%s: loading module\n", MODULE_NAME);
 pr_info("%s: foreground console is %d\n", MODULE_NAME, fg_console);

 if (!vc_cons[fg_console].d) {
  pr_err("%s: foreground console is not available\n", MODULE_NAME);
  return -ENODEV;
 }

 kbleds_tty = vc_cons[fg_console].d->port.tty;
 if (!kbleds_tty) {
  pr_err("%s: tty is not available\n", MODULE_NAME);
  return -ENODEV;
 }

 kbleds_driver = kbleds_tty->driver;
 if (!kbleds_driver) {
  pr_err("%s: tty driver is not available\n", MODULE_NAME);
  return -ENODEV;
 }

 pr_info("%s: tty driver name: %s\n",
  MODULE_NAME,
  kbleds_driver->driver_name);

 kbleds_kobject = kobject_create_and_add(SYSFS_DIR_NAME, kernel_kobj);
 if (!kbleds_kobject)
  return -ENOMEM;

 error = sysfs_create_file(kbleds_kobject, &led_mask_attribute.attr);
 if (error) {
  pr_err("%s: failed to create sysfs file\n", MODULE_NAME);
  kobject_put(kbleds_kobject);
  return error;
 }

 timer_setup(&kbleds_timer, kbleds_timer_func, 0);
 mod_timer(&kbleds_timer,
jiffies + BLINK_DELAY);

 pr_info("%s: module loaded successfully\n", MODULE_NAME);
 pr_info("%s: use /sys/kernel/%s/%s to control LEDs\n",
  MODULE_NAME,
  SYSFS_DIR_NAME,
  SYSFS_FILE_NAME);

 return 0;
}

static void __exit kbleds_exit(void)
{
 pr_info("%s: unloading module\n", MODULE_NAME);

 del_timer_sync(&kbleds_timer);

 mutex_lock(&kbleds_lock);
 kbleds_set_leds(RESTORE_LEDS);
 mutex_unlock(&kbleds_lock);

 sysfs_remove_file(kbleds_kobject, &led_mask_attribute.attr);
 kobject_put(kbleds_kobject);

 pr_info("%s: module unloaded\n", MODULE_NAME);
}

module_init(kbleds_init);
module_exit(kbleds_exit);
