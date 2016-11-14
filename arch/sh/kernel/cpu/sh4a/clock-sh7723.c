/*
 * arch/sh/kernel/cpu/sh4a/clock-sh7723.c
 *
 * SH7723 clock framework support
 *
 * Copyright (C) 2009 Magnus Damm
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <asm/clock.h>

/* SH7723 registers */
#define FRQCR		0xa4150000
#define VCLKCR		0xa4150004
#define SCLKACR		0xa4150008
#define SCLKBCR		0xa415000c
#define IRDACLKCR	0xa4150018
#define PLLCR		0xa4150024
#define MSTPCR0		0xa4150030
#define MSTPCR1		0xa4150034
#define MSTPCR2		0xa4150038
#define DLLFRQ		0xa4150050

/* Fixed 32 KHz root clock for RTC and Power Management purposes */
static struct clk r_clk = {
	.name           = "rclk",
	.id             = -1,
	.rate           = 32768,
};

/*
 * Default rate for the root input clock, reset this with clk_set_rate()
 * from the platform code.
 */
struct clk extal_clk = {
	.name		= "extal",
	.id		= -1,
	.rate		= 33333333,
};

/* The dll multiplies the 32khz r_clk, may be used instead of extal */
static unsigned long dll_recalc(struct clk *clk)
{
	unsigned long mult;

	if (__raw_readl(PLLCR) & 0x1000)
		mult = __raw_readl(DLLFRQ);
	else
		mult = 0;

	return clk->parent->rate * mult;
}

static struct clk_ops dll_clk_ops = {
	.recalc		= dll_recalc,
};

static struct clk dll_clk = {
	.name           = "dll_clk",
	.id             = -1,
	.ops		= &dll_clk_ops,
	.parent		= &r_clk,
	.flags		= CLK_ENABLE_ON_INIT,
};

static unsigned long pll_recalc(struct clk *clk)
{
	unsigned long mult = 1;
	unsigned long div = 1;

	if (__raw_readl(PLLCR) & 0x4000)
		mult = (((__raw_readl(FRQCR) >> 24) & 0x1f) + 1);
	else
		div = 2;

	return (clk->parent->rate * mult) / div;
}

static struct clk_ops pll_clk_ops = {
	.recalc		= pll_recalc,
};

static struct clk pll_clk = {
	.name		= "pll_clk",
	.id		= -1,
	.ops		= &pll_clk_ops,
	.flags		= CLK_ENABLE_ON_INIT,
};

struct clk *main_clks[] = {
	&r_clk,
	&extal_clk,
	&dll_clk,
	&pll_clk,
};

