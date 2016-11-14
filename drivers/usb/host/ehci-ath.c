/*
 * EHCI Host Controller Driver
 *
 * Atheros USB Controller
 *
 */

#include "../gadget/ath_defs.h"

#ifdef CONFIG_USB_ATH_OTG_MODULE
#warning "OTG enabled in host"
#define CONFIG_USB_ATH_OTG
#endif

static inline void ath_usb_host_reset(u32 ath_host_to_reset)
{
	ath_usb_reg_rmw_set(ath_host_to_reset,
			ATH_RESET_USBSUS_OVRIDE |
			ATH_RESET_USB_PHY_ANALOG);
	mdelay(10);

	ath_usb_reg_wr(ath_host_to_reset,
			((ath_usb_reg_rd(ath_host_to_reset) & ~(ATH_USB_RESET_USB_HOST))
			 | ATH_RESET_USBSUS_OVRIDE));
	mdelay(10);

	ath_usb_reg_wr(ath_host_to_reset,
			((ath_usb_reg_rd(ath_host_to_reset) &
			  ~(ATH_USB_RESET_USB_PHY | ATH_RESET_USB_PHY_ANALOG)) |
			 ATH_RESET_USBSUS_OVRIDE));
	mdelay(10);

	return;
}



#ifdef	CONFIG_USB_ATH_OTG
static int ath_usb_start_hc(struct ehci_hcd *ehci, struct device *dev)
{
	printk("%s: %p, %p\n", __func__, ehci_to_hcd(ehci),
	       &ehci_to_hcd(ehci)->self);
	if (ehci->transceiver) {
		int status = otg_set_host(ehci->transceiver,
					  &ehci_to_hcd(ehci)->self);
		dev_info(dev, "init %s transceiver, status %d\n",
			 ehci->transceiver->label, status);
		if (status) {
			if (ehci->transceiver)
				put_device(ehci->transceiver->dev);
		}
		return status;
	} else {
		dev_err(dev, "can't find transceiver\n");
		return -ENODEV;
	}
}
#endif

#ifdef	CONFIG_USB_OTG
void start_hnp(struct ehci_hcd *ehci)
{
	unsigned long flags;
	otg_start_hnp(ehci->transceiver);

	local_irq_save(flags);
	ehci->transceiver->state = OTG_STATE_A_SUSPEND;
	local_irq_restore(flags);
}
#endif

static int ath_usb_ehci_init(struct usb_hcd *hcd)
{
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	int ret;

	ath_usb_debug_fn("__enter %s\n", __FUNCTION__);

	/*
	 * EHCI Capability Register offset at 0x100      - Info from ChipIdea
 	 * EHCI Operational Register offset at 0x140     - Info from ChipIdea
	 */
	ehci->caps = hcd->regs + 0x100;	/* Device/Host Capa Reg */
	ehci->regs = hcd->regs + 0x100 +	/* Device/Host Oper Reg */
			HC_LENGTH(readl(&ehci->caps->hc_capbase));

	/*Reading HC Structural Parameters */
	ehci->hcs_params = readl(&ehci->caps->hcs_params);
	ath_usb_debug_dev("HCS Params %x\n", ehci->hcs_params);
	ath_usb_debug_dev("HCC Params %x\n", readl(&ehci->caps->hcc_params));

	ret = ehci_init(hcd);
	if (ret) {
		return ret;
	}
	/*
	 * Informs USB Subsystem abt embedded TT.
	 * Sets to host mode
	 */
	hcd->has_tt = 1;
	ehci->sbrn = 0x20;
	ehci_reset(ehci);

	return ret;
}

static struct hc_driver ath_usb_ehci_driver = {
	.description = hcd_name,
	.product_desc = "ATH EHCI",
	.hcd_priv_size = sizeof(struct ehci_hcd),

	/*
	 * generic hardware linkage
	 */
#ifndef CONFIG_USB_ATH_OTG
	.irq = ehci_irq,
#endif
	.flags = HCD_MEMORY | HCD_USB2,
	/*
	 * basic lifecycle operations
	 */
	.reset = ath_usb_ehci_init,
	.start = ehci_run,
#ifdef CONFIG_PM
	.suspend = ehci_bus_suspend,
	.resume = ehci_bus_resume,
#endif
	.stop = ehci_stop,
	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue = ehci_urb_enqueue,
	.urb_dequeue = ehci_urb_dequeue,
	.endpoint_disable = ehci_endpoint_disable,
	/*
	 * scheduling support
	 */
	.get_frame_number = ehci_get_frame,
	/*
	 * root hub support
	 */
	.hub_status_data = ehci_hub_status_data,
	.hub_control = ehci_hub_control,
	.bus_suspend = ehci_bus_suspend,
	.bus_resume = ehci_bus_resume,

