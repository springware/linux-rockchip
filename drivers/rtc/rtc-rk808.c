/*
 * RTC driver for Rockchip RK808
 *
 * Copyright (c) 2014, Fuzhou Rockchip Electronics Co., Ltd
 *
 * Author: Chris Zhong <zyw@rock-chips.com>
 * Author: Zhang Qing <zhangqing@rock-chips.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/time.h>
#include <linux/rtc.h>
#include <linux/slab.h>
#include <linux/bcd.h>
#include <linux/interrupt.h>
#include <linux/ioctl.h>
#include <linux/completion.h>
#include <linux/mfd/rk808.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/irqdomain.h>

/* RTC_CTRL_REG bitfields */
#define BIT_RTC_CTRL_REG_STOP_RTC_M		0x01
#define BIT_RTC_CTRL_REG_ROUND_30S_M		0x02
#define BIT_RTC_CTRL_REG_AUTO_COMP_M		0x04
#define BIT_RTC_CTRL_REG_MODE_12_24_M		0x08
#define BIT_RTC_CTRL_REG_TEST_MODE_M		0x10
#define BIT_RTC_CTRL_REG_SET_32_COUNTER_M	0x20
#define BIT_RTC_CTRL_REG_GET_TIME_M		0x40
#define BIT_RTC_CTRL_REG_RTC_V_OPT_M		0x80

/* RTC_STATUS_REG bitfields */
#define BIT_RTC_STATUS_REG_RUN_M		0x02
#define BIT_RTC_STATUS_REG_1S_EVENT_M		0x04
#define BIT_RTC_STATUS_REG_1M_EVENT_M		0x08
#define BIT_RTC_STATUS_REG_1H_EVENT_M		0x10
#define BIT_RTC_STATUS_REG_1D_EVENT_M		0x20
#define BIT_RTC_STATUS_REG_ALARM_M		0x40
#define BIT_RTC_STATUS_REG_POWER_UP_M		0x80

/* RTC_INTERRUPTS_REG bitfields */
#define BIT_RTC_INTERRUPTS_REG_EVERY_M		0x03
#define BIT_RTC_INTERRUPTS_REG_IT_TIMER_M	0x04
#define BIT_RTC_INTERRUPTS_REG_IT_ALARM_M	0x08

/* DEVCTRL bitfields */
#define BIT_RTC_PWDN				0x40

/* REG_SECONDS_REG through REG_YEARS_REG is how many registers? */
#define ALL_TIME_REGS				7
#define ALL_ALM_REGS				6

#define RTC_SET_TIME_RETRIES	5
#define RTC_GET_TIME_RETRIES	5

struct rk808_rtc {
	struct rk808 *rk808;
	struct rtc_device *rtc;
	unsigned int alarm_enabled:1;
};

/*
 * Read current time and date in RTC
 */
static int rk808_rtc_readtime(struct device *dev, struct rtc_time *tm)
{
	struct rk808_rtc *rk808_rtc = dev_get_drvdata(dev);
	struct rk808 *rk808 = rk808_rtc->rk808;
	int ret;
	u8 rtc_data[ALL_TIME_REGS + 1];
	uint32_t rtc_ctl;

	/* Has the RTC been programmed? */
	ret = regmap_read(rk808->regmap, RK808_RTC_CTRL_REG, &rtc_ctl);
	if (ret < 0) {
		dev_err(dev, "Failed to read RTC control: %d\n", ret);
		return ret;
	}

	rtc_ctl &= ~BIT_RTC_CTRL_REG_RTC_V_OPT_M;

	ret = regmap_write(rk808->regmap, RK808_RTC_CTRL_REG, rtc_ctl);
	if (ret < 0) {
		dev_err(dev, "Failed to write RTC control: %d\n", ret);
		return ret;
	}

	ret = regmap_bulk_read(rk808->regmap, RK808_SECONDS_REG, rtc_data, 6);
	if (ret < 0) {
		dev_err(dev, "Failed to bulk read rtc_data: %d\n", ret);
		return ret;
	}
	tm->tm_sec = bcd2bin(rtc_data[0]);
	tm->tm_min = bcd2bin(rtc_data[1]);
	tm->tm_hour = bcd2bin(rtc_data[2]);
	tm->tm_mday = bcd2bin(rtc_data[3]);
	tm->tm_mon = bcd2bin(rtc_data[4]) - 1;
	tm->tm_year = bcd2bin(rtc_data[5]) + 100;
	tm->tm_wday = bcd2bin(rtc_data[6]);
	dev_dbg(dev, "RTC date/time %4d-%02d-%02d(%d) %02d:%02d:%02d\n",
		1900 + tm->tm_year, tm->tm_mon + 1, tm->tm_mday,
		tm->tm_wday, tm->tm_hour , tm->tm_min, tm->tm_sec);

	return 0;
}

