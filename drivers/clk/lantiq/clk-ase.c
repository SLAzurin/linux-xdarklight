/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * Copyright (C) 2015 Martin Blumenstingl <martin.blumenstingl@googlemail.com>
 */

#include <dt-bindings/clock/lantiq-xway-pmu.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/types.h>

#include "clk-xway.h"

#define CGU_SYS			0x10
#define CGU_IFCCR		0x18

#define IFCCR_EPHY_GATE_BIT	BIT(5)

static inline void ltq_ase_ephy_gate_set(struct ltq_cgu_clk *cgu_clk,
					 bool enable)
{
	u32 val = ltq_cgu_clk_read(cgu_clk, CGU_IFCCR);

	if (enable)
		val |= IFCCR_EPHY_GATE_BIT;
	else
		val &= ~IFCCR_EPHY_GATE_BIT;

	__raw_writel(val, cgu_clk->reg_base + CGU_IFCCR);
}

static int ltq_ase_ephy_gate_enable(struct clk_hw *hw)
{
	struct ltq_cgu_clk *cgu_clk = to_ltq_cgu_clk(hw);

	ltq_ase_ephy_gate_set(cgu_clk, 1);

	return 0;
}

static void ltq_ase_ephy_gate_disable(struct clk_hw *hw)
{
	struct ltq_cgu_clk *cgu_clk = to_ltq_cgu_clk(hw);

	ltq_ase_ephy_gate_set(cgu_clk, 0);
}

static int ltq_ase_ephy_gate_is_enabled(struct clk_hw *hw)
{
	struct ltq_cgu_clk *cgu_clk = to_ltq_cgu_clk(hw);

	return ltq_cgu_clk_read(cgu_clk, CGU_IFCCR) & IFCCR_EPHY_GATE_BIT;
}

static unsigned long ltq_ase_cpu_recalc_rate(struct clk_hw *hw,
					     unsigned long parent_rate)
{
	struct ltq_cgu_clk *cgu_clk = to_ltq_cgu_clk(hw);
	unsigned long rate;

	if (ltq_cgu_clk_read(cgu_clk, CGU_SYS) & (1 << 5))
		rate = CLOCK_266M;
	else
		rate = CLOCK_133M;

	return rate;
}

const struct clk_ops ase_ephy_gate_ops = {
	.enable = ltq_ase_ephy_gate_enable,
	.disable = ltq_ase_ephy_gate_disable,
	.is_enabled = ltq_ase_ephy_gate_is_enabled,
};

const struct clk_ops ase_cpu_clk_ops = {
	.recalc_rate = ltq_ase_cpu_recalc_rate,
};

static void __init ase_cgu_clocks_init_dt(struct device_node *np)
{
	struct ltq_cgu_clocks cgu_clocks = {};
	struct clk *ephy_gate;

	ltq_xway_cgu_init_dt(np);

	ephy_gate = ltq_cgu_register_clk("ephy", NULL, &ase_ephy_gate_ops);

	cgu_clocks.cpu_clk = ltq_cgu_register_clk("cpu", NULL,
						  &ase_cpu_clk_ops);
	cgu_clocks.fpi_clk = clk_register_fixed_rate(NULL, "fpi", NULL,
						      CLK_IS_ROOT, CLOCK_133M);
	cgu_clocks.io_clk = cgu_clocks.fpi_clk; /* FPI clock is used for IO */
	cgu_clocks.pp32_clk = cgu_clocks.cpu_clk; /* PP32 and CPU are equal */
	cgu_clocks.ephy_clk = ephy_gate;

	ltq_cgu_clk_of_add_provider(np, &cgu_clocks);

	if (clk_register_clkdev(ephy_gate, "ephycgu", "1e180000.etop"))
		pr_err("%s: Failed to register ephycgu lookup\n", __func__);
}

CLK_OF_DECLARE(cgu_ase, "lantiq,cgu-ase", ase_cgu_clocks_init_dt);

static struct ltq_xway_pmu_gate ase_pmu_gates[] __initdata = {
	PMU_GATE(PMU_GATE_USB0_PHY, "pmu_usb0_phy", 0, NULL, 0, NULL, NULL),
	PMU_GATE(PMU_GATE_SDIO, "pmu_sdio", 2, NULL, 0, NULL, NULL),
	PMU_GATE(PMU_GATE_DMA, "pmu_dma", 5, NULL, 0, "1e104100.dma", NULL),
	PMU_GATE(PMU_GATE_USB0_CTRL, "pmu_usb0_ctrl", 6, NULL, 0, NULL, NULL),
	PMU_GATE(PMU_GATE_EPHY, "pmu_ephy", 7, NULL, 0, "1e180000.etop",
		 "ephy"),
	PMU_GATE(PMU_GATE_SPI, "pmu_spi", 8, NULL, 0, "1e100800.spi", NULL),
	PMU_GATE(PMU_GATE_DSL_DFE, "pmu_dsl_dfe", 9, NULL, 0, NULL, NULL),
	PMU_GATE(PMU_GATE_EBU, "pmu_ebu", 10, NULL, 0, "1e105300.ebu", NULL),
	PMU_GATE(PMU_GATE_STP, "pmu_stp", 11, NULL, 0, "1e100bb0.stp", NULL),
	PMU_GATE(PMU_GATE_GPTC, "pmu_gptu", 12, NULL, 0, "1e100a00.gptu",
		 NULL),
	PMU_GATE(PMU_GATE_ETOP, "pmu_etop", 13, NULL, 0, "1e180000.etop",
		 NULL),
	PMU_GATE(PMU_GATE_FPI0, "pmu_fpi0", 14, NULL, 0, "10000000.fpi", NULL),
	PMU_GATE(PMU_GATE_AHB, "pmu_ahb", 15, NULL, 0, NULL, NULL),
	PMU_GATE(PMU_GATE_UART1, "pmu_serial1", 17, NULL, 0, "1e100c00.serial",
		 NULL),
	PMU_GATE(PMU_GATE_WDT0, "pmu_wdt0", 18, NULL, CLK_IGNORE_UNUSED, NULL,
		 NULL),
	PMU_GATE(PMU_GATE_PPE_TC, "pmu_ppe_tc", 21, NULL, 0, NULL, NULL),
	PMU_GATE(PMU_GATE_PPE_DPLUS, "pmu_ppe_dplus", 23, NULL, 0, NULL, NULL),
};

static void __init ase_pmu_clk_gates_init_dt(struct device_node *np)
{
	ltq_xway_pmu_register_gates(np,
				    ase_pmu_gates,
				    ARRAY_SIZE(ase_pmu_gates),
				    &xway_pmu_clk_gate_ops);
}

CLK_OF_DECLARE(pmu_ase, "lantiq,pmu-ase", ase_pmu_clk_gates_init_dt);