static int multipliers[] = { 1, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
static int divisors[] = { 1, 3, 2, 5, 3, 4, 5, 6, 8, 10, 12, 16, 20 };

static struct clk_div_mult_table div4_table = {
	.divisors = divisors,
	.nr_divisors = ARRAY_SIZE(divisors),
	.multipliers = multipliers,
	.nr_multipliers = ARRAY_SIZE(multipliers),
};

enum { DIV4_I, DIV4_U, DIV4_SH, DIV4_B, DIV4_B3, DIV4_P,
       DIV4_SIUA, DIV4_SIUB, DIV4_IRDA, DIV4_NR };

#define DIV4(_str, _reg, _bit, _mask, _flags) \
  SH_CLK_DIV4(_str, &pll_clk, _reg, _bit, _mask, _flags)

struct clk div4_clks[DIV4_NR] = {
	[DIV4_I] = DIV4("cpu_clk", FRQCR, 20, 0x0dbf, CLK_ENABLE_ON_INIT),
	[DIV4_U] = DIV4("umem_clk", FRQCR, 16, 0x0dbf, CLK_ENABLE_ON_INIT),
	[DIV4_SH] = DIV4("shyway_clk", FRQCR, 12, 0x0dbf, CLK_ENABLE_ON_INIT),
	[DIV4_B] = DIV4("bus_clk", FRQCR, 8, 0x0dbf, CLK_ENABLE_ON_INIT),
	[DIV4_B3] = DIV4("b3_clk", FRQCR, 4, 0x0db4, CLK_ENABLE_ON_INIT),
	[DIV4_P] = DIV4("peripheral_clk", FRQCR, 0, 0x0dbf, 0),
	[DIV4_SIUA] = DIV4("siua_clk", SCLKACR, 0, 0x0dbf, 0),
	[DIV4_SIUB] = DIV4("siub_clk", SCLKBCR, 0, 0x0dbf, 0),
	[DIV4_IRDA] = DIV4("irda_clk", IRDACLKCR, 0, 0x0dbf, 0),
};

struct clk div6_clks[] = {
	SH_CLK_DIV6("video_clk", &pll_clk, VCLKCR, 0),
};

#define MSTP(_str, _parent, _reg, _bit, _force_on, _need_cpg, _need_ram) \
  SH_CLK_MSTP32(_str, -1, _parent, _reg, _bit, _force_on * CLK_ENABLE_ON_INIT)

static struct clk mstp_clks[] = {
	/* See page 60 of Datasheet V1.0: Overview -> Block Diagram */
	MSTP("tlb0", &div4_clks[DIV4_I], MSTPCR0, 31, 1, 1, 0),
	MSTP("ic0", &div4_clks[DIV4_I], MSTPCR0, 30, 1, 1, 0),
	MSTP("oc0", &div4_clks[DIV4_I], MSTPCR0, 29, 1, 1, 0),
	MSTP("l2c0", &div4_clks[DIV4_SH], MSTPCR0, 28, 1, 1, 0),
	MSTP("ilmem0", &div4_clks[DIV4_I], MSTPCR0, 27, 1, 1, 0),
	MSTP("fpu0", &div4_clks[DIV4_I], MSTPCR0, 24, 1, 1, 0),
	MSTP("intc0", &div4_clks[DIV4_I], MSTPCR0, 22, 1, 1, 0),
	MSTP("dmac0", &div4_clks[DIV4_B], MSTPCR0, 21, 0, 1, 1),
	MSTP("sh0", &div4_clks[DIV4_SH], MSTPCR0, 20, 0, 1, 0),
	MSTP("hudi0", &div4_clks[DIV4_P], MSTPCR0, 19, 0, 1, 0),
	MSTP("ubc0", &div4_clks[DIV4_I], MSTPCR0, 17, 0, 1, 0),
	MSTP("tmu0", &div4_clks[DIV4_P], MSTPCR0, 15, 0, 1, 0),
	MSTP("cmt0", &r_clk, MSTPCR0, 14, 0, 0, 0),
	MSTP("rwdt0", &r_clk, MSTPCR0, 13, 0, 0, 0),
	MSTP("dmac1", &div4_clks[DIV4_B], MSTPCR0, 12, 0, 1, 1),
	MSTP("tmu1", &div4_clks[DIV4_P], MSTPCR0, 11, 0, 1, 0),
	MSTP("flctl0", &div4_clks[DIV4_P], MSTPCR0, 10, 0, 1, 0),
	MSTP("scif0", &div4_clks[DIV4_P], MSTPCR0, 9, 0, 1, 0),
	MSTP("scif1", &div4_clks[DIV4_P], MSTPCR0, 8, 0, 1, 0),
	MSTP("scif2", &div4_clks[DIV4_P], MSTPCR0, 7, 0, 1, 0),
	MSTP("scif3", &div4_clks[DIV4_B], MSTPCR0, 6, 0, 1, 0),
	MSTP("scif4", &div4_clks[DIV4_B], MSTPCR0, 5, 0, 1, 0),
	MSTP("scif5", &div4_clks[DIV4_B], MSTPCR0, 4, 0, 1, 0),
	MSTP("msiof0", &div4_clks[DIV4_B], MSTPCR0, 2, 0, 1, 0),
	MSTP("msiof1", &div4_clks[DIV4_B], MSTPCR0, 1, 0, 1, 0),
	MSTP("meram0", &div4_clks[DIV4_SH], MSTPCR0, 0, 1, 1, 0),

	MSTP("i2c0", &div4_clks[DIV4_P], MSTPCR1, 9, 0, 1, 0),
	MSTP("rtc0", &r_clk, MSTPCR1, 8, 0, 0, 0),

	MSTP("atapi0", &div4_clks[DIV4_SH], MSTPCR2, 28, 0, 1, 0),
	MSTP("adc0", &div4_clks[DIV4_P], MSTPCR2, 27, 0, 1, 0),
	MSTP("tpu0", &div4_clks[DIV4_B], MSTPCR2, 25, 0, 1, 0),
	MSTP("irda0", &div4_clks[DIV4_P], MSTPCR2, 24, 0, 1, 0),
	MSTP("tsif0", &div4_clks[DIV4_B], MSTPCR2, 22, 0, 1, 0),
	MSTP("icb0", &div4_clks[DIV4_B], MSTPCR2, 21, 0, 1, 1),
	MSTP("sdhi0", &div4_clks[DIV4_B], MSTPCR2, 18, 0, 1, 0),
	MSTP("sdhi1", &div4_clks[DIV4_B], MSTPCR2, 17, 0, 1, 0),
	MSTP("keysc0", &r_clk, MSTPCR2, 14, 0, 0, 0),
	MSTP("usb0", &div4_clks[DIV4_B], MSTPCR2, 11, 0, 1, 0),
	MSTP("2dg0", &div4_clks[DIV4_B], MSTPCR2, 10, 0, 1, 1),
	MSTP("siu0", &div4_clks[DIV4_B], MSTPCR2, 8, 0, 1, 0),
	MSTP("veu1", &div4_clks[DIV4_B], MSTPCR2, 6, 1, 1, 1),
	MSTP("vou0", &div4_clks[DIV4_B], MSTPCR2, 5, 0, 1, 1),
	MSTP("beu0", &div4_clks[DIV4_B], MSTPCR2, 4, 0, 1, 1),
	MSTP("ceu0", &div4_clks[DIV4_B], MSTPCR2, 3, 0, 1, 1),
	MSTP("veu0", &div4_clks[DIV4_B], MSTPCR2, 2, 1, 1, 1),
	MSTP("vpu0", &div4_clks[DIV4_B], MSTPCR2, 1, 1, 1, 1),
	MSTP("lcdc0", &div4_clks[DIV4_B], MSTPCR2, 0, 0, 1, 1),
};

int __init arch_clk_init(void)
{
	int k, ret = 0;

	/* autodetect extal or dll configuration */
	if (__raw_readl(PLLCR) & 0x1000)
		pll_clk.parent = &dll_clk;
	else
		pll_clk.parent = &extal_clk;

	for (k = 0; !ret && (k < ARRAY_SIZE(main_clks)); k++)
		ret = clk_register(main_clks[k]);

	if (!ret)
		ret = sh_clk_div4_register(div4_clks, DIV4_NR, &div4_table);

	if (!ret)
		ret = sh_clk_div6_register(div6_clks, ARRAY_SIZE(div6_clks));

	if (!ret)
		ret = sh_clk_mstp32_register(mstp_clks, ARRAY_SIZE(mstp_clks));

	return ret;
}