/*
 * Set current time and date in RTC
 */
static int rk808_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct rk808_rtc *rk808_rtc = dev_get_drvdata(dev);
	struct rk808 *rk808 = rk808_rtc->rk808;
	int ret;
	uint32_t rtc_ctl;
	uint32_t rtc_data[ALL_TIME_REGS + 1];

	rtc_data[0] = bin2bcd(tm->tm_sec);
	rtc_data[1] = bin2bcd(tm->tm_min);
	rtc_data[2] = bin2bcd(tm->tm_hour);
	rtc_data[3] = bin2bcd(tm->tm_mday);
	rtc_data[4] = bin2bcd(tm->tm_mon + 1);
	rtc_data[5] = bin2bcd(tm->tm_year - 100);
	rtc_data[6] = bin2bcd(tm->tm_wday);
	dev_dbg(dev, "set RTC date/time %4d-%02d-%02d(%d) %02d:%02d:%02d\n",
		1900 + tm->tm_year, tm->tm_mon + 1, tm->tm_mday,
		tm->tm_wday, tm->tm_hour , tm->tm_min, tm->tm_sec);

	/* Stop RTC while updating the RTC registers */
	ret = regmap_read(rk808->regmap, RK808_RTC_CTRL_REG, &rtc_ctl);
	if (ret < 0) {
		dev_err(dev, "Failed to read RTC control: %d\n", ret);
		return ret;
	}
	rtc_ctl |= (BIT_RTC_CTRL_REG_STOP_RTC_M);
	ret = regmap_write(rk808->regmap, RK808_RTC_CTRL_REG, rtc_ctl);
	if (ret < 0) {
		dev_err(dev, "Failed to write RTC control: %d\n", ret);
		return ret;
	}

	ret = regmap_bulk_write(rk808->regmap, RK808_SECONDS_REG, rtc_data, 6);
	if (ret < 0) {
		dev_err(dev, "Failed to bull write rtc_data: %d\n", ret);
		return ret;
	}
	/* Start RTC again */
	ret = regmap_read(rk808->regmap, RK808_RTC_CTRL_REG, &rtc_ctl);
	if (ret < 0) {
		dev_err(dev, "Failed to read RTC control: %d\n", ret);
		return ret;
	}
	rtc_ctl &= ~BIT_RTC_CTRL_REG_STOP_RTC_M;
	ret = regmap_write(rk808->regmap, RK808_RTC_CTRL_REG, rtc_ctl);
	if (ret < 0) {
		dev_err(dev, "Failed to write RTC control: %d\n", ret);
		return ret;
	}
	return 0;
}

/*
 * Read alarm time and date in RTC
 */
