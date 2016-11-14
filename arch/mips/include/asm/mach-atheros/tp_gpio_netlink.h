#ifndef __TP_GPIO_NETLINK_H
#define __TP_GPIO_NETLINK_H

#define NETLINK_TP_GPIO 26
#define U_PID 1
#define K_MSG 2
#define D_GRP 1

/* make sure the receiver will use the same data struct */
struct tp_nl_gpio_msg
{
	int gpio_type;
	int gpio_port;
	int gpio_status;
};

enum GPIO_TYPE
{
	GPIO_WIFI_BUTTON = 1,
	GPIO_RST_BUTTON,
	GPIO_TXPWR_BUTTON,
	GPIO_TYPE_MAX,
};

struct sock * init_tp_gpio_netlink(void);
int tp_nl_gpio_send(struct sock *nl_sock, struct tp_nl_gpio_msg *info);

#endif