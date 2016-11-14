/*!Copyright(c) 2008-2010 Shenzhen TP-LINK Technologies Co.Ltd.
 *
 *\file	tp_mroute.c
 *\brief	IGMP & Mcast UDP diliver module. 
 *
 *\author	Wang Wenhao
 *\version	1.0.1
 *\date	05Jan10
 *
 *\history 

 * 		\arg 1.0.3, 03Dec10, Hou Xubo，增加对2.6.31内核的支持
 *                                             
 *
 * 		\arg 1.0.2, 06May10, Yin Zhongtao，增加对WLAN的组播转发支持，修复WAN口QUERY问题，
 *                                             优化了组播数据转发效率
 *
 *		\arg 1.0.1, 05Jan10, Wang Wenhao, 将IGMP包和UDP转发分开发送，解决UDP可能找不到路由的问题.
 *											通过查找静态路由获取dst，解决可能出现的kernel panic. 
 * 				    
 *		\arg 1.0.0, ???, Wang Wenhao, Create the file.
 */

#include <linux/autoconf.h>
#include <linux/types.h>
#include <linux/tp_mroute.h>
#include <linux/igmp.h>
#include <linux/netdevice.h>
#include <net/ip.h>

/* add support for kernel 2.6.31. by HouXB, 03Dec10 */
#include <linux/version.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31))
#include <net/route.h>
#include <linux/seq_file.h>
#endif


extern struct net init_net;

#if defined(CONFIG_ATHRS17_HNAT_VLAN) || defined(CONFIG_ATHRS27_AR8236)
/* use VLAN DEV to identify LAN/WAN */
static unsigned char wan_eth_name[IFNAMSIZ] = "eth0.2";
static unsigned char lan_eth_name[IFNAMSIZ] = "eth0.1";
#else
static unsigned char wan_eth_name[IFNAMSIZ] = "eth1";
static unsigned char lan_eth_name[IFNAMSIZ] = "eth0";
#endif

#undef TEST_DEBUG
#ifdef TEST_DEBUG
#define debugk(x) printk(x)
#else
#define debugk(X)
#endif

#ifdef TEST_DEBUG
static void dumpHexData(unsigned char *head, int len)
{
    int i;
    unsigned char *curPtr = head;

    for (i = 0; i < len; ++i) {
		if (!i)
			;
		else if ((i & 0xF) == 0)
            printk("\n"); 
		else if ((i & 0x7) == 0)
            printk("- ");
        printk("%02X ", *curPtr++);
    }
    printk("\n\n");
}
#endif

#define IGMP_SIZE (sizeof(struct igmphdr)+sizeof(struct iphdr)+4)

static int initialed = 0;

/*  by huangwenzhong, 13Sep12 */
static unsigned char lan_br_name[IFNAMSIZ] = "br0";/* default value is br0 */

static struct net_device *lan_br_dev = NULL;
static struct net_device *lan_eth_dev = NULL;
static struct net_device *wan_eth_dev = NULL;

static int lan_br_index = -1;
static int lan_eth_index = -1;
static int wan_eth_index = -1;

#define CONFIG_TP_MULTICAST_SWITCH	(1)
static int tp_mroute_enable = 1;

static struct mc_table table;


static void disable_tp_mroute(void);


/*  by huangwenzhong, 13Sep12 */
/* 
add proc for lan_br_name, LAN_ETH_NAME and WAN_ETH_NAME, 
so that they can be configurable.
*/

#ifdef CONFIG_PROC_FS
struct proc_dir_entry *tp_mroute_dir_entry = NULL;
struct proc_dir_entry *tp_mroute_enable_entry = NULL;
struct proc_dir_entry *tp_mroute_lan_br_entry = NULL;
struct proc_dir_entry *tp_mroute_lan_eth_entry = NULL;
struct proc_dir_entry *tp_mroute_wan_eth_entry = NULL;

static int tp_mroute_enable_read (char *page, char **start, off_t off,
                               int count, int *eof, void *data)
{
    return sprintf (page, "%d\n", tp_mroute_enable);
}

static int tp_mroute_enable_write (struct file *file, const char *buf,
                                        unsigned long count, void *data)
{
    int val;

	if (sscanf(buf, "%d", &val) != 1)
	{
        return -EINVAL;
	}
	
	if ((val != 0) && (val != 1))
	{
		return -EINVAL;
	}
	
	tp_mroute_enable = val;

	if (!tp_mroute_enable)
	{
		disable_tp_mroute();
	}
	else
	{	
		if (!initialed)
		{
			mc_table_init();
		}
		
		/* table.generl_query_timer.expires = jiffies + QUERY_INTERVAL; */
		if (timer_pending(&(table.generl_query_timer)))
		{
			mod_timer(&(table.generl_query_timer), jiffies + 2);
		}
		else
		{
			table.generl_query_timer.expires = jiffies + 2;
    		add_timer(&table.generl_query_timer);
		}
	}
	
	return count;
}