static int rk808_rtc_readalarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct rk808_rtc *rk808_rtc = dev_get_drvdata(dev);
	struct rk808 *rk808 = rk808_rtc->rk808;
	int ret;
	uint32_t int_reg;
	u8 alrm_data[ALL_ALM_REGS + 1];

	ret = regmap_bulk_read(rk808->regmap,
			RK808_ALARM_SECONDS_REG, alrm_data, 6);

	/* some of these fields may be wildcard/"match all" */
	alrm->time.tm_sec = bcd2bin(alrm_data[0]);
	alrm->time.tm_min = bcd2bin(alrm_data[1]);
	alrm->time.tm_hour = bcd2bin(alrm_data[2]);
	alrm->time.tm_mday = bcd2bin(alrm_data[3]);
	alrm->time.tm_mon = bcd2bin(alrm_data[4]) - 1;
	alrm->time.tm_year = bcd2bin(alrm_data[5]) + 100;

	ret = regmap_read(rk808->regmap, RK808_RTC_INT_REG, &int_reg);
	if (ret < 0) {
		dev_err(dev, "Failed to read RTC INT REG: %d\n", ret);
		return ret;
	}

	dev_dbg(dev, "alrm read RTC date/time %4d-%02d-%02d(%d) %02d:%02d:%02d\n",
		1900 + alrm->time.tm_year, alrm->time.tm_mon + 1,
		alrm->time.tm_mday, alrm->time.tm_wday, alrm->time.tm_hour,
		alrm->time.tm_min, alrm->time.tm_sec);

	if (int_reg & BIT_RTC_INTERRUPTS_REG_IT_ALARM_M)
		alrm->enabled = 1;
	else
		alrm->enabled = 0;

	return 0;
}

static int rk808_rtc_stop_alarm(struct rk808_rtc *rk808_rtc)
{
	int int_reg;
	int ret = 0;
	struct rk808 *rk808 = rk808_rtc->rk808;

	ret = regmap_read(rk808->regmap, RK808_RTC_INT_REG, &int_reg);
	if (!ret) {
		int_reg &= ~BIT_RTC_INTERRUPTS_REG_IT_ALARM_M;
		ret = regmap_write(rk808->regmap, RK808_RTC_INT_REG, int_reg);
	}
	if (!ret)
		rk808_rtc->alarm_enabled = 0;

	return ret;
}

static int rk808_rtc_start_alarm(struct rk808_rtc *rk808_rtc)
{
	int int_reg;
	int ret = 0;
	struct rk808 *rk808 = rk808_rtc->rk808;

	ret = regmap_read(rk808->regmap, RK808_RTC_INT_REG, &int_reg);
	if (!ret) {
		int_reg |= BIT_RTC_INTERRUPTS_REG_IT_ALARM_M;
		ret = regmap_write(rk808->regmap, RK808_RTC_INT_REG, int_reg);
	}
	if (!ret)
		rk808_rtc->alarm_enabled = 1;

	return ret;
}

static int rk808_rtc_setalarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct rk808_rtc *rk808_rtc = dev_get_drvdata(dev);
	struct rk808 *rk808 = rk808_rtc->rk808;
	int ret;
	unsigned char alrm_data[ALL_TIME_REGS + 1];

	ret = rk808_rtc_stop_alarm(rk808_rtc);
	if (ret < 0) {
		dev_err(dev, "Failed to stop alarm: %d\n", ret);
		return ret;
	}
	dev_dbg(dev, "alrm set RTC date/time %4d-%02d-%02d(%d) %02d:%02d:%02d\n",
		1900 + alrm->time.tm_year, alrm->time.tm_mon + 1,
		alrm->time.tm_mday, alrm->time.tm_wday, alrm->time.tm_hour,
		alrm->time.tm_min, alrm->time.tm_sec);

	alrm_data[0] = bin2bcd(alrm->time.tm_sec);
	alrm_data[1] = bin2bcd(alrm->time.tm_min);
	alrm_data[2] = bin2bcd(alrm->time.tm_hour);
	alrm_data[3] = bin2bcd(alrm->time.tm_mday);
	alrm_data[4] = bin2bcd(alrm->time.tm_mon + 1);
	alrm_data[5] = bin2bcd(alrm->time.tm_year - 100);

	ret = regmap_bulk_write(rk808->regmap,
			RK808_ALARM_SECONDS_REG, alrm_data, 6);
	if (ret < 0) {
		dev_err(dev, "Failed to bulk write: %d\n", ret);
		return ret;
	}
	if (alrm->enabled) {
		ret = rk808_rtc_start_alarm(rk808_rtc);
		if (ret < 0) {
			dev_err(dev, "Failed to start alarm: %d\n", ret);
			return ret;
		}
	}
	return 0;
}

