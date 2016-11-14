/*
 * General Interrupt handling for AR7240 soc
 */
//#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/pm.h>
#include <linux/delay.h>
#include <linux/reboot.h>
#include <linux/kallsyms.h>

#include <asm/irq.h>
#include <asm/mipsregs.h>
//#include <asm/gdb-stub.h>

#include "ar7240.h"
#include <asm/irq_cpu.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <linux/swap.h>
#include <linux/proc_fs.h>
#include <linux/pfn.h>
#include <linux/threads.h>
#include <asm/asm-offsets.h>

/*
 * dummy irqaction, so that interrupt controller cascading can work. Basically
 * when one IC is connected to another, this will be used to enable to Parent
 * IC's irq line to which the child IC is connected
 */
static struct irqaction cascade = {
	.handler	= no_action,
	.name		= "cascade",
};

static void ar7240_dispatch_misc_intr(void);
#ifndef CONFIG_WASP_SUPPORT
static void ar7240_dispatch_pci_intr(void);
#endif
static void ar7240_dispatch_gpio_intr(void);
static void ar7240_misc_irq_init(int irq_base);
//extern asmlinkage void ar7240_interrupt_receive(void);
extern pgd_t swapper_pg_dir[_PTRS_PER_PGD];
extern unsigned long pgd_current[NR_CPUS];


void __init arch_init_irq(void)
{
	// set_except_vector(0, ar7240_interrupt_receive);

	/*
	 * initialize our interrupt controllers
	 */
	//mips_cpu_irq_init(AR7240_CPU_IRQ_BASE);
	mips_cpu_irq_init();
	ar7240_misc_irq_init(AR7240_MISC_IRQ_BASE);
	ar7240_gpio_irq_init(AR7240_GPIO_IRQ_BASE);

#ifdef CONFIG_PCI
	ar7240_pci_irq_init(AR7240_PCI_IRQ_BASE);
#endif

	/*
	 * enable cascades
	 */
	setup_irq(AR7240_CPU_IRQ_MISC,  &cascade);
	setup_irq(AR7240_MISC_IRQ_GPIO, &cascade);

#ifdef CONFIG_PCI
	setup_irq(AR7240_CPU_IRQ_PCI,   &cascade);
#endif

#if defined(CONFIG_WASP_SUPPORT) || defined(CONFIG_MACH_HORNET)
	set_irq_chip_and_handler(ATH_CPU_IRQ_WLAN,
				&dummy_irq_chip,
				handle_percpu_irq);
#endif

//#define ALLINTS (IE_IRQ0 | IE_IRQ1 | IE_IRQ2 | IE_IRQ3 | IE_IRQ4 | IE_IRQ5 | IE_SW0 | IE_SW1)
	//change_c0_status(ST0_IM, ALLINTS);
	set_c0_status(ST0_IM);
}


static void ar7240_misc_irq_enable(unsigned int);

