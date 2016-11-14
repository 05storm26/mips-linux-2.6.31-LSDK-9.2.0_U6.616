#ifndef EXPORT_SYMTAB
#define EXPORT_SYMTAB
#endif

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/signal.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/init.h>
#include <linux/resource.h>
#include <linux/proc_fs.h>
#include <asm/types.h>
#include <asm/irq.h>
#include <asm/delay.h>
#include <asm/system.h>

#include <linux/mtd/mtd.h>
#include <linux/cdev.h>
#include <linux/irqreturn.h>
#include <linux/workqueue.h>

#include <atheros.h>
#include <tp_gpio_netlink.h>
#include <gpio.h>

static struct sock *tp_gpio_nl_sk = NULL;

int counter = 0;
int jiff_when_press = 0;
int bBlockWps = 1;
struct timer_list rst_timer;

int g_usbLedBlinkCountDown = 1;

#ifdef CONFIG_MUX_RESET_WPS_BUTTON
/* control params for reset button reuse, by zjg, 13Apr10 */
static int l_bMultiUseResetButton		=	0;
static int l_bWaitForQss				= 	1;
#endif

EXPORT_SYMBOL(g_usbLedBlinkCountDown);

/*
 * Specific instance of the callback structure
 */
static multi_callback_t sccallback[2];

/* not mux the RST&WPS Button */
#ifndef CONFIG_MUX_RESET_WPS_BUTTON
static int ignore_pushbutton = 1;
#endif

static struct proc_dir_entry *simple_config_entry = NULL;
static struct proc_dir_entry *simulate_push_button_entry = NULL;
static struct proc_dir_entry *tricolor_led_entry = NULL;
static struct proc_dir_entry *wifi_button_entry = NULL;
static struct proc_dir_entry *txpwr_button_entry = NULL;
static struct proc_dir_entry *txpwr_led_entry = NULL;

#ifdef CONFIG_MUX_RESET_WPS_BUTTON
/* added by zjg, 12Apr10 */
static struct proc_dir_entry *multi_use_reset_button_entry = NULL;
#endif

/* ZJin 100317: for 3g usb led blink feature, use procfs simple config. */
static struct proc_dir_entry *usb_led_blink_entry = NULL;

#ifndef CONFIG_NO_USB
/*added by  ZQQ<10.06.02 for usb power*/
static struct proc_dir_entry *usb_power_entry = NULL;
#endif

void ath_gpio_config_int(int gpio,
			 ath_gpio_int_type_t type,
			 ath_gpio_int_pol_t polarity)
{
	u32 val;

	/*
	 * allow edge sensitive/rising edge too
	 */
	if (type == INT_TYPE_LEVEL) {
		/* level sensitive */
		ath_reg_rmw_set(ATH_GPIO_INT_TYPE, (1 << gpio));
	} else {
		/* edge triggered */
		val = ath_reg_rd(ATH_GPIO_INT_TYPE);
		val &= ~(1 << gpio);
		ath_reg_wr(ATH_GPIO_INT_TYPE, val);
	}

	if (polarity == INT_POL_ACTIVE_HIGH) {
		ath_reg_rmw_set(ATH_GPIO_INT_POLARITY, (1 << gpio));
	} else {
		val = ath_reg_rd(ATH_GPIO_INT_POLARITY);
		val &= ~(1 << gpio);
		ath_reg_wr(ATH_GPIO_INT_POLARITY, val);
	}

	ath_reg_rmw_set(ATH_GPIO_INT_ENABLE, (1 << gpio));
}

void ath_gpio_config_output(int gpio)
{
#if defined(CONFIG_MACH_AR934x) || defined(CONFIG_MACH_QCA955x)
	ath_reg_rmw_clear(ATH_GPIO_OE, (1 << gpio));
#else
	ath_reg_rmw_set(ATH_GPIO_OE, (1 << gpio));
#endif
}
EXPORT_SYMBOL(ath_gpio_config_output);

void ath_gpio_config_input(int gpio)
{
#if defined(CONFIG_MACH_AR934x) || defined(CONFIG_MACH_QCA955x)
	ath_reg_rmw_set(ATH_GPIO_OE, (1 << gpio));
#else
	ath_reg_rmw_clear(ATH_GPIO_OE, (1 << gpio));
#endif
}

void ath_gpio_out_val(int gpio, int val)
{
	if (val & 0x1) {
		ath_reg_rmw_set(ATH_GPIO_OUT, (1 << gpio));
	} else {
		ath_reg_rmw_clear(ATH_GPIO_OUT, (1 << gpio));
	}
}

EXPORT_SYMBOL(ath_gpio_out_val);

int ath_gpio_in_val(int gpio)
{
	return ((1 << gpio) & (ath_reg_rd(ATH_GPIO_IN)));
}

static void
ath_gpio_intr_enable(unsigned int irq)
{
	ath_reg_rmw_set(ATH_GPIO_INT_MASK,
				(1 << (irq - ATH_GPIO_IRQ_BASE)));
}