static int rk808_rtc_alarm_irq_enable(struct device *dev,
				       unsigned int enabled)
{
	struct rk808_rtc *rk808_rtc = dev_get_drvdata(dev);

	if (enabled)
		return rk808_rtc_start_alarm(rk808_rtc);
	else
		return rk808_rtc_stop_alarm(rk808_rtc);
}

/*
 * We will just handle setting the frequency and make use the framework for
 * reading the periodic interupts.
 *
 * @freq: Current periodic IRQ freq:
 * bit 0: every second
 * bit 1: every minute
 * bit 2: every hour
 * bit 3: every day
 */
static irqreturn_t rk808_alm_irq(int irq, void *data)
{
	struct rk808_rtc *rk808_rtc = data;
	struct rk808 *rk808 = rk808_rtc->rk808;
	int ret;
	uint32_t rtc_ctl;

	ret = regmap_read(rk808->regmap, RK808_RTC_STATUS_REG, &rtc_ctl);
	if (ret < 0) {
		dev_err(rk808_rtc->rk808->dev,
			"%s:Failed to read RTC status: %d\n", __func__, ret);
		return ret;
	}

	rtc_ctl &= 0xff;

	/* The alarm interrupt keeps its low level, until the micro-controller
	 * write 1 in the ALARM bit of the RTC_STATUS_REG register.
	 */
	ret = regmap_write(rk808->regmap, RK808_RTC_STATUS_REG, rtc_ctl);
	if (ret < 0) {
		dev_err(rk808_rtc->rk808->dev,
			"%s:Failed to read RTC status: %d\n", __func__, ret);
		return ret;
	}

	rtc_update_irq(rk808_rtc->rtc, 1, RTC_IRQF | RTC_AF);
	dev_info(rk808_rtc->rk808->dev,
		"%s:irq=%d,rtc_ctl=0x%x\n", __func__, irq, rtc_ctl);
	return IRQ_HANDLED;
}

static const struct rtc_class_ops rk808_rtc_ops = {
	.read_time = rk808_rtc_readtime,
	.set_time = rk808_rtc_set_time,
	.read_alarm = rk808_rtc_readalarm,
	.set_alarm = rk808_rtc_setalarm,
	.alarm_irq_enable = rk808_rtc_alarm_irq_enable,
};

#ifdef CONFIG_PM
/* Turn off the alarm if it should not be a wake source. */
static int rk808_rtc_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct rk808_rtc *rk808_rtc = dev_get_drvdata(&pdev->dev);
	int ret;

	if (rk808_rtc->alarm_enabled && device_may_wakeup(&pdev->dev))
		ret = rk808_rtc_start_alarm(rk808_rtc);
	else
		ret = rk808_rtc_stop_alarm(rk808_rtc);

	if (ret != 0)
		dev_err(&pdev->dev, "Failed to update RTC alarm: %d\n", ret);

	return 0;
}

/* Enable the alarm if it should be enabled (in case it was disabled to
 * prevent use as a wake source).
 */
static int rk808_rtc_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct rk808_rtc *rk808_rtc = dev_get_drvdata(&pdev->dev);
	int ret;

	if (rk808_rtc->alarm_enabled) {
		ret = rk808_rtc_start_alarm(rk808_rtc);
		if (ret)
			dev_err(&pdev->dev,
				"Failed to restart RTC alarm: %d\n", ret);
	}

	return 0;
}

#else
#define rk808_rtc_suspend NULL
#define rk808_rtc_resume NULL
#endif