static void
ar7240_dispatch_misc_intr()
{
	int pending;

	pending = ar7240_reg_rd(AR7240_MISC_INT_STATUS) &
		ar7240_reg_rd(AR7240_MISC_INT_MASK);

	if (pending & MIMR_UART) {
		do_IRQ(AR7240_MISC_IRQ_UART);
		ar7240_reg_rmw_clear(AR7240_MISC_INT_STATUS,MIMR_UART);
	}
	else if (pending & MIMR_DMA) {
		do_IRQ(AR7240_MISC_IRQ_DMA);
		ar7240_reg_rmw_clear(AR7240_MISC_INT_STATUS,MIMR_DMA);
	}
	else if (pending & MIMR_PERF_COUNTER) {
		do_IRQ(AR7240_MISC_IRQ_PERF_COUNTER);
		ar7240_reg_rmw_clear(AR7240_MISC_INT_STATUS,MIMR_PERF_COUNTER);
	}
	else if (pending & MIMR_TIMER) {
		do_IRQ(AR7240_MISC_IRQ_TIMER);
		ar7240_reg_rmw_clear(AR7240_MISC_INT_STATUS,MIMR_TIMER);
	}
	else if (pending & MIMR_OHCI_USB) {
		do_IRQ(AR7240_MISC_IRQ_USB_OHCI);
		ar7240_reg_rmw_clear(AR7240_MISC_INT_STATUS,MIMR_OHCI_USB);
	}
	else if (pending & MIMR_ERROR) {
		do_IRQ(AR7240_MISC_IRQ_ERROR);
		ar7240_reg_rmw_clear(AR7240_MISC_INT_STATUS,MIMR_ERROR);
	}
	else if (pending & MIMR_GPIO) {
		ar7240_dispatch_gpio_intr();
		ar7240_reg_rmw_clear(AR7240_MISC_INT_STATUS,MIMR_GPIO);
	}
	else if (pending & MIMR_WATCHDOG) {
		do_IRQ(AR7240_MISC_IRQ_WATCHDOG);
		ar7240_reg_rmw_clear(AR7240_MISC_INT_STATUS,MIMR_WATCHDOG);
	}
	else if (pending & MIMR_ENET_LINK) {
		do_IRQ(AR7240_MISC_IRQ_ENET_LINK);
		ar7240_reg_rmw_clear(AR7240_MISC_INT_STATUS,MIMR_ENET_LINK);
	}

}

static void
ar7240_dispatch_pci_intr(void)
{
#if 0
	int pending;
	pending = ar7240_reg_rd(AR7240_PCI_INT_STATUS) &
		ar7240_reg_rd(AR7240_PCI_INT_MASK);

	if (pending & PISR_DEV0)
		do_IRQ(AR7240_PCI_IRQ_DEV0, regs);

	else if (pending & PISR_DEV1)
		do_IRQ(AR7240_PCI_IRQ_DEV1, regs);

	else if (pending & PISR_DEV2)
		do_IRQ(AR7240_PCI_IRQ_DEV2, regs);
#else
	do_IRQ(AR7240_PCI_IRQ_DEV0);
#endif
}

static void
ar7240_dispatch_gpio_intr(void)
{
	int pending, i;

	pending = ar7240_reg_rd(AR7240_GPIO_INT_PENDING) &
		ar7240_reg_rd(AR7240_GPIO_INT_MASK);

	for(i = 0; i < AR7240_GPIO_IRQ_COUNT; i++) {
		if (pending & (1 << i))
			do_IRQ(AR7240_GPIO_IRQn(i));
	}
}

/*
 * Dispatch interrupts.
 * XXX: This currently does not prioritize except in calling order. Eventually
 * there should perhaps be a static map which defines, the IPs to be masked for
 * a given IP.
 */
asmlinkage void plat_irq_dispatch(void)
{
	int pending = read_c0_status() & read_c0_cause();
#if 0
	if (!(pending & CAUSEF_IP7))
		printk("%s: in irq dispatch \n", __func__);
#endif
	if (pending & CAUSEF_IP7)
		do_IRQ(AR7240_CPU_IRQ_TIMER);


	else if (pending & CAUSEF_IP2) {
		//printk("%s: 0x%x\n", __func__, ar7240_reg_rd(AR7240_PCIE_WMAC_INT_STATUS));
#ifdef CONFIG_WASP_SUPPORT
#ifdef CONFIG_PCI
		if (unlikely(ar7240_reg_rd
			(AR7240_PCIE_WMAC_INT_STATUS) & PCI_WMAC_INTR))
			ar7240_dispatch_pci_intr();
		else
#endif
			do_IRQ(ATH_CPU_IRQ_WLAN);
#elif defined (CONFIG_MACH_HORNET)
			do_IRQ(ATH_CPU_IRQ_WLAN);
#else
			ar7240_dispatch_pci_intr();
#endif
	} 
        else if (pending & CAUSEF_IP4)
		do_IRQ(AR7240_CPU_IRQ_GE0);

	else if (pending & CAUSEF_IP5)
		do_IRQ(AR7240_CPU_IRQ_GE1);

	else if (pending & CAUSEF_IP3)
		do_IRQ(AR7240_CPU_IRQ_USB);

	else if (pending & CAUSEF_IP6)
		ar7240_dispatch_misc_intr();

	/*
	 * Some PCI devices are write to clear. These writes are posted and might
	 * require a flush (r8169.c e.g.). Its unclear what will have more
	 * performance impact - flush after every interrupt or taking a few
	 * "spurious" interrupts. For now, its the latter.
	 */
	/*else
	printk("spurious IRQ pending: 0x%x\n", pending);*/
}

