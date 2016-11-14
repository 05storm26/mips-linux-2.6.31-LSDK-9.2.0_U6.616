//#include <linux/config.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <atheros.h>

/*
 * PCI IRQ map
 */
int __init
pcibios_map_irq(const struct pci_dev *dev, uint8_t slot, uint8_t pin)
{
	extern struct pci_ops ath_pci_ops;

	pr_debug("fixing irq for slot %d pin %d\n", slot, pin);

	if (dev->bus->number >= 0 &&
	    dev->bus->number < ATH_MAX_PCI_BUS) {
		printk("%s: IRQ %d for bus %d\n", __func__,
			ATH_PCI_IRQ_BASE + dev->bus->number,
			dev->bus->number);
#ifndef CONFIG_PCI_INIT_IN_MONITOR

#define ATH_PCI_CMD_INIT	(PCI_COMMAND_MEMORY |		\
				 PCI_COMMAND_MASTER |		\
				 PCI_COMMAND_INVALIDATE |	\
				 PCI_COMMAND_PARITY |		\
				 PCI_COMMAND_SERR |		\
				 PCI_COMMAND_FAST_BACK)

		/*
		 * clear any lingering errors and register core error IRQ
		 */
		ath_pci_ops.write(dev->bus, 0,
				PCI_COMMAND, 4, ATH_PCI_CMD_INIT);
#endif
		return (ATH_PCI_IRQ_BASE + dev->bus->number);
	} else {
		printk("%s: Unknown bus no. %d!\n",
			__func__, dev->bus->number);
		return -1;
	}
}

int
pcibios_plat_dev_init(struct pci_dev *dev)
{
	return 0;
}