static void
ath_gpio_intr_disable(unsigned int irq)
{
	ath_reg_rmw_clear(ATH_GPIO_INT_MASK,
				(1 << (irq - ATH_GPIO_IRQ_BASE)));
}

static unsigned int
ath_gpio_intr_startup(unsigned int irq)
{
	ath_gpio_intr_enable(irq);
	return 0;
}

static void
ath_gpio_intr_shutdown(unsigned int irq)
{
	ath_gpio_intr_disable(irq);
}

static void
ath_gpio_intr_ack(unsigned int irq)
{
	ath_gpio_intr_disable(irq);
}

static void
ath_gpio_intr_end(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED | IRQ_INPROGRESS)))
		ath_gpio_intr_enable(irq);
}

static int
ath_gpio_intr_set_affinity(unsigned int irq, const struct cpumask *dest)
{
	/*
	 * Only 1 CPU; ignore affinity request
	 */
	return 0;
}

struct irq_chip /* hw_interrupt_type */ ath_gpio_intr_controller = {
	.name = "ATH GPIO",
	.startup = ath_gpio_intr_startup,
	.shutdown = ath_gpio_intr_shutdown,
	.enable = ath_gpio_intr_enable,
	.disable = ath_gpio_intr_disable,
	.ack = ath_gpio_intr_ack,
	.end = ath_gpio_intr_end,
	.eoi = ath_gpio_intr_end,
	.set_affinity = ath_gpio_intr_set_affinity,
};

void ath_gpio_irq_init(int irq_base)
{
	int i;

	for (i = irq_base; i < irq_base + ATH_GPIO_IRQ_COUNT; i++) {
		irq_desc[i].status = IRQ_DISABLED;
		irq_desc[i].action = NULL;
		irq_desc[i].depth = 1;
		//irq_desc[i].chip = &ath_gpio_intr_controller;
		set_irq_chip_and_handler(i, &ath_gpio_intr_controller,
					 handle_percpu_irq);
	}
}

/*
 *  USB GPIO control
 */

void ap_usb_led_on(void)
{
	/* we do not need this function, and usb hub drivers will call this function to let led on by lyj, 26Sep11 */
#if 0
#ifdef AP_USB_LED_GPIO  
	ath_gpio_out_val(AP_USB_LED_GPIO, USB_LED_ON);
#endif
#endif
}

EXPORT_SYMBOL(ap_usb_led_on);

void ap_usb_led_off(void)
{
#if 0
#ifdef AP_USB_LED_GPIO
	ath_gpio_out_val(AP_USB_LED_GPIO, USB_LED_OFF);
#endif
#endif
}

EXPORT_SYMBOL(ap_usb_led_off);

/*
 * USB 1 GPIO Control
 */

void ap_usb_1_led_on(void)
{
#ifdef AP_USB_1_LED_GPIO  
	ath_gpio_out_val(AP_USB_1_LED_GPIO, USB_1_LED_ON);
#endif
}

EXPORT_SYMBOL(ap_usb_1_led_on);

void ap_usb_1_led_off(void)
{
#ifdef AP_USB_1_LED_GPIO
	ath_gpio_out_val(AP_USB_1_LED_GPIO, USB_1_LED_OFF);
#endif
}

EXPORT_SYMBOL(ap_usb_1_led_off);

/*changed by hujia.*/
/*void register_simple_config_callback (void *callback, void *arg)*/
int32_t register_simple_config_callback(char *cbname, void *callback, void *arg1, void *arg2)
{
    printk("SC Callback Registration for %s\n", cbname);
	if (!sccallback[0].name) {
		sccallback[0].name = (char*)kmalloc(strlen(cbname), GFP_KERNEL);
		strcpy(sccallback[0].name, cbname);
		sccallback[0].registered_cb = (sc_callback_t) callback;
		sccallback[0].cb_arg1 = arg1;
		sccallback[0].cb_arg2 = arg2;
	} else if (!sccallback[1].name) {
		sccallback[1].name = (char*)kmalloc(strlen(cbname), GFP_KERNEL);
		strcpy(sccallback[1].name, cbname);
		sccallback[1].registered_cb = (sc_callback_t) callback;
		sccallback[1].cb_arg1 = arg1;
		sccallback[1].cb_arg2 = arg2;
	} else {
		printk("@@@@ Failed SC Callback Registration for %s\n", cbname);
		return -1;
	}
	return 0;
}
EXPORT_SYMBOL(register_simple_config_callback);

int32_t unregister_simple_config_callback(char *cbname)
{
	if (sccallback[1].name && strcmp(sccallback[1].name, cbname) == 0) {
		kfree(sccallback[1].name);
		sccallback[1].name = NULL;
		sccallback[1].registered_cb = NULL;
		sccallback[1].cb_arg1 = NULL;
		sccallback[1].cb_arg2 = NULL;
	} else if (sccallback[0].name && strcmp(sccallback[0].name, cbname) == 0) {
		kfree(sccallback[0].name);
		sccallback[0].name = NULL;
		sccallback[0].registered_cb = NULL;
		sccallback[0].cb_arg1 = NULL;
		sccallback[0].cb_arg2 = NULL;
	} else {
		printk("!&!&!&!& ERROR: Unknown callback name %s\n", cbname);
		return -1;
	}
	return 0;
}
EXPORT_SYMBOL(unregister_simple_config_callback);

