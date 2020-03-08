// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020 Martin Blumenstingl <martin.blumenstingl@googlemail.com>
 *
 * Based on panfrost_devfreq.c:
 *   Copyright 2019 Collabora ltd.
 */
#include <linux/clk.h>
#include <linux/devfreq.h>
#include <linux/devfreq_cooling.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/property.h>

#include "lima_device.h"
#include "lima_devfreq.h"

enum lima_devfreq_update_mode {
	LIMA_DEVFREQ_UPDATE_NONE,
	LIMA_DEVFREQ_UPDATE_BUSY,
	LIMA_DEVFREQ_UPDATE_IDLE
};

static void lima_devfreq_update_utilization(struct lima_device *ldev,
					    enum lima_devfreq_update_mode mode)
{
	unsigned long irqflags;
	ktime_t now, last;

	if (!ldev->devfreq.devfreq)
		return;

	spin_lock_irqsave(&ldev->devfreq.lock, irqflags);

	switch (mode) {
	case LIMA_DEVFREQ_UPDATE_BUSY:
		ldev->devfreq.busy_count++;
		break;

	case LIMA_DEVFREQ_UPDATE_IDLE:
		if (!WARN_ON(!ldev->devfreq.busy_count))
			ldev->devfreq.busy_count--;
		break;

	default:
		break;
	}

	now = ktime_get();
	last = ldev->devfreq.time_last_update;

	if (ldev->devfreq.busy_count)
		ldev->devfreq.busy_time += ktime_sub(now, last);
	else
		ldev->devfreq.idle_time += ktime_sub(now, last);

	ldev->devfreq.time_last_update = now;

	spin_unlock_irqrestore(&ldev->devfreq.lock, irqflags);
}

static int lima_devfreq_target(struct device *dev, unsigned long *freq,
			       u32 flags)
{
	struct dev_pm_opp *opp;
	int err;

	opp = devfreq_recommended_opp(dev, freq, flags);
	if (IS_ERR(opp))
		return PTR_ERR(opp);
	dev_pm_opp_put(opp);

	err = dev_pm_opp_set_rate(dev, *freq);
	if (err)
		return err;

	return 0;
}

static void lima_devfreq_reset(struct lima_device *ldev)
{
	unsigned long irqflags;

	spin_lock_irqsave(&ldev->devfreq.lock, irqflags);

	ldev->devfreq.busy_time = 0;
	ldev->devfreq.idle_time = 0;
	ldev->devfreq.time_last_update = ktime_get();

	spin_unlock_irqrestore(&ldev->devfreq.lock, irqflags);
}

static int lima_devfreq_get_dev_status(struct device *dev,
				       struct devfreq_dev_status *status)
{
	struct lima_device *ldev = dev_get_drvdata(dev);
	unsigned long irqflags;

	lima_devfreq_update_utilization(ldev, LIMA_DEVFREQ_UPDATE_NONE);

	status->current_frequency = clk_get_rate(ldev->clk_gpu);

	spin_lock_irqsave(&ldev->devfreq.lock, irqflags);

	status->total_time = ktime_to_ns(ktime_add(ldev->devfreq.busy_time,
						   ldev->devfreq.idle_time));
	status->busy_time = ktime_to_ns(ldev->devfreq.busy_time);

	spin_unlock_irqrestore(&ldev->devfreq.lock, irqflags);

	lima_devfreq_reset(ldev);

	dev_dbg(ldev->dev, "busy %lu total %lu %lu %% freq %lu MHz\n",
		status->busy_time, status->total_time,
		status->busy_time / (status->total_time / 100),
		status->current_frequency / 1000 / 1000);

	return 0;
}

static struct devfreq_dev_profile lima_devfreq_profile = {
	.polling_ms = 50, /* ~3 frames */
	.target = lima_devfreq_target,
	.get_dev_status = lima_devfreq_get_dev_status,
};

void lima_devfreq_fini(struct lima_device *ldev)
{
	if (ldev->devfreq.cooling)
		devfreq_cooling_unregister(ldev->devfreq.cooling);

	if (ldev->devfreq.devfreq)
		devm_devfreq_remove_device(&ldev->pdev->dev,
					   ldev->devfreq.devfreq);

	if (ldev->devfreq.has_opp_of_table)
		dev_pm_opp_of_remove_table(&ldev->pdev->dev);

	if (ldev->devfreq.regulators_opp_table)
		dev_pm_opp_put_regulators(ldev->devfreq.regulators_opp_table);

	if (ldev->devfreq.clkname_opp_table)
		dev_pm_opp_put_clkname(ldev->devfreq.clkname_opp_table);
}

int lima_devfreq_init(struct lima_device *ldev)
{
	struct thermal_cooling_device *cooling;
	struct device *dev = &ldev->pdev->dev;
	struct opp_table *opp_table;
	struct devfreq *devfreq;
	struct dev_pm_opp *opp;
	unsigned long cur_freq;
	int ret;

	if (!device_property_present(dev, "operating-points-v2"))
		/* Optional, continue without devfreq */
		return 0;

	spin_lock_init(&ldev->devfreq.lock);

	opp_table = dev_pm_opp_set_clkname(dev, "core");
	if (IS_ERR(opp_table)) {
		ret = PTR_ERR(opp_table);
		goto err_fini;
	}

	ldev->devfreq.clkname_opp_table = opp_table;

	opp_table = dev_pm_opp_set_regulators(dev,
					      (const char *[]){ "mali" },
					      1);
	if (IS_ERR(opp_table)) {
		ret = PTR_ERR(opp_table);

		/* Continue if the optional regulator is missing */
		if (ret != -ENODEV)
			goto err_fini;
	} else {
		ldev->devfreq.regulators_opp_table = opp_table;
	}

	ret = dev_pm_opp_of_add_table(dev);
	if (ret)
		goto err_fini;

	ldev->devfreq.has_opp_of_table = true;

	lima_devfreq_reset(ldev);

	cur_freq = clk_get_rate(ldev->clk_gpu);

	opp = devfreq_recommended_opp(dev, &cur_freq, 0);
	if (IS_ERR(opp)) {
		ret = PTR_ERR(opp);
		goto err_fini;
	}

	lima_devfreq_profile.initial_freq = cur_freq;
	dev_pm_opp_put(opp);

	devfreq = devm_devfreq_add_device(dev, &lima_devfreq_profile,
					  DEVFREQ_GOV_SIMPLE_ONDEMAND, NULL);
	if (IS_ERR(devfreq)) {
		dev_err(dev, "Couldn't initialize GPU devfreq\n");
		ret = PTR_ERR(devfreq);
		goto err_fini;
	}

	ldev->devfreq.devfreq = devfreq;

	cooling = of_devfreq_cooling_register(dev->of_node, devfreq);
	if (IS_ERR(cooling))
		dev_info(dev, "Failed to register cooling device\n");
	else
		ldev->devfreq.cooling = cooling;

	return 0;

err_fini:
	lima_devfreq_fini(ldev);
	return ret;
}

void lima_devfreq_record_busy(struct lima_device *ldev)
{
	lima_devfreq_update_utilization(ldev, LIMA_DEVFREQ_UPDATE_BUSY);
}

void lima_devfreq_record_idle(struct lima_device *ldev)
{
	lima_devfreq_update_utilization(ldev, LIMA_DEVFREQ_UPDATE_IDLE);
}