#if 1
#define vpk(...)
#define vps(...)
#else
#define vpk     printk
#define vps     print_symbol
#endif

static void
ar7240_misc_irq_enable(unsigned int irq)
{
#if 0
	vpk("%s: %u ", __func__, irq);
	vps("%s\n", __builtin_return_address(0));
#endif
	ar7240_reg_rmw_set(AR7240_MISC_INT_MASK,
		(1 << (irq - AR7240_MISC_IRQ_BASE)));
}

static void
ar7240_misc_irq_disable(unsigned int irq)
{
#if 0
	vpk("%s: %u ", __func__, irq);
	vps("%s\n", __builtin_return_address(0));
#endif
	ar7240_reg_rmw_clear(AR7240_MISC_INT_MASK,
		(1 << (irq - AR7240_MISC_IRQ_BASE)));
}
static unsigned int
ar7240_misc_irq_startup(unsigned int irq)
{
#if 0
	vpk("%s: %u ", __func__, irq);
	vps("%s\n", __builtin_return_address(0));
#endif
	ar7240_misc_irq_enable(irq);
	return 0;
}

static void
ar7240_misc_irq_shutdown(unsigned int irq)
{
#if 0
	vpk("%s: %u ", __func__, irq);
	vps("%s\n", __builtin_return_address(0));
#endif
	ar7240_misc_irq_disable(irq);
}

static void
ar7240_misc_irq_ack(unsigned int irq)
{
#if 0
	vpk("%s: %u ", __func__, irq);
	vps("%s\n", __builtin_return_address(0));
#endif
	ar7240_misc_irq_disable(irq);
}

static void
ar7240_misc_irq_end(unsigned int irq)
{
#if 0
	vpk("%s: %u ", __func__, irq);
	vps("%s\n", __builtin_return_address(0));
#endif
	if (!(irq_desc[irq].status & (IRQ_DISABLED | IRQ_INPROGRESS)))
		ar7240_misc_irq_enable(irq);
}

static int
ar7240_misc_irq_set_affinity(unsigned int irq, const struct cpumask *dest)
{
	/*
	 * Only 1 CPU; ignore affinity request
	 */
	return 0;
}

struct irq_chip /* hw_interrupt_type */ ar7240_misc_irq_controller = {
	.name		= "AR7240 MISC",
	.startup	= ar7240_misc_irq_startup,
	.shutdown	= ar7240_misc_irq_shutdown,
	.enable		= ar7240_misc_irq_enable,
	.disable	= ar7240_misc_irq_disable,
	.ack		= ar7240_misc_irq_ack,
	.end		= ar7240_misc_irq_end,
	.eoi		= ar7240_misc_irq_end,
	.set_affinity	= ar7240_misc_irq_set_affinity,
};

/*
 * Determine interrupt source among interrupts that use IP6
 */
static void
ar7240_misc_irq_init(int irq_base)
{
	int i;

	for (i = irq_base; i < irq_base + AR7240_MISC_IRQ_COUNT; i++) {
		irq_desc[i].status = IRQ_DISABLED;
		irq_desc[i].action = NULL;
		irq_desc[i].depth = 1;
		//irq_desc[i].chip = &ar7240_misc_irq_controller;
		set_irq_chip_and_handler(i, &ar7240_misc_irq_controller,
					 handle_percpu_irq);
	}
}