/* not mux the RST&WPS Button */
#ifndef CONFIG_MUX_RESET_WPS_BUTTON
/*
 * Irq for front panel SW wps start switch
 * Connected to XSCALE through GPIO4
 */
irqreturn_t wpsStart_irq(int cpl, void *dev_id)
{
	if (ignore_pushbutton) {
		ath_gpio_config_int(WPS_BUTTON_GPIO, INT_TYPE_LEVEL,
					INT_POL_ACTIVE_HIGH);
		ignore_pushbutton = 0;
		return IRQ_HANDLED;
	}

    ath_gpio_config_int (WPS_BUTTON_GPIO, INT_TYPE_LEVEL, INT_POL_ACTIVE_LOW);
		ignore_pushbutton = 1;

    printk ("Wps start button pressed.\n");

	/* dual band */
	if (sccallback[0].registered_cb)
	{
		sccallback[0].registered_cb (cpl, sccallback[0].cb_arg1, NULL, sccallback[0].cb_arg2);
	}
	if (sccallback[1].registered_cb)
	{
			sccallback[1].registered_cb (cpl, sccallback[1].cb_arg1, NULL, sccallback[1].cb_arg2);
	}
	
    return IRQ_HANDLED;
}
#endif

static int ignore_rstbutton = 1;

/* irq handler for reset button */
static irqreturn_t rst_irq(int cpl, void *dev_id)
{
    if (ignore_rstbutton)
	{
        ath_gpio_config_int(RST_DFT_GPIO, INT_TYPE_LEVEL, INT_POL_ACTIVE_HIGH);
        ignore_rstbutton = 0;

		mod_timer(&rst_timer, jiffies + RST_HOLD_TIME * HZ);

        return IRQ_HANDLED;
	}

    ath_gpio_config_int (RST_DFT_GPIO, INT_TYPE_LEVEL, INT_POL_ACTIVE_LOW);
    ignore_rstbutton = 1;

	printk("Reset button pressed.\n");

	/* mux the RST&WPS Button */
#ifdef CONFIG_MUX_RESET_WPS_BUTTON	
	/* mark reset status, by zjg, 12Apr10 */
	if (!bBlockWps && l_bMultiUseResetButton && l_bWaitForQss) 
	{
		if (sccallback[0].registered_cb)
		{
			sccallback[0].registered_cb (cpl, sccallback[0].cb_arg1, NULL, sccallback[0].cb_arg2);
		}
		if (sccallback[1].registered_cb)
		{
			sccallback[1].registered_cb (cpl, sccallback[1].cb_arg1, NULL, sccallback[1].cb_arg2);
		}	
	}
#endif

	return IRQ_HANDLED;
}

static void rst_button_work(struct work_struct *work)
{
	struct tp_nl_gpio_msg msg;

	memset(&msg, 0, sizeof(struct tp_nl_gpio_msg));

	msg.gpio_status = counter;
	msg.gpio_port = RST_DFT_GPIO;
	msg.gpio_type = GPIO_RST_BUTTON;
	tp_nl_gpio_send(tp_gpio_nl_sk, &msg);
}

static DECLARE_WORK(gpio_rst_button_work, rst_button_work);

void check_rst(unsigned long nothing)
{
	if (!ignore_rstbutton)
	{
		printk ("restoring factory default...\n");
		counter++;
		schedule_work(&gpio_rst_button_work);

#ifdef CONFIG_MUX_RESET_WPS_BUTTON
		if (l_bMultiUseResetButton)
		{
			/* to mark reset status, forbid QSS, added by zjg, 12Apr10 */
			l_bWaitForQss	= 0;
		}
#endif
	}
}

#ifdef CONFIG_TX_PWR_BUTTON
static int ignore_pwrbutton = 1;
static int txpwrStatus = 0;
static txpwr_led_state_e gpio_txpwr_led = LED_PWR_OFF;

static void txpwr_button_work(struct work_struct *work)
{
	struct tp_nl_gpio_msg msg;

	memset(&msg, 0, sizeof(struct tp_nl_gpio_msg));

	msg.gpio_status = txpwrStatus;
	msg.gpio_port = WIFI_TX_PWR_GPIO;
	msg.gpio_type = GPIO_TXPWR_BUTTON;
	tp_nl_gpio_send(tp_gpio_nl_sk, &msg);
}

static DECLARE_WORK(gpio_txpwr_button_work, txpwr_button_work);

