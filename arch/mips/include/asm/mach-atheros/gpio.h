#ifndef __GPIO_H
#define __GPIO_H

#include <linux/config.h>

/*
 * IOCTL Command Codes
 */

#define AR7240_GPIO_IOCTL_BASE			0x01
#define AR7240_GPIO_IOCTL_CMD1      	AR7240_GPIO_IOCTL_BASE
#define AR7240_GPIO_IOCTL_CMD2      	AR7240_GPIO_IOCTL_BASE + 0x01
#define AR7240_GPIO_IOCTL_CMD3      	AR7240_GPIO_IOCTL_BASE + 0x02
#define AR7240_GPIO_IOCTL_CMD4      	AR7240_GPIO_IOCTL_BASE + 0x03
#define AR7240_GPIO_IOCTL_CMD5      	AR7240_GPIO_IOCTL_BASE + 0x04
#define AR7240_GPIO_IOCTL_CMD6      	AR7240_GPIO_IOCTL_BASE + 0x05
#define AR7240_GPIO_IOCTL_CMD7      	AR7240_GPIO_IOCTL_BASE + 0x06
#define AR7240_GPIO_IOCTL_MAX			AR7240_GPIO_IOCTL_CMD7

#define AR7240_GPIO_MAGIC 				0xB2
#define	AR7240_GPIO_BTN_READ			_IOR(AR7240_GPIO_MAGIC, AR7240_GPIO_IOCTL_CMD1, int)
#define	AR7240_GPIO_LED_READ			_IOR(AR7240_GPIO_MAGIC, AR7240_GPIO_IOCTL_CMD2, int)
#define	AR7240_GPIO_LED_WRITE			_IOW(AR7240_GPIO_MAGIC, AR7240_GPIO_IOCTL_CMD3, int)

#define AR7240_GPIO_USB_LED1_WRITE		_IOW(AR7240_GPIO_MAGIC, AR7240_GPIO_IOCTL_CMD4, int)

#define AR7240_GPIO_USB_1_LED1_WRITE	_IOW(AR7240_GPIO_MAGIC, AR7240_GPIO_IOCTL_CMD5, int)
#define AR7240_GPIO_USB_POWER_WRITE		_IOW(AR7240_GPIO_MAGIC, AR7240_GPIO_IOCTL_CMD6, int)
#define AR7240_GPIO_USB_1_POWER_WRITE	_IOW(AR7240_GPIO_MAGIC, AR7240_GPIO_IOCTL_CMD7, int)

#define gpio_major      				238
#define gpio_minor      				0

/*
 * GPIO assignment
 */

/*
 * RST_WPS Button		SYS_LED		WPS_LED		WIFI_RADIO_SW	USB_0_LED	USB_1_LED
 		16				   14		   15			17				11			12			
 */
 
/* reset default(WPS) button, default is GPIO16 */
#define RST_DFT_GPIO		CONFIG_GPIO_RESET_FAC_BIT
/* How long the user must press the button before Router rst, default is 5 */
#define RST_HOLD_TIME		CONFIG_GPIO_FAC_RST_HOLD_TIME

/* system LED, default is GPIO14 	*/
#define SYS_LED_GPIO		CONFIG_GPIO_READY_STATUS_BIT
/* system LED's value when off, default is 1 */
#define SYS_LED_OFF         (!CONFIG_GPIO_READY_STATUS_ON)

/* do not mux the RST&WPS button */
#ifndef CONFIG_MUX_RESET_WPS_BUTTON
#define WPS_BUTTON_GPIO		CONFIG_GPIO_WPSSTART_SW_BIT
#endif

/* do config wifi power button */
#ifdef CONFIG_TX_PWR_BUTTON
#define WIFI_TX_PWR_GPIO		CONFIG_GPIO_TXPWR_SW_BIT
#define TXPWR_LED_GPIO CONFIG_GPIO_TXPWR_LED_BIT
#define TXPWR_ON CONFIG_GPIO_TXPWR_LED_ON
#define TXPWR_OFF (!CONFIG_GPIO_TXPWR_LED_ON)
#endif