static int tp_mroute_lan_br_read (char *page, char **start, off_t off,
                               int count, int *eof, void *data)
{
    return sprintf (page, "%s\n", lan_br_name);
}

static int tp_mroute_lan_br_write (struct file *file, const char *buf,
                                        unsigned long count, void *data)
{
    unsigned char if_name[IFNAMSIZ] = {0};

	if (sscanf(buf, "%s", if_name) != 1)
	{
        return -EINVAL;
	}

	if (if_name[0] == '\0')
	{
		return -EINVAL;
	}

	memcpy(lan_br_name, if_name, IFNAMSIZ);
	
	return count;
}

static int tp_mroute_lan_eth_read (char *page, char **start, off_t off,
                               int count, int *eof, void *data)
{
    return sprintf (page, "%s\n", lan_eth_name);
}

static int tp_mroute_lan_eth_write (struct file *file, const char *buf,
                                        unsigned long count, void *data)
{
    unsigned char if_name[IFNAMSIZ] = {0};

	if (sscanf(buf, "%s", if_name) != 1)
	{
        return -EINVAL;
	}

	if (if_name[0] == '\0')
	{
		return -EINVAL;
	}

	memcpy(lan_eth_name, if_name, IFNAMSIZ);
	
	return count;
}


static int tp_mroute_wan_eth_read (char *page, char **start, off_t off,
                               int count, int *eof, void *data)
{
    return sprintf (page, "%s\n", wan_eth_name);
}

static int tp_mroute_wan_eth_write (struct file *file, const char *buf,
                                        unsigned long count, void *data)
{
    unsigned char if_name[IFNAMSIZ] = {0};

	if (sscanf(buf, "%s", if_name) != 1)
	{
        return -EINVAL;
	}

	if (if_name[0] == '\0')
	{
		return -EINVAL;
	}

	memcpy(wan_eth_name, if_name, IFNAMSIZ);
	
	return count;
}


static int tp_mroute_proc_init(void)
{
	tp_mroute_dir_entry = proc_mkdir(TP_MROUTE_DIR_NAME, init_net.proc_net);
	if (!tp_mroute_dir_entry)
	{
		printk("(%s,%d)cannot %s proc entry", __FUNCTION__, __LINE__, TP_MROUTE_DIR_NAME);
		return -ENOENT;
	}
	
	tp_mroute_enable_entry = create_proc_entry(TP_MROUTE_ENABLE_FILE_NAME, 0666, tp_mroute_dir_entry);
	if (!tp_mroute_enable_entry)
	{
		printk("(%s,%d)cannot create %s proc entry", __FUNCTION__, __LINE__, TP_MROUTE_ENABLE_FILE_NAME);
		return -ENOENT;
	}
	tp_mroute_enable_entry->write_proc = tp_mroute_enable_write;
	tp_mroute_enable_entry->read_proc	= tp_mroute_enable_read;

	tp_mroute_lan_br_entry = create_proc_entry(TP_MROUTE_LAN_BR_FILE_NAME, 0666, tp_mroute_dir_entry);
	if (!tp_mroute_lan_br_entry)
	{
		printk("(%s,%d)cannot create %s proc entry", __FUNCTION__, __LINE__, TP_MROUTE_LAN_BR_FILE_NAME);
		return -ENOENT;
	}
	tp_mroute_lan_br_entry->write_proc = tp_mroute_lan_br_write;
	tp_mroute_lan_br_entry->read_proc	= tp_mroute_lan_br_read;

	tp_mroute_lan_eth_entry = create_proc_entry(TP_MROUTE_LAN_ETH_FILE_NAME, 0666, tp_mroute_dir_entry);
	if (!tp_mroute_lan_eth_entry)
	{
		printk("(%s,%d)cannot create %s proc entry", __FUNCTION__, __LINE__, TP_MROUTE_LAN_ETH_FILE_NAME);
		return -ENOENT;
	}
	tp_mroute_lan_eth_entry->write_proc = tp_mroute_lan_eth_write;
	tp_mroute_lan_eth_entry->read_proc	= tp_mroute_lan_eth_read;

	tp_mroute_wan_eth_entry = create_proc_entry(TP_MROUTE_WAN_ETH_FILE_NAME, 0666, tp_mroute_dir_entry);
	if (!tp_mroute_wan_eth_entry)
	{
		printk("(%s,%d)cannot create %s proc entry", __FUNCTION__, __LINE__, TP_MROUTE_WAN_ETH_FILE_NAME);
		return -ENOENT;
	}
	tp_mroute_wan_eth_entry->write_proc = tp_mroute_wan_eth_write;
	tp_mroute_wan_eth_entry->read_proc	= tp_mroute_wan_eth_read;

	return 0;
}