/* irq handler for reset button */
static irqreturn_t txpwr_irq(int cpl, void *dev_id)
{
	if (ignore_pwrbutton)
	{
		ath_gpio_config_int(WIFI_TX_PWR_GPIO, INT_TYPE_LEVEL, INT_POL_ACTIVE_HIGH);
		ignore_pwrbutton = 0;

		return IRQ_HANDLED;
	}

	ath_gpio_config_int (WIFI_TX_PWR_GPIO, INT_TYPE_LEVEL, INT_POL_ACTIVE_LOW);
	ignore_pwrbutton = 1;
	txpwrStatus = !txpwrStatus;
	schedule_work(&gpio_txpwr_button_work);

	return IRQ_HANDLED;
}

static int txpwr_button_read (char *page, char **start, off_t off,
								int count, int *eof, void *data)
{
    return sprintf (page, "%d\n", txpwrStatus);
}

static int txpwr_button_wirte (struct file *file, const char *buf,
								unsigned long count, void *data)
{
	u_int32_t val;

	if (sscanf(buf, "%d", &val) != 1)
		return -EINVAL;

	if ((val < 0) || (val > 1))
		return -EINVAL;

	txpwrStatus = val;

	return count;
}

static int gpio_txpwr_led_read (char *page, char **start, off_t off,
					int count, int *eof, void *data)
{
	return sprintf (page, "%d\n", gpio_txpwr_led);
}

static int gpio_txpwr_led_write (struct file *file, const char *buf,
					unsigned long count, void *data)
{
	u_int32_t val, led_onoff = 0;

	if (sscanf(buf, "%d", &val) != 1)
		return -EINVAL;

	if (val >= LED_PWR_MAX)
		return -EINVAL;

	if (val == gpio_txpwr_led)
		return count;

	switch (val) {
		case LED_PWR_OFF :
			led_onoff = TXPWR_OFF;
			break;

		case LED_PWR_ON:
			led_onoff = TXPWR_ON;
			break;
	}
	ath_gpio_out_val (TXPWR_LED_GPIO, led_onoff);
	gpio_txpwr_led = val;

	return count;
}

#endif


#ifdef CONFIG_ENABLE_WIFI_BUTTON
int wifiButtonStatus = 0;
static int ignore_wifibutton = 1;

static void wifi_button_work(struct work_struct *work)
{
	struct tp_nl_gpio_msg msg;

	memset(&msg, 0, sizeof(struct tp_nl_gpio_msg));

	msg.gpio_status = wifiButtonStatus;
	msg.gpio_port = WIFI_RADIO_SW_GPIO;
	msg.gpio_type = GPIO_WIFI_BUTTON;
	tp_nl_gpio_send(tp_gpio_nl_sk, &msg);
}

static DECLARE_WORK(gpio_wifi_button_work, wifi_button_work);

/* irq handler for wifi switch */
static irqreturn_t wifi_sw_irq(int cpl, void *dev_id)
{
    if (ignore_wifibutton)
	{
		ath_gpio_config_int (WIFI_RADIO_SW_GPIO, INT_TYPE_LEVEL, INT_POL_ACTIVE_HIGH);
		ignore_wifibutton = 0;
		
		return IRQ_HANDLED;
	}

	ath_gpio_config_int (WIFI_RADIO_SW_GPIO, INT_TYPE_LEVEL, INT_POL_ACTIVE_LOW);
	ignore_wifibutton = 1;
	wifiButtonStatus = !wifiButtonStatus; /*close wifi*/
	schedule_work(&gpio_wifi_button_work);

	return IRQ_HANDLED;
}

static int wifi_button_read (char *page, char **start, off_t off,
                               int count, int *eof, void *data)
{
    return sprintf (page, "%d\n", wifiButtonStatus);
}

static int wifi_button_wirte (struct file *file, const char *buf,
                                        unsigned long count, void *data)
{
    u_int32_t val;

	if (sscanf(buf, "%d", &val) != 1)
        return -EINVAL;

	if ((val < 0) || (val > 1))
		return -EINVAL;

	wifiButtonStatus = val;

	return count;
}

#endif


#ifdef CONFIG_MUX_RESET_WPS_BUTTON
static int multi_use_reset_button_read (char *page, char **start, off_t off,
                               int count, int *eof, void *data)
{
    return sprintf (page, "%d\n", l_bMultiUseResetButton);
}

static int multi_use_reset_button_write (struct file *file, const char *buf,
                                        unsigned long count, void *data)
{
    u_int32_t val;

	if (sscanf(buf, "%d", &val) != 1)
        return -EINVAL;

	/* only admit "0" or "1" */
	if ((val < 0) || (val > 1))
		return -EINVAL;	

	l_bMultiUseResetButton = val;
	
	return count;

}
#endif

static int push_button_read (char *page, char **start, off_t off,
                               int count, int *eof, void *data)
{
    return 0;
}

static int push_button_write (struct file *file, const char *buf,
                                        unsigned long count, void *data)
{
    if (sccallback[0].registered_cb) {
        sccallback[0].registered_cb (0, sccallback[0].cb_arg1, 0, sccallback[0].cb_arg2);
    }
    if (sccallback[1].registered_cb) {
        sccallback[1].registered_cb (0, sccallback[1].cb_arg1, 0, sccallback[1].cb_arg2);
    }
    return count;
}