	.clear_tt_buffer_complete = ehci_clear_tt_buffer_complete,
};

#ifndef CONFIG_USB_ATH_OTG
int ath_usb_ehci_probe(struct hc_driver *driver,
			  struct usb_hcd **hcd_out,
			  struct platform_device *pdev)
{
	struct usb_hcd *hcd;
	int ret;
	struct ehci_hcd *ehci;

	ath_usb_debug_dev("No of Resources %d \n", pdev->num_resources);

	/* Verify the Host Mode of the Driver */
	if (pdev->resource[1].flags != IORESOURCE_IRQ) {
		printk("resource[1] is not IORESOURCE_IRQ");
		ret = -ENOMEM;
	}

	hcd = usb_create_hcd(driver, &pdev->dev, pdev->dev.bus->name);
	if (!hcd) {
		ret = -ENOMEM;
		goto err;
	}
	hcd->rsrc_start = pdev->resource[0].start;
	hcd->rsrc_len = pdev->resource[0].end - pdev->resource[0].start + 1;

	if (!request_mem_region(hcd->rsrc_start, hcd->rsrc_len,
				driver->description)) {
		dev_dbg(&pdev->dev, "controller already in use\n");
		ret = -EBUSY;
		goto err1;
	}

	hcd->regs = ioremap(hcd->rsrc_start, hcd->rsrc_len);
	if (hcd->regs == NULL) {
		dev_dbg(&pdev->dev, "error mapping memory \n");
		ret = -EFAULT;
		goto err2;
	}

	/*
	 * EHCI Capability Register offset at 0x100      - Info from ChipIdea
	 * EHCI Operational Register offset at 0x140     - Info from ChipIdea
	 */
	ehci = hcd_to_ehci(hcd);
	ehci->caps = hcd->regs + 0x100;	/* Device/Host Capa Reg */
	ehci->regs = hcd->regs + 0x140;	/* Device/Host Oper Reg */

	ath_usb_debug_dev("hcd->regs %p \n", hcd->regs);
	ath_usb_debug_dev("Host Capability Reg %p \n", ehci->caps);
	ath_usb_debug_dev("Host Operational Reg %p \n", ehci->regs);

	/* Added 5_29_07 */
#ifdef CONFIG_MACH_AR933x
	ath_usb_reg_rmw_set(ATH_USB_RESET, ATH_RESET_USBSUS_OVRIDE);
	mdelay(10);

	ath_usb_reg_wr(ATH_USB_RESET,
		      ((ath_usb_reg_rd(ATH_USB_RESET) & ~(ATH_USB_RESET_USB_HOST))
		       | ATH_RESET_USBSUS_OVRIDE));
	mdelay(10);

	ath_usb_reg_wr(ATH_USB_RESET,
		      ((ath_usb_reg_rd(ATH_USB_RESET) & ~(ATH_USB_RESET_USB_PHY)) |
		       ATH_RESET_USBSUS_OVRIDE));
	mdelay(10);
#elif defined(CONFIG_MACH_AR934x)
	if (is_ar934x_13_or_later()) {
		ath_reg_rmw_clear(PHY_CTRL1_ADDRESS, (PHY_CTRL1_USE_PLL_LOCKDETECT_MASK | 
					PHY_CTRL1_RX_LOWR_PDET_MASK));
	}
	/* USB Soft Reset Sequence - Power Down the USB PHY PLL */
	ath_usb_reg_rmw_set(ATH_USB_RESET,
					ATH_RESET_USBSUS_OVRIDE | ATH_RESET_USB_PHY_ANALOG |
					ATH_RESET_USB_PHY_PLL_PWD_EXT);
	mdelay(10);

	/* USB Soft Reset Sequence - Power Up the USB PHY PLL */
	ath_usb_reg_wr(ATH_USB_RESET,(ath_usb_reg_rd(ATH_USB_RESET) & ~(ATH_RESET_USB_PHY_PLL_PWD_EXT)));
	mdelay(10);

	ath_usb_reg_wr(ATH_USB_RESET,
					((ath_usb_reg_rd(ATH_USB_RESET) & ~(ATH_USB_RESET_USB_HOST))
					 | ATH_RESET_USBSUS_OVRIDE));
	mdelay(10);