/* QSS default button, default is None, we use RST&QSS MUX button */
/* QSS(WPS) LED, default is GPIO15 */
#define TRICOLOR_LED_GREEN_PIN  CONFIG_GPIO_JUMPSTART_LED_BIT
/* jump start LED's value when off */
#define OFF     (!CONFIG_GPIO_JUMPSTART_LED_ON)
/* jump start LED'S value when on, default is 0 */
#define ON      CONFIG_GPIO_JUMPSTART_LED_ON
/* WiFi switch, default is GPIO17 */
#ifdef CONFIG_ENABLE_WIFI_BUTTON
#define WIFI_RADIO_SW_GPIO	CONFIG_GPIO_WIFI_SWITCH_BIT
#endif

/* USB 0 by lyj, 31Aug11 */
#ifdef CONFIG_GPIO_USB_0_LED_BIT
/* USB 0 LED, default is GPIO11 */
#define AP_USB_LED_GPIO     CONFIG_GPIO_USB_0_LED_BIT
/* USB 0 LED's value when off */
#define USB_LED_OFF         (!CONFIG_GPIO_USB_0_LED_ON)
/* USB 0 LED's value when on, default is 0 */
#define USB_LED_ON          CONFIG_GPIO_USB_0_LED_ON

/* for USB 3G by lyj, 31Aug11 */
/* usb power switch for usb port 0 */
#define USB_POWER_SW_GPIO	CONFIG_GPIO_USB_0_SWITCHFOR3G_BIT
#define USB_POWER_ON		1
#define USB_POWER_OFF		(!USB_POWER_ON)

#else
#undef AP_USB_LED_GPIO
#undef USB_POWER_SW_GPIO
#endif

/* USB 1 LED by lyj, 31Aug11 */
#ifdef CONFIG_GPIO_USB_1_LED_BIT
/* USB 1 LED, default is GPIO11 */
#define AP_USB_1_LED_GPIO     CONFIG_GPIO_USB_1_LED_BIT
/* USB 1 LED's value when off */
#define USB_1_LED_OFF         (!CONFIG_GPIO_USB_1_LED_ON)
/* USB 1 LED's value when on, default is 0 */
#define USB_1_LED_ON          CONFIG_GPIO_USB_1_LED_ON

#define USB_1_POWER_SW_GPIO	CONFIG_GPIO_USB_1_SWITCHFOR3G_BIT
#define USB_1_POWER_ON		1
#define USB_1_POWER_OFF		(!USB_1_POWER_ON)

#else
#undef AP_USB_1_LED_GPIO
#undef USB_1_POWER_SW_GPIO
#endif


/*
 * GPIO interrupt stuff
 */
typedef enum {
	INT_TYPE_EDGE,
	INT_TYPE_LEVEL,
} ath_gpio_int_type_t;

typedef enum {
	INT_POL_ACTIVE_LOW,
	INT_POL_ACTIVE_HIGH,
} ath_gpio_int_pol_t;


/*changed by hujia.*/
typedef irqreturn_t(*sc_callback_t)(int, void *, struct pt_regs *, void *);

/*
 * Multiple Simple Config callback support
 * For multiple radio scenarios, we need to post the button push to
 * all radios at the same time.  However, there is only 1 button, so
 * we only have one set of GPIO callback pointers.
 *
 * Creating a structure that contains each callback, tagged with the
 * name of the device registering the callback.  The unregister routine
 * will need to determine which element to "unregister", so the device
 * name will have to be passed to unregister also
 */

typedef struct {
	char		*name;
	sc_callback_t	registered_cb;
	void		*cb_arg1;
	void		*cb_arg2;
} multi_callback_t;

typedef enum {
        LED_STATE_OFF   =       1,
        LED_STATE_GREEN =       0,
        LED_STATE_YELLOW =      2,
        LED_STATE_ORANGE =      3,
        LED_STATE_MAX =         4
} led_state_e;

typedef enum {
		LED_PWR_ON	= 0,
        LED_PWR_OFF	= 1,
        LED_PWR_MAX	= 2
} txpwr_led_state_e;

#endif