static int usb_led_blink_read (char *page, char **start, off_t off,
                               int count, int *eof, void *data)
{
    return sprintf (page, "%d\n", g_usbLedBlinkCountDown);
}

static int usb_led_blink_write (struct file *file, const char *buf,
                                        unsigned long count, void *data)
{
    u_int32_t val;

	if (sscanf(buf, "%d", &val) != 1)
        return -EINVAL;

	if ((val < 0) || (val > 1))
		return -EINVAL;

	g_usbLedBlinkCountDown = val;

	return count;
}




/* for usb 3G by lyj, 31Aug11 */
#ifndef CONFIG_NO_USB
/*added by ZQQ,10.06.02*/
static int usb_power_read (char *page, char **start, off_t off,
				int count, int *eof, void *data)
{
	return 0;
}

static int usb_power_write (struct file *file, const char *buf,
				unsigned long count, void *data)
{
    u_int32_t val = 0;

	if (sscanf(buf, "%d", &val) != 1)
        return -EINVAL;

	if ((val < 0) || (val > 1))
		return -EINVAL;

	printk("%s %d: write gpio:value = %d\r\n",__FUNCTION__,__LINE__,val);

	#ifdef USB_POWER_SW_GPIO
	if (USB_POWER_ON == val)
	{
		ath_gpio_out_val(USB_POWER_SW_GPIO, USB_POWER_ON);
	}
	else
	{
		ath_gpio_out_val(USB_POWER_SW_GPIO, USB_POWER_OFF);
	}
	#endif
	
	return count;
}
#endif

static led_state_e gpio_tricolorled = LED_STATE_OFF;

static int gpio_tricolor_led_read (char *page, char **start, off_t off,
					int count, int *eof, void *data)
{
    return sprintf (page, "%d\n", gpio_tricolorled);
}

static int gpio_tricolor_led_write (struct file *file, const char *buf,
					unsigned long count, void *data)
{
    u_int32_t val, green_led_onoff = 0, yellow_led_onoff = 0;

	if (sscanf(buf, "%d", &val) != 1)
		return -EINVAL;

    if (val >= LED_STATE_MAX)
        return -EINVAL;

    if (val == gpio_tricolorled)
		return count;

    switch (val) {
        case LED_STATE_OFF :
                green_led_onoff = OFF;   /* both LEDs OFF */
                yellow_led_onoff = OFF;
                break;

        case LED_STATE_GREEN:
                green_led_onoff = ON;    /* green ON, Yellow OFF */
                yellow_led_onoff = OFF;
                break;

        case LED_STATE_YELLOW:
                green_led_onoff = OFF;   /* green OFF, Yellow ON */
                yellow_led_onoff = ON;
                break;
    
        case LED_STATE_ORANGE:
                green_led_onoff = ON;    /* both LEDs ON */
                yellow_led_onoff = ON;
                break;
	}

    ath_gpio_out_val (TRICOLOR_LED_GREEN_PIN, green_led_onoff);
    gpio_tricolorled = val;

    return count;
}