static void tp_mroute_proc_exit(void)
{
	if (tp_mroute_wan_eth_entry)
	{
		remove_proc_entry(TP_MROUTE_WAN_ETH_FILE_NAME, tp_mroute_dir_entry);
	}

	if (tp_mroute_lan_eth_entry)
	{
		remove_proc_entry(TP_MROUTE_LAN_ETH_FILE_NAME, tp_mroute_dir_entry);
	}

	if (tp_mroute_lan_br_entry)
	{
		remove_proc_entry(TP_MROUTE_LAN_BR_FILE_NAME, tp_mroute_dir_entry);
	}

	if (tp_mroute_enable_entry)
	{
		remove_proc_entry(TP_MROUTE_ENABLE_FILE_NAME, tp_mroute_dir_entry);
	}

	if (tp_mroute_dir_entry)
	{
		remove_proc_entry(TP_MROUTE_DIR_NAME, init_net.proc_net);
	}	

	return;
}


#endif

int tp_mroute_is_enable(void)
{
	return tp_mroute_enable;
}


static struct mc_entry *hlist_sort(__u32 group);

/*
 * 	create IGMP packet's IP header
 */
static struct sk_buff *create_igmp_ip_skb(struct net_device *dev, __u32 daddr)
{
    struct sk_buff *skb;
    struct iphdr *iph;
	struct rtable *rt = NULL;
	struct flowi fl = { .oif = dev->ifindex,
						.nl_u = { .ip4_u = { .daddr = daddr } },
						.proto = IPPROTO_IGMP };

