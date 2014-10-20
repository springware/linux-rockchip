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
#define RK1000_TVE_VFCR_SUBC_NORST	BIT(6)
#define RK1000_TVE_VFCR_VIN_RANGE_1_254	BIT(3)
#define RK1000_TVE_VFCR_BLACK_0IRE	BIT(2)
#define RK1000_TVE_VFCR_TVF_NTSC_M	(0 << 0)
#define RK1000_TVE_VFCR_TVF_PAL_M	(1 << 0)
#define RK1000_TVE_VFCR_TVF_PAL_BDGHIN	(2 << 0)
#define RK1000_TVE_VFCR_TVF_PAL_NC	(3 << 0)
#define RK1000_TVE_VFCR_TVF_MASK	0x3

#define RK1000_TVE_VINCR		0x01
#define RK1000_TVE_VINCR_VIN_DLY_MASK	(3 << 5)
#define RK1000_TVE_VINCR_VIN_DLY(x)	(x << 5)
#define RK1000_TVE_VINCR_HS_POL_RISING	BIT(4)
#define RK1000_TVE_VINCR_VS_POL_RISING	BIT(3)
#define RK1000_TVE_VINCR_VINF_BT601_SL	(0 << 1)
#define RK1000_TVE_VINCR_VINF_BT656	(1 << 1)
#define RK1000_TVE_VINCR_VINF_BT601_MS	(2 << 1)
#define RK1000_TVE_VINCR_VINF_COLORBAR	(3 << 1)
#define RK1000_TVE_VINCR_VINF_MASK	0x3
#define RK1000_TVE_VINCR_FIELD_MODE	BIT(0)

#define RK1000_TVE_VOUTCR		0x02
#define RK1000_TVE_VOUTCR_VOF_YCBCR	BIT(6)
#define RK1000_TVE_VOUTCR_BLUE		BIT(5)
#define RK1000_TVE_VOUTCR_BLACK		BIT(4)
#define RK1000_TVE_VOUTCR_CVBS_NOCOLOR	BIT(3)
#define RK1000_TVE_VOUTCR_LUMA_MASK	0x7
#define RK1000_TVE_VOUTCR_LUMA_CYCLMASK	0x3
#define RK1000_TVE_VOUTCR_LUMA_PRECED	BIT(2)

/*
 * Precedence (< 0) or delay (> 0) of luma relative to chroma timing.
 * @dly: delay or precedence in clki cycles
 */
static inline int rk1000_tve_luma_timing(int dly)
{
	if (dly >= 0)
		return (dly >> 1) & RK1000_TVE_VOUTCR_LUMA_CYCLMASK;
	else
		return ((dly * (-1)) >> 1) & RK1000_TVE_VOUTCR_LUMA_CYCLMASK) |
		       RK1000_TVE_VOUTCR_LUMA_PRECED;
}

#define RK1000_TVE_POWCR		0x03
#define RK1000_TVE_POWCR_CLKI_P_INVERSE	BIT(4)
#define RK1000_TVE_POWCR_DACCLK_P_SAME	BIT(3)
#define RK1000_TVE_POWCR_DAC_CVBS_PD	BIT(2)
#define RK1000_TVE_POWCR_DAC_Y_PD	BIT(1)
#define RK1000_TVE_POWCR_DAC_C_PD	BIT(0)

#define RK1000_TVE_SRESET		0x04
#define RK1000_TVE_SRESET_SRESET	BIT(0)

#define RK1000_TVE_HDCR			0x05
#define RK1000_TVE_HDCR_RGB2YC_REC709	BIT(4)
#define RK1000_TVE_HDCR_INPUT_YUV	BIT(3)
#define RK1000_TVE_HDCR_720P_60HZ	BIT(2)
#define RK1000_TVE_HDCR_MODE_MASK	0x3
#define RK1000_TVE_HDCR_MODE_720P	(3 << 0)
#define RK1000_TVE_HDCR_MODE_480P	(2 << 0)
#define RK1000_TVE_HDCR_MODE_576P	(1 << 0)
#define RK1000_TVE_HDCR_MODE_INTERLACE	(0 << 0)

#define RK1000_TVE_YADJCR		0x06
#define RK1000_TVE_YADJCR_MASK		0x1f

#define RK1000_TVE_CBADJCR		0x07
#define RK1000_TVE_CBADJCR_MASK		0x1f

#define RK1000_TVE_CRADJCR		0x08
#define RK1000_TVE_CRADJCR_MASK		0x1f

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