static int create_simple_config_led_proc_entry(void)
{
	if (simple_config_entry != NULL) {
		printk("Already have a proc entry for /proc/simple_config!\n");
		return -ENOENT;
	}

	simple_config_entry = proc_mkdir("simple_config", NULL);
	if (!simple_config_entry)
		return -ENOENT;

	simulate_push_button_entry = create_proc_entry("push_button", 0644,
							simple_config_entry);
	if (!simulate_push_button_entry)
		return -ENOENT;

	simulate_push_button_entry->write_proc = push_button_write;
	simulate_push_button_entry->read_proc = push_button_read;

#ifdef CONFIG_MUX_RESET_WPS_BUTTON
	/* added by zjg, 12Apr10 */
	multi_use_reset_button_entry = create_proc_entry ("multi_use_reset_button", 0644,
                                                      simple_config_entry);
    if (!multi_use_reset_button_entry)
        return -ENOENT;

    multi_use_reset_button_entry->write_proc	= multi_use_reset_button_write;
    multi_use_reset_button_entry->read_proc 	= multi_use_reset_button_read;
	/* end added */
#endif

    tricolor_led_entry = create_proc_entry ("tricolor_led", 0644,
							simple_config_entry);
    if (!tricolor_led_entry)
		return -ENOENT;

    tricolor_led_entry->write_proc = gpio_tricolor_led_write;
    tricolor_led_entry->read_proc = gpio_tricolor_led_read;

	/* for usb led blink */
	usb_led_blink_entry = create_proc_entry ("usb_blink", 0666,
                                                      simple_config_entry);
	if (!usb_led_blink_entry)
		return -ENOENT;
	
    usb_led_blink_entry->write_proc = usb_led_blink_write;
    usb_led_blink_entry->read_proc = usb_led_blink_read;

#ifdef CONFIG_ENABLE_WIFI_BUTTON
	wifi_button_entry = create_proc_entry ("wifi_button", 0644,
							simple_config_entry);
	if(!wifi_button_entry)
		return -ENOENT;

	wifi_button_entry->write_proc = wifi_button_wirte;
	wifi_button_entry->read_proc = wifi_button_read;
#endif

#ifdef CONFIG_TX_PWR_BUTTON
	txpwr_button_entry = create_proc_entry ("txpwr_button", 0644,
							simple_config_entry);
	if(!txpwr_button_entry)
		return -ENOENT;

	txpwr_button_entry->write_proc = txpwr_button_wirte;
	txpwr_button_entry->read_proc = txpwr_button_read;

	txpwr_led_entry = create_proc_entry ("txpwr_led", 0644,
							simple_config_entry);
	if (!txpwr_led_entry)
		return -ENOENT;

	txpwr_led_entry->write_proc = gpio_txpwr_led_write;
	txpwr_led_entry->read_proc = gpio_txpwr_led_read;
#endif

	/* if no usb, no need to init usb power proc entry */
#ifndef CONFIG_NO_USB	
	/*added by ZQQ, 10.06.02 for usb power*/
	usb_power_entry = create_proc_entry("usb_power", 0666, simple_config_entry);
	if(!usb_power_entry)
		return -ENOENT;

	usb_power_entry->write_proc = usb_power_write;
	usb_power_entry->read_proc = usb_power_read;	
	/*end added*/
#endif

	/* configure gpio as outputs */
    ath_gpio_config_output (TRICOLOR_LED_GREEN_PIN); 
    //ar7240_gpio_config_output (TRICOLOR_LED_YELLOW_PIN); 

	/* switch off the led */
	/* TRICOLOR_LED_GREEN_PIN is poll up, so ON is OFF modified by tiger 09/07/15 */
    ath_gpio_out_val(TRICOLOR_LED_GREEN_PIN, ON);
    //ar7240_gpio_out_val(TRICOLOR_LED_YELLOW_PIN, OFF);

#ifdef CONFIG_TX_PWR_BUTTON
	ath_gpio_config_output (TXPWR_LED_GPIO); 

	ath_gpio_out_val(TXPWR_LED_GPIO, TXPWR_ON);
#endif
	return 0;
}



/******* begin ioctl stuff **********/
#ifdef CONFIG_GPIO_DEBUG
void print_gpio_regs(char* prefix)
{
	printk("\n-------------------------%s---------------------------\n", prefix);
	printk("AR7240_GPIO_OE:%#X\n", ar7240_reg_rd(AR7240_GPIO_OE));
	printk("AR7240_GPIO_IN:%#X\n", ar7240_reg_rd(AR7240_GPIO_IN));
	printk("AR7240_GPIO_OUT:%#X\n", ar7240_reg_rd(AR7240_GPIO_OUT));
	printk("AR7240_GPIO_SET:%#X\n", ar7240_reg_rd(AR7240_GPIO_SET));
	printk("AR7240_GPIO_CLEAR:%#X\n", ar7240_reg_rd(AR7240_GPIO_CLEAR));
	printk("AR7240_GPIO_INT_ENABLE:%#X\n", ar7240_reg_rd(AR7240_GPIO_INT_ENABLE));
	printk("AR7240_GPIO_INT_TYPE:%#X\n", ar7240_reg_rd(AR7240_GPIO_INT_TYPE));
	printk("AR7240_GPIO_INT_POLARITY:%#X\n", ar7240_reg_rd(AR7240_GPIO_INT_POLARITY));
	printk("AR7240_GPIO_INT_PENDING:%#X\n", ar7240_reg_rd(AR7240_GPIO_INT_PENDING));
	printk("AR7240_GPIO_INT_MASK:%#X\n", ar7240_reg_rd(AR7240_GPIO_INT_MASK));
	printk("\n-------------------------------------------------------\n");
}
#endif

