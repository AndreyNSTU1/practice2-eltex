#include <linux/module.h>
#include <net/sock.h>
#include <linux/netlink.h>
#include <linux/skbuff.h>
#include <net/net_namespace.h>
#include <linux/string.h>

#define NETLINK_USER 31

static struct sock *nl_sk = NULL;

static void hello_nl_recv_msg(struct sk_buff *skb)
{
    struct nlmsghdr *nlh;
    int pid;
    struct sk_buff *skb_out;
    int msg_size;
    char *msg = "Hello from kernel";
    int res;

    printk(KERN_INFO "Netlink: message received\n");

    if (skb->len < sizeof(struct nlmsghdr)) {
        printk(KERN_WARNING "Netlink: truncated message\n");
        return;
    }

    nlh = (struct nlmsghdr *)skb->data;
    pid = nlh->nlmsg_pid;

    if (nlh->nlmsg_len < sizeof(struct nlmsghdr)) {
        printk(KERN_WARNING "Netlink: invalid message length\n");
        return;
    }

    printk(KERN_INFO "Netlink received payload: %s\n", (char *)nlmsg_data(nlh));

    msg_size = strlen(msg) + 1;
    skb_out = nlmsg_new(msg_size, GFP_KERNEL);
    if (!skb_out) {
        printk(KERN_ERR "Netlink: failed to allocate skb\n");
        return;
    }

    nlh = nlmsg_put(skb_out, 0, 0, NLMSG_DONE, msg_size, 0);
    if (!nlh) {
        printk(KERN_ERR "Netlink: nlmsg_put failed\n");
        nlmsg_free(skb_out);
        return;
    }

    strncpy((char *)nlmsg_data(nlh), msg, msg_size);

    res = nlmsg_unicast(nl_sk, skb_out, pid);
    if (res < 0) {
        printk(KERN_ERR "Netlink: error sending reply (%d)\n", res);
    }
}

static struct netlink_kernel_cfg cfg = {
    .groups = 0,
    .input  = hello_nl_recv_msg,
};

static int __init hello_init(void)
{
    printk(KERN_INFO "Initializing netlink module\n");
    nl_sk = netlink_kernel_create(&init_net, NETLINK_USER, &cfg);
    if (!nl_sk) {
        printk(KERN_ALERT "Error creating netlink socket\n");
        return -ENOMEM;
    }
    printk(KERN_INFO "Netlink module loaded successfully\n");
    return 0;
}

static void __exit hello_exit(void)
{
    printk(KERN_INFO "Exiting netlink module\n");
    netlink_kernel_release(nl_sk);
}

module_init(hello_init);
module_exit(hello_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Белетков Андрей");
MODULE_DESCRIPTION("Netlink example module");
