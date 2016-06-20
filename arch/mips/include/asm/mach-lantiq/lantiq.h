/*
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 *
 *  Copyright (C) 2010 John Crispin <john@phrozen.org>
 */
#ifndef _LANTIQ_H__
#define _LANTIQ_H__

#include <linux/irq.h>
#include <linux/device.h>
#include <linux/clk.h>

/* generic reg access functions */
#define ltq_r32(reg)		__raw_readl(reg)
#define ltq_w32(val, reg)	__raw_writel(val, reg)
#define ltq_w32_mask(clear, set, reg)	\
	ltq_w32((ltq_r32(reg) & ~(clear)) | (set), reg)
#define ltq_r8(reg)		__raw_readb(reg)
#define ltq_w8(val, reg)	__raw_writeb(val, reg)

/* register access macros for EBU */
#define ltq_ebu_w32(x, y)	ltq_w32((x), ltq_ebu_membase + (y))
#define ltq_ebu_r32(x)		ltq_r32(ltq_ebu_membase + (x))
#define ltq_ebu_w32_mask(x, y, z) \
	ltq_w32_mask(x, y, ltq_ebu_membase + (z))
extern __iomem void *ltq_ebu_membase;

/* spinlock all ebu i/o */
extern spinlock_t ebu_lock;

/* some irq helpers */
extern void ltq_disable_irq(struct irq_data *data);
extern void ltq_mask_and_ack_irq(struct irq_data *data);
extern void ltq_enable_irq(struct irq_data *data);
extern int ltq_eiu_get_irq(int exin);

/* legacy clock handling */
static inline struct clk *clk_get_fpi(void)
{
	return clk_get(NULL, "fpi");
}

static inline struct clk *clk_get_io(void)
{
	return clk_get(NULL, "io");
}

static inline struct clk *clk_get_ppe(void)
{
	return clk_get(NULL, "pp32");
}

/* find out what bootsource we have */
extern unsigned char ltq_boot_select(void);
/* find out what caused the last cpu reset */
extern int ltq_reset_cause(void);
/* find out the soc type */
extern int ltq_soc_type(void);

#define IOPORT_RESOURCE_START	0x10000000
#define IOPORT_RESOURCE_END	0xffffffff
#define IOMEM_RESOURCE_START	0x10000000
#define IOMEM_RESOURCE_END	0xffffffff

#endif