/* ioctl for reset default detection and system led switch*/
int ar7240_gpio_ioctl(struct inode *inode, struct file *file,  unsigned int cmd, unsigned long arg)
{
	int i;
	int* argp = (int *)arg;

	if (_IOC_TYPE(cmd) != AR7240_GPIO_MAGIC ||
		_IOC_NR(cmd) < AR7240_GPIO_IOCTL_BASE ||
		_IOC_NR(cmd) > AR7240_GPIO_IOCTL_MAX)
	{
		printk("type:%d nr:%d\n", _IOC_TYPE(cmd), _IOC_NR(cmd));
		printk("ar7240_gpio_ioctl:unknown command\n");
		return -1;
	}

	switch (cmd)
	{
	case AR7240_GPIO_BTN_READ:
		*argp = counter;
		counter = 0;
		break;
			
	case AR7240_GPIO_LED_READ:
		for (i = 0; i < ATH_GPIO_COUNT; i ++)
		{
			printk("pin%d: %d\n", i, ath_gpio_in_val(i));
		}

	#ifdef CONFIG_GPIO_DEBUG
		print_gpio_regs("");
	#endif
			
		break;
			
	case AR7240_GPIO_LED_WRITE:
		if (unlikely(bBlockWps))
			bBlockWps = 0;
		ath_gpio_out_val(SYS_LED_GPIO, *argp);	/* PB92 use gpio 1 to config switch */
		break;

	case AR7240_GPIO_USB_LED1_WRITE:
			#ifdef AP_USB_LED_GPIO
			ath_gpio_out_val(AP_USB_LED_GPIO, *argp);
			#endif
			break;
			
	case AR7240_GPIO_USB_1_LED1_WRITE:
			#ifdef AP_USB_1_LED_GPIO
			ath_gpio_out_val(AP_USB_1_LED_GPIO, *argp);
			#endif
			break;
			
	case AR7240_GPIO_USB_POWER_WRITE:
			#ifdef USB_POWER_SW_GPIO
			ath_gpio_out_val(USB_POWER_SW_GPIO, *argp);
			#endif
			break;

	case AR7240_GPIO_USB_1_POWER_WRITE:
			#ifdef USB_1_POWER_SW_GPIO
			ath_gpio_out_val(USB_1_POWER_SW_GPIO, *argp);
			#endif
			break;
			
	default:
		printk("command not supported\n");
		return -1;
	}


	return 0;
}


int ar7240_gpio_open (struct inode *inode, struct file *filp)
{
	int minor = iminor(inode);
	int devnum = minor; //>> 1;
	struct mtd_info *mtd;

	if ((filp->f_mode & 2) && (minor & 1))
{
		printk("You can't open the RO devices RW!\n");
		return -EACCES;
}

	mtd = get_mtd_device(NULL, devnum);   
	if (!mtd)
{
		printk("Can not open mtd!\n");
		return -ENODEV;	
	}
	filp->private_data = mtd;
	return 0;

}

/* struct for cdev */
struct file_operations gpio_device_op =
{
	.owner = THIS_MODULE,
	.ioctl = ar7240_gpio_ioctl,
	.open = ar7240_gpio_open,
};

/* struct for ioctl */
static struct cdev gpio_device_cdev =
{
	.owner  = THIS_MODULE,
	.ops	= &gpio_device_op,
};
/******* end  ioctl stuff **********/