static const struct dev_pm_ops rk808_rtc_pm_ops = {
	.suspend = rk808_rtc_suspend,
	.resume = rk808_rtc_resume,
	.poweroff = rk808_rtc_suspend,
};

/*2012.1.1 12:00:00 Saturday*/
struct rtc_time tm_def = {
			.tm_wday = 6,
			.tm_year = 112,
			.tm_mon = 0,
			.tm_mday = 1,
			.tm_hour = 12,
			.tm_min = 0,
			.tm_sec = 0,
};

static int rk808_rtc_probe(struct platform_device *pdev)
{
	struct rk808 *rk808 = dev_get_drvdata(pdev->dev.parent);
	struct rk808_rtc *rk808_rtc;
	struct rtc_time tm;
	int alm_irq;
	int ret = 0;
	uint32_t rtc_ctl;

	rk808_rtc = devm_kzalloc(&pdev->dev, sizeof(*rk808_rtc), GFP_KERNEL);
	if (rk808_rtc == NULL)
		return -ENOMEM;

	platform_set_drvdata(pdev, rk808_rtc);
	rk808_rtc->rk808 = rk808;

	/* start rtc default */
	ret = regmap_read(rk808->regmap, RK808_RTC_CTRL_REG, &rtc_ctl);
	if (ret < 0) {
		dev_err(&pdev->dev,
			"Failed to read RTC control: %d\n", ret);
		return ret;
	}
	rtc_ctl &= ~BIT_RTC_CTRL_REG_STOP_RTC_M;

	ret = regmap_write(rk808->regmap, RK808_RTC_CTRL_REG, rtc_ctl);
	if (ret < 0) {
		dev_err(&pdev->dev,
			"Failed to write RTC control: %d\n", ret);
			return ret;
	}

	ret = regmap_write(rk808->regmap, RK808_RTC_STATUS_REG, 0xfe);
	if (ret < 0) {
		dev_err(&pdev->dev,
			"Failed to write RTC control: %d\n", ret);
			return ret;
	}
	/* set init time */
	ret = rk808_rtc_readtime(&pdev->dev, &tm);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to read RTC time\n");
		return ret;
	}
	ret = rtc_valid_tm(&tm);
	if (ret) {
		dev_err(&pdev->dev, "invalid date/time and init time\n");
		rk808_rtc_set_time(&pdev->dev, &tm_def);
	}

	device_init_wakeup(&pdev->dev, 1);

	rk808_rtc->rtc = devm_rtc_device_register(&pdev->dev,
		"rk808", &rk808_rtc_ops, THIS_MODULE);
	if (IS_ERR(rk808_rtc->rtc)) {
		ret = PTR_ERR(rk808_rtc->rtc);
		return ret;
	}

	alm_irq  = platform_get_irq(pdev, 0);
	if (alm_irq <= 0) {
		dev_warn(&pdev->dev, "Wake up is not possible as irq = %d\n",
			 alm_irq);
		return -ENXIO;
	}

	/* request alarm irq of rk808 */
	ret = devm_request_threaded_irq(&pdev->dev, alm_irq, NULL,
		rk808_alm_irq, IRQF_TRIGGER_LOW | IRQF_EARLY_RESUME,
		"RTC alarm", rk808_rtc);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request alarm IRQ %d: %d\n",
			alm_irq, ret);
	}
	device_set_wakeup_capable(&pdev->dev, 1);

	dev_info(rk808->dev, "%s:ok\n", __func__);

	return 0;
}

static struct platform_driver rk808_rtc_driver = {
	.probe = rk808_rtc_probe,
	.driver = {
		.name = "rk808-rtc",
		.pm = &rk808_rtc_pm_ops,
	},
};

module_platform_driver(rk808_rtc_driver);

MODULE_DESCRIPTION("RTC driver for the rk808 series PMICs");
MODULE_AUTHOR("Chris Zhong <zyw@rock-chips.com>");
MODULE_AUTHOR("Zhang Qing <zhanqging@rock-chips.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:rk808-rtc");