	ath_usb_reg_wr(ATH_USB_RESET,
					((ath_usb_reg_rd(ATH_USB_RESET) &
					  ~(ATH_USB_RESET_USB_PHY | ATH_RESET_USB_PHY_ANALOG)) |
					 ATH_RESET_USBSUS_OVRIDE));
	mdelay(10);

#elif defined(CONFIG_MACH_QCA955x)
	//
	// This WAR is not needed for Scorpion (now). Will uncomment
	// this if necessary
	//
	//if(pdev->id == ATH_USB_HOST0) {
	//	ath_reg_rmw_clear(USB_PHY_CTRL1_ADDRESS, (USB_PHY_CTRL1_USE_PLL_LOCKDETECT_MASK |
	//				USB_PHY_CTRL1_RX_LOWR_PDET_MASK));
	//	ath_usb_host_reset(ATH_USB_RESET);
	//} else if(pdev->id == ATH_USB_HOST1) {
	//	ath_reg_rmw_clear(USB2_PHY_CTRL1_ADDRESS, (USB2_PHY_CTRL1_USE_PLL_LOCKDETECT_MASK |
	//				USB2_PHY_CTRL1_RX_LOWR_PDET_MASK));
	//	ath_usb_host_reset(ATH_USB2_RESET);
	//} else {
	//	dev_dbg(&pdev->id, "error in host selection\n");
	//}
#else
	#error :Host selection need to be defined.
#endif

	/*
	 * Reset host controller
	 */

	printk("Port Status %x \n", readl(&ehci->regs->port_status[0]));
	ret = usb_add_hcd(hcd, pdev->resource[1].start, IRQF_SHARED);

	if (ret != 0) {
		goto err3;
	}
	return ret;

err3:
	iounmap(hcd->regs);
err2:
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
err1:
	usb_put_hcd(hcd);
err:
	dev_err(&pdev->dev, "init %s fail, %d \n", pdev->dev.bus->name, ret);
	return ret;
}

static int ath_usb_ehci_drv_probe(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	if (usb_disabled()) {
		ath_usb_error("USB_DISABLED \n");
		return -ENODEV;
	}
	return ath_usb_ehci_probe(&ath_usb_ehci_driver, &hcd, pdev);
}

static int ath_usb_ehci_drv_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	usb_remove_hcd(hcd);
	iounmap(hcd->regs);
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
	usb_put_hcd(hcd);
	return 0;
}

static struct platform_driver ath_usb_ehci_hcd_driver = {
	.probe = ath_usb_ehci_drv_probe,
	.remove = ath_usb_ehci_drv_remove,
	.driver = {
		   .name = "ath-ehci",
	},
};

#ifdef CONFIG_MACH_QCA955x
static struct platform_driver ath_usb_ehci_hcd_driver_1 = {
	.probe = ath_usb_ehci_drv_probe,
	.remove = ath_usb_ehci_drv_remove,
	.driver = {
		   .name = "ath-ehci1",
		   .mod_name = "ath-ehci-host2",
	},
};
#endif /* CONFIG_MACH_QCA955x */
#else

int usb_otg_ath_usb_probe(struct hc_driver *driver)
{
	struct ath_usb_otg *ath_usb_otg;
	struct usb_hcd *hcd;
	struct ehci_hcd *ehci;
	struct device *dev;
	int ret;

	printk(" %s \n", __func__);
	ath_usb_otg = ath_usb_get_otg();
	if (ath_usb_otg == NULL) {
		return -EINVAL;
	}
	dev = ath_usb_otg->dev;

	hcd = usb_create_hcd(driver, dev, dev->bus->name);
	if (!hcd) {
		ret = -ENOMEM;
		goto err;
	}
	hcd->rsrc_start = 0;
	hcd->rsrc_len = 0;

	hcd->regs = ath_usb_otg->reg_base;
	if (hcd->regs == NULL) {
		dev_dbg(dev, "error mapping memory \n");
		ret = -EFAULT;
		goto err1;
	}

	/* 
	 * EHCI Capability Register offset at 0x100      - Info from ChipIdea
	 * EHCI Operational Register offset at 0x140     - Info from ChipIdea
	 */
	ehci = hcd_to_ehci(hcd);
	ehci->caps = hcd->regs + 0x100;	/* Device/Host Capa Reg */
	ehci->regs = hcd->regs + 0x140;	/* Device/Host Oper Reg */

	ath_usb_otg->ehci = ehci;	/* Temp To Test HNP */

	printk("hcd->regs %p, %p \n", hcd, hcd->regs);
	printk("Host Capability Reg %p \n", ehci->caps);
	printk("Host Operational Reg %p \n", ehci->regs);

	ehci->transceiver = &ath_usb_otg->otg;

	printk("usb_add_hcd\n");
	ret = usb_add_hcd(hcd, 0, 0);
	if (ret != 0) {
		goto err1;
	}
	dev_set_drvdata(dev, hcd);

	ehci_hc_ath_usb_driver.irq = ehci_irq;
	ret = ath_usb_start_hc(ehci, dev);
	if (ret != 0) {
		goto err1;
	}

	return ret;

      err1:
	usb_put_hcd(hcd);
      err:
	dev_err(dev, "init %s fail, %d \n", dev->bus_id, ret);
	return ret;
}
#endif