int __init ar7240_simple_config_init(void)
{
    int req;

	/* restore factory default and system led */
	dev_t dev;
    int rt;
	int current_wifi_value;
    int ar7240_gpio_major = gpio_major;
    int ar7240_gpio_minor = gpio_minor;

	/* init the tp gpio netlink */
	tp_gpio_nl_sk = init_tp_gpio_netlink();

	if (NULL == tp_gpio_nl_sk)
	{
		printk("%s: netlink create failed\n", __func__);
	}
	
	init_timer(&rst_timer);
	rst_timer.function = check_rst;

	/* config gpio 11, 12, 14, 15, 16, 17, 18, 19 as normal gpio function */
	/* gpio11 */
	ath_reg_rmw_clear(ATH_GPIO_OUT_FUNCTION2, 0xff<<24);
	/* gpio12 */
	ath_reg_rmw_clear(ATH_GPIO_OUT_FUNCTION3, 0xff<<0);
	/* gpio14 */
	ath_reg_rmw_clear(ATH_GPIO_OUT_FUNCTION3, 0xff<<16);
	/* gpio15 */
	ath_reg_rmw_clear(ATH_GPIO_OUT_FUNCTION3, 0xff<<24);
	/* gpio16 */
	ath_reg_rmw_clear(ATH_GPIO_OUT_FUNCTION4, 0xff<<0);
	/* gpio17 */
	ath_reg_rmw_clear(ATH_GPIO_OUT_FUNCTION4, 0xff<<8);
	/* gpio18 */
	ath_reg_rmw_clear(ATH_GPIO_OUT_FUNCTION4, 0xff<<16);
	/* gpio19 */
	ath_reg_rmw_clear(ATH_GPIO_OUT_FUNCTION4, 0xff<<24);
	/* gpio21 */
	ath_reg_rmw_clear(ATH_GPIO_OUT_FUNCTION5, 0xff<<8);
	/* gpio22 */
	ath_reg_rmw_clear(ATH_GPIO_OUT_FUNCTION5, 0xff<<16);

	/* 2.4 G wifi LED */
	ath_gpio_config_output(12);
	ath_gpio_out_val(12, 1);

	/* 5G wifi LED */
	ath_gpio_config_output(17);
	ath_gpio_out_val(17, 1);
	
#ifndef CONFIG_MUX_RESET_WPS_BUTTON
	/* This is NECESSARY, lsz 090109 */
	ath_gpio_config_input(WPS_BUTTON_GPIO);

    /* configure JUMPSTART_GPIO as level triggered interrupt */
    ath_gpio_config_int (WPS_BUTTON_GPIO, INT_TYPE_LEVEL, INT_POL_ACTIVE_LOW);

    req = request_irq (ATH_GPIO_IRQn(WPS_BUTTON_GPIO), wpsStart_irq, 0,
                       "SW_WPSSTART", NULL);
    if (req != 0)
	{
        printk (KERN_ERR "unable to request IRQ for SWWPSSTART GPIO (error %d)\n", req);
    }
#endif

#ifdef CONFIG_TX_PWR_BUTTON
	ath_gpio_config_input(WIFI_TX_PWR_GPIO);

    ath_gpio_config_int (WIFI_TX_PWR_GPIO, INT_TYPE_LEVEL, INT_POL_ACTIVE_LOW);

    req = request_irq (ATH_GPIO_IRQn(WIFI_TX_PWR_GPIO), txpwr_irq, 0,
                       "WIFI_TXPWR", NULL);
	if (req != 0)
	{
		printk (KERN_ERR "unable to request IRQ for WIFI_TXPWR GPIO (error %d)\n", req);
	}
#endif

    create_simple_config_led_proc_entry ();

	ath_gpio_config_input(RST_DFT_GPIO);

	/* configure GPIO RST_DFT_GPIO as level triggered interrupt */
    ath_gpio_config_int (RST_DFT_GPIO, INT_TYPE_LEVEL, INT_POL_ACTIVE_LOW);

    rt = request_irq (ATH_GPIO_IRQn(RST_DFT_GPIO), rst_irq, 0,
                       "RESTORE_FACTORY_DEFAULT", NULL);
    if (rt != 0)
	{
        printk (KERN_ERR "unable to request IRQ for RESTORE_FACTORY_DEFAULT GPIO (error %d)\n", rt);
    }

#ifdef CONFIG_ENABLE_WIFI_BUTTON
	/* wifi switch! */
	ath_gpio_config_input(WIFI_RADIO_SW_GPIO);
	
	ignore_wifibutton = 0;
	ath_gpio_config_int (WIFI_RADIO_SW_GPIO, INT_TYPE_LEVEL, INT_POL_ACTIVE_LOW);


    req = request_irq (ATH_GPIO_IRQn(WIFI_RADIO_SW_GPIO), wifi_sw_irq, 0,
                       "WIFI_RADIO_SWITCH", NULL);
	if (req != 0)
	{
		printk (KERN_ERR "unable to request IRQ for WIFI_RADIO_SWITCH GPIO (error %d)\n", req);
	}
#endif
    if (ar7240_gpio_major)
	{
        dev = MKDEV(ar7240_gpio_major, ar7240_gpio_minor);
        rt = register_chrdev_region(dev, 1, "ar7240_gpio_chrdev");
    }
	else
	{
        rt = alloc_chrdev_region(&dev, ar7240_gpio_minor, 1, "ar7240_gpio_chrdev");
        ar7240_gpio_major = MAJOR(dev);
    }

    if (rt < 0)
	{
        printk(KERN_WARNING "ar7240_gpio_chrdev : can`t get major %d\n", ar7240_gpio_major);
        return rt;
	}

    cdev_init (&gpio_device_cdev, &gpio_device_op);
    rt = cdev_add(&gpio_device_cdev, dev, 1);
	
    if (rt < 0) 
		printk(KERN_NOTICE "Error %d adding ar7240_gpio_chrdev ", rt);

	#ifdef AP_USB_LED_GPIO
	ath_gpio_config_output(AP_USB_LED_GPIO);
	#endif
	#ifdef AP_USB_1_LED_GPIO
	ath_gpio_config_output(AP_USB_1_LED_GPIO);
	#endif
	ath_gpio_config_output(SYS_LED_GPIO);
	
	/* for USB 3G by lyj, 31Aug11 */
	#ifdef USB_POWER_SW_GPIO
	ath_gpio_config_output(USB_POWER_SW_GPIO);
	#endif
	#ifdef USB_1_POWER_SW_GPIO
	ath_gpio_config_output(USB_1_POWER_SW_GPIO);
	#endif
	
	ath_gpio_out_val(SYS_LED_GPIO, SYS_LED_OFF);
	#ifdef AP_USB_LED_GPIO
	ath_gpio_out_val(AP_USB_LED_GPIO, USB_LED_OFF);
	#endif
	#ifdef AP_USB_1_LED_GPIO
	ath_gpio_out_val(AP_USB_1_LED_GPIO, USB_1_LED_OFF);
	#endif
	ath_gpio_out_val (TRICOLOR_LED_GREEN_PIN, OFF);

	#ifdef CONFIG_TX_PWR_BUTTON
	ath_gpio_out_val(TXPWR_LED_GPIO, TXPWR_OFF);
	#endif

	/* for USB 3G by lyj, 31Aug11 */
	#ifdef USB_POWER_SW_GPIO
	ath_gpio_out_val(USB_POWER_SW_GPIO, USB_POWER_ON);
	#endif
	#ifdef USB_1_POWER_SW_GPIO
	ath_gpio_out_val(USB_1_POWER_SW_GPIO, USB_1_POWER_ON);
	#endif

	return 0;
}

subsys_initcall(ar7240_simple_config_init);
