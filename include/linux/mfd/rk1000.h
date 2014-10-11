/*
 * rk808.h for Rockchip RK808
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
 */

#ifndef __LINUX_MFD_RK1000_H
#define __LINUX_MFD_RK1000_H

#include <linux/regmap.h>
#include <linux/gpio/consumer.h>

#define RK1000_ADC_CON			0x00
#define RK1000_ADC_CON_ADC1_PDOWN	BIT(7)
#define RK1000_ADC_CON_ADC0_PDOWN	BIT(3)

#define RK1000_CODEC_CON		0x01
#define RK1000_CODEC_CON_CLK_LCDC	BIT(4)
#define RK1000_CODEC_CON_CLK_I2S	0
#define RK1000_CODEC_CON_DAC_DISABLE	BIT(3)
#define RK1000_CODEC_CON_ADC_DISABLE	BIT(2)
#define RK1000_CODEC_CON_DISABLE	BIT(0)

#define RK1000_I2C_CON			0x02
#define RK1000_TVE_CON			0x03
#define RK1000_TVE_CON_VDAC_R_BYPASS	BIT(7)
#define RK1000_TVE_CON_CVBS_ENABLE	BIT(6)
#define RK1000_TVE_CON_RGB2CCIR_MASK	(3 << 4)
#define RK1000_TVE_CON_RGB2CCIR_YUV	(3 << 4)
#define RK1000_TVE_CON_RGB2CCIR_RGB565	(2 << 4)
#define RK1000_TVE_CON_RGB2CCIR_RGB666	(1 << 4)
#define RK1000_TVE_CON_RGB2CCIR_RGB888	(0 << 4)
#define RK1000_TVE_CON_RGB2CCIR_BGR	BIT(3)
#define RK1000_TVE_CON_RGB2CCIR_INTERL	BIT(2)
#define RK1000_TVE_CON_RGB2CCIR_PROGR	0
#define RK1000_TVE_CON_RGB2CCIR_NTSC	BIT(1)
#define RK1000_TVE_CON_RGB2CCIR_PAL	0
#define RK1000_TVE_CON_RGB2CCIR_ENABLE	BIT(0)

#define RK1000_SRESET_CON		0x04
#define RK1000_SRESET_CON_RGB2CCIR	BIT(0)

#define RK1000_ADC_STAT			0x07 /* or 0x05? */
#define RK1000_ADC_STAT_ADC1_UNDERFLOW	BIT(3)
#define RK1000_ADC_STAT_ADC1_OVERFLOW	BIT(2)
#define RK1000_ADC_STAT_ADC0_UNDERFLOW	BIT(1)
#define RK1000_ADC_STAT_ADC0_OVERFLOW	BIT(0)

/* tv-encoder registers */
#define RK1000_TVE_VFCR			0x00
#define RK1000_TVE_VINCR		0x01
#define RK1000_TVE_VOUTCR		0x02
#define RK1000_TVE_POWCR		0x03
#define RK1000_TVE_SRESET		0x04
#define RK1000_TVE_HDTVCR		0x05
#define RK1000_TVE_YADJCR		0x06
#define RK1000_TVE_CBADJCR		0x07
#define RK1000_TVE_CRADJCR		0x08
#define RK1000_TVE_HVER			0x0f

struct rk1000_tve;

struct rk1000 {
	struct i2c_client *client_core;
	struct i2c_client *client_tve;
	struct i2c_client *client_codec;
	struct device *dev;

	struct regmap *regmap_core;
	struct regmap *regmap_tve;
	struct regmap *regmap_codec;

	struct gpio_desc *gpio_rst;

	struct rk1000_tve *tve;
};

#endif /* __LINUX_MFD_RK1000_H */