	/* added by HouXB, 03Dec10 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31))
	struct net *net =  dev_net(dev);
#endif
	
	//eth0 don't have static route, use br0 instead
    if (dev->ifindex == lan_eth_index)
    {
        fl.oif = lan_br_index;
    }
	/* added by HouXB, 03Dec10 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31))
	if (ip_route_output_key(net, &rt, &fl))
#else
	if (ip_route_output_key(&rt, &fl))
#endif
    {
        printk("Ooops, static route igmp error!\n");
        return NULL;
    }
    
    if (rt->rt_src == 0)
    {
		ip_rt_put(rt);
        debugk("no igmp route source\n");
		return NULL;
	}

    skb = alloc_skb(IGMP_SIZE+LL_RESERVED_SPACE(dev), GFP_ATOMIC);
	if (skb == NULL)
    {
		ip_rt_put(rt);
		return NULL;
	}

/* added by HouXB, 03Dec10 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31))
	skb_dst_set(skb, &rt->u.dst);
#else
	skb->dst = &rt->u.dst;
#endif

	skb_reserve(skb, LL_RESERVED_SPACE(dev));
	iph = (struct iphdr *)skb_put(skb, sizeof(struct iphdr) + 4);
	
/* added by HouXB, 03Dec10 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31))	
	skb->network_header = (sk_buff_data_t)iph;	
#else
	skb->nh.iph = iph;
#endif	

	iph->version  = 4;
	iph->ihl      = (sizeof(struct iphdr)+4) >> 2;
	iph->tos      = 0xc0;
	iph->frag_off = htons(IP_DF);
	iph->ttl      = 1;
	iph->daddr    = daddr;
	iph->saddr    = rt->rt_src;
	iph->protocol = IPPROTO_IGMP;
	iph->tot_len  = htons(IGMP_SIZE);
	ip_select_ident(iph, &rt->u.dst, NULL);
	
	((u8*)&iph[1])[0] = IPOPT_RA;
	((u8*)&iph[1])[1] = 4;
	((u8*)&iph[1])[2] = 0;
	((u8*)&iph[1])[3] = 0;
	ip_send_check(iph);

    return skb;
}

/*
 *	fulfill IGMP report header and send 
 */
static int tp_send_igmp_report(struct net_device *dev, __u32 daddr)
{
    struct sk_buff *skb;
    struct igmphdr *ih;
    
    if ((skb = create_igmp_ip_skb(dev, daddr)) == NULL)
    {
        return -1;
    }

	ih = (struct igmphdr *)skb_put(skb, sizeof(struct igmphdr));
	ih->type = IGMPV2_HOST_MEMBERSHIP_REPORT;
	ih->code = 0;
	ih->csum = 0;
	ih->group = daddr;
	ih->csum = ip_compute_csum((void *)ih, sizeof(struct igmphdr));

    return ip_output(skb);
}

/*
 *	fulfill IGMP query header and send 
 */
static int tp_send_igmp_query(struct net_device *dev, __u32 daddr, __u32 group, __u8 delay)
{
    struct sk_buff *skb;
    struct igmphdr *ih;
    
    if ((skb = create_igmp_ip_skb(dev, daddr)) == NULL)
    {
        debugk("create IGMP pack failed\n");
        return -1;
    }

	ih = (struct igmphdr *)skb_put(skb, sizeof(struct igmphdr));
	ih->type = IGMP_HOST_MEMBERSHIP_QUERY;
	ih->code = delay;
	ih->csum = 0;
	ih->group = group;
	ih->csum = ip_compute_csum((void *)ih, sizeof(struct igmphdr));

    return ip_output(skb);
}

/*
 *	fulfill IGMP leave header and send 
 */
static int tp_send_igmp_leave(struct net_device *dev, __u32 group)
{
    struct sk_buff *skb;
    struct igmphdr *ih;
    
    if ((skb = create_igmp_ip_skb(dev, IGMP_ALL_ROUTER_ADDR)) == NULL)
    {
        return -1;
    }

	ih = (struct igmphdr *)skb_put(skb, sizeof(struct igmphdr));
	ih->type = IGMP_HOST_LEAVE_MESSAGE;
	ih->code = 0;
	ih->csum = 0;
	ih->group = group;
	ih->csum = ip_compute_csum((void *)ih, sizeof(struct igmphdr));

    return ip_output(skb);

}

/*
 *	send general query only
 */
static void general_query_send(char *dev_name)
{
    debugk("send general query\n");

/* added by HouXB, 03Dec10 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31))
	lan_br_dev = dev_get_by_name(&init_net, dev_name);
#else
	lan_br_dev = dev_get_by_name(dev_name);
#endif	
    
    if (tp_send_igmp_query(lan_br_dev, IGMP_ALL_HOST_ADDR, 0, QUERY_RESPONSE_INTERVAL_NUM * 10))
    {
        printk("Ooops, general query send failed!\n");
    }

	dev_put(lan_br_dev);
	
}

/*
 *	g-q timer expired, send general query & reset timer
 */
static void general_query_timer_expired(unsigned long data)
{
    debugk("g-q tiemer expired\n");
	
	if (!tp_mroute_enable)/* if IGMP is disabled, stop it */
	{
		table.generl_query_timer.expires = jiffies + QUERY_INTERVAL;
    	add_timer(&table.generl_query_timer);
		return;
	}	
	
/* added by HouXB, 03Dec10 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31))
	lan_br_dev = dev_get_by_name(&init_net, lan_br_name);
#else
	lan_br_dev = dev_get_by_name(lan_br_name);
#endif	
    
    if (tp_send_igmp_query(lan_br_dev, IGMP_ALL_HOST_ADDR, 0, QUERY_RESPONSE_INTERVAL_NUM * 10))
    {
        printk("Ooops, general query send failed!\n");
    }

	dev_put(lan_br_dev);
    
    table.generl_query_timer.expires = jiffies + QUERY_INTERVAL;
    add_timer(&table.generl_query_timer);

	return;
}

/*
 *	r-c timer expired, check all items, remove the unreported one
 */
static void report_checking_timer_expired(unsigned long data)
{
    int i;

    struct mc_entry *mcp;
	
	debugk("r-c timer expired\n");

    for (i = 0; i < MAX_GROUP_ENTRIES; i++)
    {
        mcp = &table.entry[i];
		//if mcp->mc_addr is 0, it must not be used
        if (!mcp->reported && mcp->mc_addr)
        {
			//the timer of the removing item must be stopped
            if (timer_pending(&mcp->wan_qr_timer))
            {
                del_timer(&mcp->wan_qr_timer);
            }

            spin_lock_bh(table.lock);
            __hlist_del((struct hlist_node *)mcp);
            hlist_add_head((struct hlist_node *)mcp, &table.empty_list);
            table.groups--;
            spin_unlock_bh(table.lock);
			
			/* added by HouXB, 03Dec10 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31))
			wan_eth_dev = dev_get_by_name(&init_net, wan_eth_name);
#else
			wan_eth_dev = dev_get_by_name(wan_eth_name);
#endif	
		
            tp_send_igmp_leave(wan_eth_dev, mcp->mc_addr);
			dev_put(wan_eth_dev);
			
            mcp->mc_addr = 0;
			if(mcp->dst)
			{
				dst_release(mcp->dst);
				mcp->dst = NULL;
			}

            debugk("del some tips from mc_table\n");
        }
		// clear the report flag
        mcp->reported = 0;
    }

	// if no tips alive, stop all function
    if (!table.groups)
    {
        if (timer_pending(&table.report_checking_timer))
        {
            del_timer(&table.report_checking_timer);
        }

        return;
    }
    
    table.report_checking_timer.expires = jiffies + GROUP_MEMBER_SHIP_INTERVAL;
    add_timer(&table.report_checking_timer);
}

static void all_group_send_igmp_leave(void)
{
	int i;
    struct mc_entry *mcp = NULL;

    for (i = 0; i < MAX_GROUP_ENTRIES; i++)
    {
        mcp = &table.entry[i];
		
        if (mcp->mc_addr)
        {
			//the timer of the removing item must be stopped
            if (timer_pending(&mcp->wan_qr_timer))
            {
                del_timer(&mcp->wan_qr_timer);
            }

            spin_lock_bh(table.lock);
            __hlist_del((struct hlist_node *)mcp);
            hlist_add_head((struct hlist_node *)mcp, &table.empty_list);
            table.groups--;
            spin_unlock_bh(table.lock);
			
/* added by HouXB, 03Dec10 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31))
			wan_eth_dev = dev_get_by_name(&init_net, wan_eth_name);
#else
			wan_eth_dev = dev_get_by_name(wan_eth_name);
#endif		
            tp_send_igmp_leave(wan_eth_dev, mcp->mc_addr);
			dev_put(wan_eth_dev);
			
            mcp->mc_addr = 0;
			if(mcp->dst)
			{
				dst_release(mcp->dst);
				mcp->dst = NULL;
			}
        }
		// clear the report flag
        mcp->reported = 0;
    }

	// if no tips alive, stop all function
    if (!table.groups)
    {
		//keep g-q timer, yzt 2010-02-21
        if (timer_pending(&table.report_checking_timer))
        {
            del_timer(&table.report_checking_timer);
        }
		
		printk("(%s)%d, has delete all IGMP groups and timer\n", __FUNCTION__, __LINE__);
    }

	return;
    
}


/*
 *	wan q-r timer expired, send report to wan port
 */
static void wan_qr_timer_expired(unsigned long data)
{
    int i = (int) data;

    struct mc_entry *mcp = &table.entry[i];
	
	debugk("wan q-r timer expired\n");
	
	/* added by HouXB, 03Dec10 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31))
	wan_eth_dev = dev_get_by_name(&init_net, wan_eth_name);
#else
	wan_eth_dev = dev_get_by_name(wan_eth_name);
#endif	
    tp_send_igmp_report(wan_eth_dev, mcp->mc_addr);
	dev_put(wan_eth_dev);
}

static void lan_leave_timer_expired(unsigned long data)
{
	struct mc_entry *mcp;

	debugk("lan leave timer expired\n");	
	mcp = hlist_sort((__u32)data);

	if (mcp)
	{
		/*
		 * timer is expired, so there are no member in this GROUP
		 * leave the group
		 */
		if (!mcp->membership_flag)
		{
			if (timer_pending(&mcp->wan_qr_timer))
			{
				del_timer(&mcp->wan_qr_timer);
			}

			spin_lock_bh(table.lock);
			__hlist_del((struct hlist_node *)mcp);
			hlist_add_head((struct hlist_node *)mcp, &table.empty_list);
			table.groups--;
			spin_unlock_bh(table.lock);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31))
			wan_eth_dev = dev_get_by_name(&init_net, wan_eth_name);
#else
			wan_eth_dev = dev_get_by_name(wan_eth_name);
#endif	
			tp_send_igmp_leave(wan_eth_dev, (__u32)data);
			dev_put(wan_eth_dev);

			mcp->mc_addr = 0;
			mcp->reported = 0;
			if(mcp->dst)
			{
				dst_release(mcp->dst);
				mcp->dst = NULL;
			}

			debugk("some tips removed from mc_table\n");

			if (!table.groups)
			{
				del_timer(&table.report_checking_timer);
			}
		}		
	}
}

/*
 * initaily get the device pointer
 */
static int update_dev_index(void)
{
	/* added by HouXB, 03Dec10 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31))
    lan_br_dev = dev_get_by_name(&init_net, lan_br_name);
    lan_eth_dev = dev_get_by_name(&init_net, lan_eth_name);
    wan_eth_dev = dev_get_by_name(&init_net, wan_eth_name);
#else
    lan_br_dev = dev_get_by_name(lan_br_name);
    lan_eth_dev = dev_get_by_name(lan_eth_name);
    wan_eth_dev = dev_get_by_name(wan_eth_name);
#endif	

    if (lan_br_dev && lan_eth_dev && wan_eth_dev)
    {
		lan_br_index = lan_br_dev->ifindex;
		lan_eth_index = lan_eth_dev->ifindex;
		wan_eth_index = wan_eth_dev->ifindex;

		dev_put(lan_eth_dev);
		dev_put(wan_eth_dev);
		dev_put(lan_br_dev);
		
        return 0;
    }
    else
    {
		if(lan_br_dev)
		{
			dev_put(lan_br_dev);
		}
		if(lan_eth_dev)
		{
			dev_put(lan_eth_dev);
		}
		if(wan_eth_dev)
		{
			dev_put(wan_eth_dev);
		}
		
        return -1;
    }
}

/*
 * find the items by the muticast group
 */
static struct mc_entry *hlist_sort(__u32 group)
{
    struct hlist_node *hnp;
    struct mc_entry *mcp;
    
    for (hnp = table.mc_hash[HASH256(group)].first; hnp != NULL; hnp = hnp->next)
    {
        mcp = (struct mc_entry *)hnp;
        if (mcp->mc_addr == group)
        {
            return mcp;
        }
    }
    return NULL;
}

static void disable_tp_mroute(void)
{
	/* all groups must send leave msg to WAN */
	all_group_send_igmp_leave();
	return;
}

/*
 * initail the mc table, zero the muticast group, define the timer expire function
 */
int mc_table_init(void)
{
    int i;

    debugk("start initial\n");
    if (update_dev_index())
    {
        printk("Ooops, why the devices couldn't been initialed?\n");
        return -1;
    }
    
    for (i = 0; i < MAX_HASH_ENTRIES; i++)
    {
        table.mc_hash[i].first = NULL;
    }
    
    for (i = MAX_GROUP_ENTRIES - 1; i >= 0; i--)
    {
        hlist_add_head((struct hlist_node *)&table.entry[i], &table.empty_list);
        init_timer(&table.entry[i].wan_qr_timer);
        table.entry[i].wan_qr_timer.function = wan_qr_timer_expired;
        table.entry[i].wan_qr_timer.data = (unsigned long)i;
		table.entry[i].lan_leave_timer.function = lan_leave_timer_expired;
		table.entry[i].lan_leave_timer.data = (unsigned long)i;
		init_timer(&table.entry[i].lan_leave_timer);
        table.entry[i].membership_flag = 0;
        table.entry[i].reported = 0;
        table.entry[i].mc_addr = 0;
		table.entry[i].dst = NULL;
    }
    init_timer(&table.generl_query_timer);
    table.generl_query_timer.function = general_query_timer_expired;
	
	//start g-q timer here, yzt 2010-02-21
	table.generl_query_timer.expires = jiffies + QUERY_INTERVAL;
	add_timer(&table.generl_query_timer);

    init_timer(&table.report_checking_timer);
    table.report_checking_timer.function = report_checking_timer_expired;
    table.groups = 0;
    spin_lock_init(&table.lock);

    initialed = 1;
    return 0;
}

/*
 * calssify the muticast packet, deliver to different function
 */
int tp_mr_classify(struct sk_buff *skb)
{

/* added by HouXB, 03Dec10 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31))
	struct iphdr *iph = (struct iphdr *)skb_network_header(skb);
#else
	struct iphdr *iph = (struct iphdr *)skb->nh.iph;
#endif

	struct igmphdr *ih = NULL;

    __u32 daddr = iph->daddr;

	//init table is not initialed
    if (!initialed)
    {
        if (mc_table_init())
        {
            goto drop;
        }
    }

	if (!tp_mroute_enable)/* if not enable IGMP, just drop it */
	{
		goto drop;
	}
	

/* added by HouXB, 03Dec10 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31))
	if (!ipv4_is_multicast(iph->daddr))
#else
	if (!MULTICAST(iph->daddr))
#endif		
    {
        goto drop;
    }

	//deliver WAN data packet
    if (iph->protocol == IPPROTO_UDP)
    {
        if (skb->dev->ifindex == wan_eth_index)
        {
            return find_data_path(skb, iph->daddr);
        }
        else
        {
            debugk("drop lan data pack\n");
            goto drop;
        }
    }

/* added by HouXB, 03Dec10 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31))
	__skb_pull(skb, iph->ihl*4);
	skb_set_transport_header(skb, 0);	
	ih = (struct igmphdr *)skb_transport_header(skb);
#else
	__skb_pull(skb, skb->nh.iph->ihl*4);
	skb->h.raw = skb->data;
    ih = (struct igmphdr *)skb->h.igmph;
#endif		

    if (iph->protocol == IPPROTO_IGMP)
    {
        debugk("heard IGMP pack\n");

		//copy from igmp.c, drop broken packet
        if (!pskb_may_pull(skb, sizeof(struct igmphdr)))
        {
        	printk("heard IGMP pack and drop it\n");
            goto drop;
        }

        if (skb->dev->ifindex == lan_br_index)
        {
            debugk("heard lan IGMP pack\n");
			
            switch (ih->type)
            {
            case IGMP_HOST_MEMBERSHIP_REPORT:
            case IGMPV2_HOST_MEMBERSHIP_REPORT:
                return lan_heard_igmp_report(skb, ih->group);
                break;
			case IGMPV3_HOST_MEMBERSHIP_REPORT:
				/*send v2 query to br0*/
				debugk("heard IGMP v3 report\n");
				general_query_send(lan_br_name);
				break;
                    
            case IGMP_HOST_LEAVE_MESSAGE:
                return lan_heard_igmp_leave(skb, ih->group);
                break;
                
            default:
                break;
            }
        }
        else if (skb->dev->ifindex == wan_eth_index)
        {
            debugk("heard wan IGMP pack\n");
            switch (ih->type)
            {
            case IGMP_HOST_MEMBERSHIP_REPORT:
            case IGMPV2_HOST_MEMBERSHIP_REPORT:
                return wan_heard_igmp_report(skb, ih->group);
                break;
                    
            case IGMP_HOST_MEMBERSHIP_QUERY:
                return wan_heard_igmp_query(skb, ih, daddr);
                break;
                
            default:
                break;
            }
            
        }
    }

drop:
    kfree_skb(skb);
    return -1;
}

/*
 * solve igmp report packet, may add/modify items
 */
int lan_heard_igmp_report(struct sk_buff *skb, __u32 group)
{
    struct mc_entry *mcp;

    debugk("heard lan igmp report\n");
    if (!table.groups)
    {
        table.report_checking_timer.expires = jiffies + GROUP_MEMBER_SHIP_INTERVAL;
        add_timer(&table.report_checking_timer);
    }
    else if ((mcp = hlist_sort(group)) != NULL)
    {
		//modify port infomation
        mcp->membership_flag |= skb->mark;
        mcp->reported = 1;
        goto end;
    }
    else if (table.groups == MAX_GROUP_ENTRIES)
    {
        printk("Ooops, groups full, why defines so little table?\n");
        goto end;
    }
    
    mcp = (struct mc_entry *)table.empty_list.first;
    mcp->mc_addr = group;
	
	//save port information
    mcp->membership_flag = skb->mark;
    mcp->reported = 1;
    spin_lock_bh(table.lock);
    __hlist_del(table.empty_list.first);
    hlist_add_head((struct hlist_node *) mcp, &table.mc_hash[HASH256(group)]);
    table.groups++;
    spin_unlock_bh(table.lock);
    debugk("some tips added to mc_table\n");

/* added by HouXB, 03Dec10 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31))
	wan_eth_dev = dev_get_by_name(&init_net, wan_eth_name);
#else
	wan_eth_dev = dev_get_by_name(wan_eth_name);
#endif	

    tp_send_igmp_report(wan_eth_dev, group);
	dev_put(wan_eth_dev);
    
end:
    kfree_skb(skb);
    return 0;
}

/*
 * solve igmp leave packet, may remove items
 */
int lan_heard_igmp_leave(struct sk_buff *skb, __u32 group)
{
    struct mc_entry *mcp;

    debugk("heard lan igmp leave\n");
    mcp = hlist_sort(group);
	
    if (mcp)
    {
		/* clear membership flag 
		 * send IGMP query
		 * if there are other member in this group, it will report
		 * and will reset membership flag
		 */
        	mcp->membership_flag &= (~skb->mark);

		if (!timer_pending(&mcp->lan_leave_timer))
            {
			mcp->lan_leave_timer.data = (unsigned long) group;
			mcp->lan_leave_timer.function = lan_leave_timer_expired;
			mcp->lan_leave_timer.expires = jiffies + QUERY_INTERVAL;
            
			add_timer(&mcp->lan_leave_timer);
			
			/* 
			 * send IGMP group-specific query to this group 
			 * if there are other member join this group, it will recv a report pkt
			 * to update membership_flag
			 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31))
			lan_br_dev= dev_get_by_name(&init_net, lan_br_name);
#else
			lan_br_dev = dev_get_by_name(lan_br_name);
#endif	
			if (tp_send_igmp_query(lan_br_dev, IGMP_ALL_HOST_ADDR, group, QUERY_RESPONSE_INTERVAL_NUM * 10))
			{
				printk("Ooops, general query send failed!\n");
			}
			
			dev_put(lan_br_dev);
        }
    }

    kfree_skb(skb);
    return 0;    
}

/*
 * solve wan query packet, set timer for all alive items
 */
int wan_heard_igmp_query(struct sk_buff *skb, struct igmphdr *ih, __u32 daddr)
{
    int i;
    struct mc_entry *mcp;

    debugk("heard wan igmp query\n");
	//if empty do not resolve
    if (table.groups)
    {
		//general query
        if (!ih->group && daddr == IGMP_ALL_HOST_ADDR)
        {
            for (i = 0; i < MAX_GROUP_ENTRIES; i++)
            {
                mcp = &table.entry[i];
                if (mcp->mc_addr)
                {
                    mcp->wan_qr_timer.expires = jiffies + (jiffies % QUERY_RESPONSE_INTERVAL_NUM) * HZ;
					if (!timer_pending(&mcp->wan_qr_timer))
					{
                    	add_timer(&mcp->wan_qr_timer);
					}
                }
            }
        }
        else if (ih->group == daddr)
        {
			// g-s query
            mcp = hlist_sort(daddr);
            if (mcp)
            {
                mcp->wan_qr_timer.expires = jiffies + (jiffies % ih->code) * HZ / 10;
				if (!timer_pending(&mcp->wan_qr_timer))
				{
                	add_timer(&mcp->wan_qr_timer);
				}
            }
        }
    }

    kfree_skb(skb);
    return 0;
}

/*
 * solve wan report packet, stop the q-r timer
 */
int wan_heard_igmp_report(struct sk_buff *skb, __u32 group)
{
    struct mc_entry *mcp;

    debugk("heard wan igmp report\n");
    mcp = hlist_sort(group); 
    if (mcp)
    {
        if (timer_pending(&mcp->wan_qr_timer))
        {
            del_timer(&mcp->wan_qr_timer);
        }
    }

    kfree_skb(skb);
    return 0;    
}

/*
 * solve wan data packet, find the out way, set port information
 */
int find_data_path(struct sk_buff *skb, __u32 group)
{
    struct mc_entry *mcp;
	struct rtable *rt;

	#define IGMP_DATA_DOWN_FLAG 0x00100000
	
/* added by HouXB, 03Dec10 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31))
	struct iphdr *iph = (struct iphdr *)skb_network_header(skb);
	struct net *net =  dev_net(skb->dev);
#else
	struct iphdr *iph = (struct iphdr *)skb->nh.iph;
#endif	

    debugk("heard data pack\n");

	/*printk("heard data pack, group is %x\n", group);*/
    mcp = hlist_sort(group); 
	
    if (mcp)
    {
        skb->mark = mcp->membership_flag | IGMP_DATA_DOWN_FLAG;
        debugk("deliver data packet\n");		

		/* modified by HouXB, 03Dec10 */
		{
		struct flowi fl = { .oif = lan_br_index,
		            .nl_u = { .ip4_u = { .daddr = iph->daddr } },
		            .proto = iph->protocol };

		if(!mcp->dst)//if no route backup
		{
/*get route info. added by HouXB, 03Dec10 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31))
			if (ip_route_output_key(net, &rt, &fl))
#else
			if (ip_route_output_key(&rt, &fl))
#endif
		    {
		        printk("Ooops, static route udp out error!\n");
				kfree(skb);
		        return -1;
		    }
			else if (rt->rt_src == 0)
			{
				ip_rt_put(rt);
				debugk("no route source\n");
				kfree(skb);
				return -1;
			}
			else
			{
				mcp->dst = &rt->u.dst;//backup route info			
			}
		}
		}

/* added by HouXB, 03Dec10 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31))
		skb_dst_set(skb, mcp->dst);
#else
		skb->dst = mcp->dst;
#endif			
		mcp->dst->lastuse = jiffies;
		dst_hold(mcp->dst);
		mcp->dst->__use++;
	    return ip_output(skb);		
    }
    else
    {
        debugk("no tips of that group, drop\n");
        kfree_skb(skb);
        return -1;
    }

	#undef IGMP_DATA_DOWN_FLAG
}

static int __init tp_mroute_init(void)
{
	mc_table_init();
	
#ifdef CONFIG_PROC_FS
		tp_mroute_proc_init();
#endif

	return 0;
}

static void __exit tp_mroute_exit(void)
{
	
#ifdef CONFIG_PROC_FS
	tp_mroute_proc_exit();
#endif
	disable_tp_mroute();
	return;
}

module_init(tp_mroute_init);
module_exit(tp_mroute_exit);

EXPORT_SYMBOL(tp_mroute_is_enable);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Huang WenHao <huangwenhao@tp-link.net>");
MODULE_DESCRIPTION("The IGMP Module for IPTV.");
MODULE_ALIAS("The IGMP Module");


/*
 *	send multicast packet //not used now, igmp and udp packets are solved seperately 
 */
 /*
int tp_send_mc(struct sk_buff *skb, struct net_device *dev)
{
    struct rtable *rt;

    struct flowi fl = { .oif = dev->ifindex,
                .nl_u = { .ip4_u = { .daddr = skb->nh.iph->daddr } },
                .proto = skb->nh.iph->protocol };

	//eth0 don't have static route, use br0 instead
    if (dev->ifindex == eth0_dev->ifindex)
    {
        fl.oif = br0_dev->ifindex;
    }
    
    if (ip_route_output_key(&rt, &fl))
    {
        printk("Ooops, static route don't kown this output interface!\n");
		kfree(skb);
        return -1;
    }
    
    if (rt->rt_src == 0)
    {
        debugk("route source\n");
        ip_rt_put(rt);
		kfree(skb);
		return -1;
	}
    
    skb->dst = (struct dst_entry*) rt;
	//change back to original dev, do not send to wireless
    skb->dev = dev;
    skb->dst->dev = dev;

    return ip_output(skb);
}
*/

