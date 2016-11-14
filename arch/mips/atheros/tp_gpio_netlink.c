#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/signal.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/init.h>
#include <linux/resource.h>
#include <linux/proc_fs.h>
#include <net/netlink.h>
#include <tp_gpio_netlink.h>


extern struct net init_net;

int tp_nl_gpio_send(struct sock *nl_sock, struct tp_nl_gpio_msg *info)
{
    int size;
    struct sk_buff *skb;
    unsigned char *old_tail;
    struct nlmsghdr *nlh;

    int retval;

	if (NULL == nl_sock)
	{
		return -1;
	}
	
    size = NLMSG_SPACE(sizeof(struct tp_nl_gpio_msg));

    skb = alloc_skb(size, GFP_KERNEL);
    if (!skb)
    {
    	printk("%s:alloc skb failed\n", __func__);
    	return -1;
    }

	nlh = nlmsg_put(skb,0,0,0,sizeof(struct tp_nl_gpio_msg),0);
	
	if (!nlh)
	{
		printk("%s:nlmsg_put error\n", __func__);
		return -1;
	}
    
    old_tail = skb->tail;
	
    memcpy(NLMSG_DATA(nlh),info,sizeof(struct tp_nl_gpio_msg));
	
    nlh->nlmsg_len = skb->tail - old_tail;
	
    NETLINK_CB(skb).pid = 0;

    NETLINK_CB(skb).dst_group = D_GRP;

	retval = netlink_broadcast(nl_sock, skb, 0, D_GRP, GFP_KERNEL);

    return 0;

}

struct sock * init_tp_gpio_netlink(void)
{
	return netlink_kernel_create(&init_net, NETLINK_TP_GPIO, 0, NULL, NULL, THIS_MODULE);
}