#ifndef _TP_MROUTE_H
#define _TP_MROUTE_H

#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/spinlock.h>
#include <linux/list.h>

#include <linux/igmp.h>

#define MAX_GROUP_ENTRIES 8
#define MAX_HASH_ENTRIES 256

#define IGMP_ALL_HOST_ADDR htonl(0xE0000001L)
#define IGMP_ALL_ROUTER_ADDR htonl(0xE0000002L)

#define QUERY_RESPONSE_INTERVAL_NUM 10
#define QUERY_RESPONSE_INTERVAL (QUERY_RESPONSE_INTERVAL_NUM * HZ)
#define QUERY_INTERVAL (125 * HZ)
#define ROBUSTNESS_VARIABLE 2
#define GROUP_MEMBER_SHIP_INTERVAL ((ROBUSTNESS_VARIABLE) * (QUERY_INTERVAL) + QUERY_RESPONSE_INTERVAL)

#define HASH256(addr) ((addr)&0xFF)

struct mc_entry
{
	struct hlist_node head;
	__u32 mc_addr;
	__u32 membership_flag;
	__u8 reported;
	struct	dst_entry	*dst;
	struct timer_list wan_qr_timer;
	struct timer_list lan_leave_timer;
};

struct mc_table
{
	struct mc_entry entry[MAX_GROUP_ENTRIES];
	struct hlist_head mc_hash[MAX_HASH_ENTRIES];
	struct hlist_head empty_list;
	struct timer_list generl_query_timer;
	struct timer_list report_checking_timer;
	spinlock_t lock;
	__u8 groups;
};

/*  by huangwenzhong, 13Sep12 */
#ifdef CONFIG_PROC_FS

#define	TP_MROUTE_DIR_NAME				"tp_mroute"
#define	TP_MROUTE_ENABLE_FILE_NAME		"tp_mroute_enable"
#define	TP_MROUTE_LAN_BR_FILE_NAME		"lan_br_name"
#define TP_MROUTE_LAN_ETH_FILE_NAME		"lan_eth_name"
#define TP_MROUTE_WAN_ETH_FILE_NAME		"wan_eth_name"

#endif

int tp_mroute_is_enable(void);

int mc_table_init(void);
int tp_mr_classify(struct sk_buff *skb);
int lan_heard_igmp_report(struct sk_buff *skb, __u32 group);
int lan_heard_igmp_leave(struct sk_buff *skb, __u32 group);
int wan_heard_igmp_report(struct sk_buff *skb, __u32 group);
int wan_heard_igmp_query(struct sk_buff *skb, struct igmphdr *ih, __u32 daddr);
int find_data_path(struct sk_buff *skb, __u32 group);

int tp_send_mc(struct sk_buff *skb, struct net_device *dev);

#endif

