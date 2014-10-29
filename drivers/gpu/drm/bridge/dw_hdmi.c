/*
 * Copyright (C) 2011-2013 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * SH-Mobile High-Definition Multimedia Interface (HDMI) driver
 * for SLISHDMI13T and SLIPHDMIT IP cores
 *
 * Copyright (C) 2010, Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 */

#include <linux/component.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/hdmi.h>
#include <linux/of_device.h>
#include <drm/drm_of.h>
#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_encoder_slave.h>
#include <drm/bridge/dw_hdmi.h>

#include "dw_hdmi.h"

#define RGB			0
#define YCBCR444		1
#define YCBCR422_16BITS		2
#define YCBCR422_8BITS		3
#define XVYCC444		4

enum hdmi_datamap {
	RGB444_8B = 0x01,
	RGB444_10B = 0x03,
	RGB444_12B = 0x05,
	RGB444_16B = 0x07,
	YCbCr444_8B = 0x09,
	YCbCr444_10B = 0x0B,
	YCbCr444_12B = 0x0D,
	YCbCr444_16B = 0x0F,
	YCbCr422_8B = 0x16,
	YCbCr422_10B = 0x14,
	YCbCr422_12B = 0x12,
};

static const u16 csc_coeff_default[3][4] = {
	{ 0x2000, 0x0000, 0x0000, 0x0000 },
	{ 0x0000, 0x2000, 0x0000, 0x0000 },
	{ 0x0000, 0x0000, 0x2000, 0x0000 }
};

static const u16 csc_coeff_rgb_out_eitu601[3][4] = {
	{ 0x2000, 0x6926, 0x74fd, 0x010e },
	{ 0x2000, 0x2cdd, 0x0000, 0x7e9a },
	{ 0x2000, 0x0000, 0x38b4, 0x7e3b }
};

static const u16 csc_coeff_rgb_out_eitu709[3][4] = {
	{ 0x2000, 0x7106, 0x7a02, 0x00a7 },
	{ 0x2000, 0x3264, 0x0000, 0x7e6d },
	{ 0x2000, 0x0000, 0x3b61, 0x7e25 }
};

static const u16 csc_coeff_rgb_in_eitu601[3][4] = {
	{ 0x2591, 0x1322, 0x074b, 0x0000 },
	{ 0x6535, 0x2000, 0x7acc, 0x0200 },
	{ 0x6acd, 0x7534, 0x2000, 0x0200 }
};

static const u16 csc_coeff_rgb_in_eitu709[3][4] = {
	{ 0x2dc5, 0x0d9b, 0x049e, 0x0000 },
	{ 0x62f0, 0x2000, 0x7d11, 0x0200 },
	{ 0x6756, 0x78ab, 0x2000, 0x0200 }
};

/*On rockchip platform, no-word access to the hdmi
 * register will causes an imprecise external abort
 */
static inline void hdmi_writel(struct dw_hdmi *hdmi, u32 val, int offset)
{
	writel(val, hdmi->regs + (offset << 2));
}

static inline u32 hdmi_readl(struct dw_hdmi *hdmi, int offset)
{
	return readl(hdmi->regs + (offset << 2));
}

static void hdmi_modl(struct dw_hdmi *hdmi, u32 data, u32 mask, unsigned reg)
{
	u32 val = hdmi_readl(hdmi, reg) & ~mask;

	val |= data & mask;
	hdmi_writel(hdmi, val, reg);
}

static void hdmi_mask_writel(struct dw_hdmi *hdmi, u32 data, unsigned int reg,
			     u32 shift, u32 mask)
{
	hdmi_modl(hdmi, data << shift, mask, reg);
}

static inline void hdmi_writeb(struct dw_hdmi *hdmi, u32 val, int offset)
{
	writeb(val, hdmi->regs + offset);
}

static inline u32 hdmi_readb(struct dw_hdmi *hdmi, int offset)
{
	return readb(hdmi->regs + offset);
}

static void hdmi_modb(struct dw_hdmi *hdmi, u32 data, u32 mask, unsigned reg)
{
	u8 val = hdmi_readb(hdmi, reg) & ~mask;

	val |= data & mask;
	hdmi_writeb(hdmi, val, reg);
}

static void hdmi_mask_writeb(struct dw_hdmi *hdmi, u32 data, unsigned int reg,
		      u32 shift, u32 mask)
{
	hdmi_modb(hdmi, data << shift, mask, reg);
}

static void hdmi_set_clock_regenerator_n(struct dw_hdmi *hdmi,
					 unsigned int value)
{
	hdmi->write(hdmi, value & 0xff, HDMI_AUD_N1);
	hdmi->write(hdmi, (value >> 8) & 0xff, HDMI_AUD_N2);
	hdmi->write(hdmi, (value >> 16) & 0x0f, HDMI_AUD_N3);

	/* nshift factor = 0 */
	hdmi->mod(hdmi, 0, HDMI_AUD_CTS3_N_SHIFT_MASK, HDMI_AUD_CTS3);
}

static void hdmi_regenerate_cts(struct dw_hdmi *hdmi, unsigned int cts)
{
	/* Must be set/cleared first */
	hdmi->mod(hdmi, 0, HDMI_AUD_CTS3_CTS_MANUAL, HDMI_AUD_CTS3);

	hdmi->write(hdmi, cts & 0xff, HDMI_AUD_CTS1);
	hdmi->write(hdmi, (cts >> 8) & 0xff, HDMI_AUD_CTS2);
	hdmi->write(hdmi, ((cts >> 16) & HDMI_AUD_CTS3_AUDCTS19_16_MASK) |
		    HDMI_AUD_CTS3_CTS_MANUAL, HDMI_AUD_CTS3);
}

static unsigned int hdmi_compute_n(unsigned int freq, unsigned long pixel_clk,
				   unsigned int ratio)
{
	unsigned int n = (128 * freq) / 1000;

	switch (freq) {
	case 32000:
		if (pixel_clk == 25170000)
			n = (ratio == 150) ? 9152 : 4576;
		else if (pixel_clk == 27020000)
			n = (ratio == 150) ? 8192 : 4096;
		else if (pixel_clk == 74170000 || pixel_clk == 148350000)
			n = 11648;
		else
			n = 4096;
		break;

	case 44100:
		if (pixel_clk == 25170000)
			n = 7007;
		else if (pixel_clk == 74170000)
			n = 17836;
		else if (pixel_clk == 148350000)
			n = (ratio == 150) ? 17836 : 8918;
		else
			n = 6272;
		break;

	case 48000:
		if (pixel_clk == 25170000)
			n = (ratio == 150) ? 9152 : 6864;
		else if (pixel_clk == 27020000)
			n = (ratio == 150) ? 8192 : 6144;
		else if (pixel_clk == 74170000)
			n = 11648;
		else if (pixel_clk == 148350000)
			n = (ratio == 150) ? 11648 : 5824;
		else
			n = 6144;
		break;

	case 88200:
		n = hdmi_compute_n(44100, pixel_clk, ratio) * 2;
		break;

	case 96000:
		n = hdmi_compute_n(48000, pixel_clk, ratio) * 2;
		break;

	case 176400:
		n = hdmi_compute_n(44100, pixel_clk, ratio) * 4;
		break;

	case 192000:
		n = hdmi_compute_n(48000, pixel_clk, ratio) * 4;
		break;

	default:
		break;
	}

	return n;
}

static unsigned int hdmi_compute_cts(unsigned int freq, unsigned long pixel_clk,
				     unsigned int ratio)
{
	unsigned int cts = 0;

	pr_debug("%s: freq: %d pixel_clk: %ld ratio: %d\n", __func__, freq,
		 pixel_clk, ratio);

	switch (freq) {
	case 32000:
		if (pixel_clk == 297000000) {
			cts = 222750;
			break;
		}
	case 48000:
	case 96000:
	case 192000:
		switch (pixel_clk) {
		case 25200000:
		case 27000000:
		case 54000000:
		case 74250000:
		case 148500000:
			cts = pixel_clk / 1000;
			break;
		case 297000000:
			cts = 247500;
			break;
		/*
		 * All other TMDS clocks are not supported by
		 * DWC_hdmi_tx. The TMDS clocks divided or
		 * multiplied by 1,001 coefficients are not
		 * supported.
		 */
		default:
			break;
		}
		break;
	case 44100:
	case 88200:
	case 176400:
		switch (pixel_clk) {
		case 25200000:
			cts = 28000;
			break;
		case 27000000:
			cts = 30000;
			break;
		case 54000000:
			cts = 60000;
			break;
		case 74250000:
			cts = 82500;
			break;
		case 148500000:
			cts = 165000;
			break;
		case 297000000:
			cts = 247500;
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
	if (ratio == 100)
		return cts;
	else
		return (cts * ratio) / 100;
}

static void hdmi_set_clk_regenerator(struct dw_hdmi *hdmi,
	unsigned long pixel_clk)
{
	unsigned int clk_n, clk_cts;

	clk_n = hdmi_compute_n(hdmi->sample_rate, pixel_clk,
			       hdmi->ratio);
	clk_cts = hdmi_compute_cts(hdmi->sample_rate, pixel_clk,
				   hdmi->ratio);

	if (!clk_cts) {
		dev_dbg(hdmi->dev, "%s: pixel clock not supported: %lu\n",
			__func__, pixel_clk);
		return;
	}

	dev_dbg(hdmi->dev, "%s: samplerate=%d  ratio=%d  pixelclk=%lu  N=%d cts=%d\n",
		__func__, hdmi->sample_rate, hdmi->ratio,
		pixel_clk, clk_n, clk_cts);

	hdmi_set_clock_regenerator_n(hdmi, clk_n);
	hdmi_regenerate_cts(hdmi, clk_cts);
}

static void hdmi_init_clk_regenerator(struct dw_hdmi *hdmi)
{
	hdmi_set_clk_regenerator(hdmi, 74250000);
}

static void hdmi_clk_regenerator_update_pixel_clock(struct dw_hdmi *hdmi)
{
	hdmi_set_clk_regenerator(hdmi, hdmi->hdmi_data.video_mode.mpixelclock);
}

/*
 * this submodule is responsible for the video data synchronization.
 * for example, for RGB 4:4:4 input, the data map is defined as
 *			pin{47~40} <==> R[7:0]
 *			pin{31~24} <==> G[7:0]
 *			pin{15~8}  <==> B[7:0]
 */
static void hdmi_video_sample(struct dw_hdmi *hdmi)
{
	int color_format = 0;
	u8 val;

	if (hdmi->hdmi_data.enc_in_format == RGB) {
		if (hdmi->hdmi_data.enc_color_depth == 8)
			color_format = 0x01;
		else if (hdmi->hdmi_data.enc_color_depth == 10)
			color_format = 0x03;
		else if (hdmi->hdmi_data.enc_color_depth == 12)
			color_format = 0x05;
		else if (hdmi->hdmi_data.enc_color_depth == 16)
			color_format = 0x07;
		else
			return;
	} else if (hdmi->hdmi_data.enc_in_format == YCBCR444) {
		if (hdmi->hdmi_data.enc_color_depth == 8)
			color_format = 0x09;
		else if (hdmi->hdmi_data.enc_color_depth == 10)
			color_format = 0x0B;
		else if (hdmi->hdmi_data.enc_color_depth == 12)
			color_format = 0x0D;
		else if (hdmi->hdmi_data.enc_color_depth == 16)
			color_format = 0x0F;
		else
			return;
	} else if (hdmi->hdmi_data.enc_in_format == YCBCR422_8BITS) {
		if (hdmi->hdmi_data.enc_color_depth == 8)
			color_format = 0x16;
		else if (hdmi->hdmi_data.enc_color_depth == 10)
			color_format = 0x14;
		else if (hdmi->hdmi_data.enc_color_depth == 12)
			color_format = 0x12;
		else
			return;
	}

	val = HDMI_TX_INVID0_INTERNAL_DE_GENERATOR_DISABLE |
		((color_format << HDMI_TX_INVID0_VIDEO_MAPPING_OFFSET) &
		HDMI_TX_INVID0_VIDEO_MAPPING_MASK);
	hdmi->write(hdmi, val, HDMI_TX_INVID0);

	/* Enable TX stuffing: When DE is inactive, fix the output data to 0 */
	val = HDMI_TX_INSTUFFING_BDBDATA_STUFFING_ENABLE |
		HDMI_TX_INSTUFFING_RCRDATA_STUFFING_ENABLE |
		HDMI_TX_INSTUFFING_GYDATA_STUFFING_ENABLE;
	hdmi->write(hdmi, val, HDMI_TX_INSTUFFING);
	hdmi->write(hdmi, 0x0, HDMI_TX_GYDATA0);
	hdmi->write(hdmi, 0x0, HDMI_TX_GYDATA1);
	hdmi->write(hdmi, 0x0, HDMI_TX_RCRDATA0);
	hdmi->write(hdmi, 0x0, HDMI_TX_RCRDATA1);
	hdmi->write(hdmi, 0x0, HDMI_TX_BCBDATA0);
	hdmi->write(hdmi, 0x0, HDMI_TX_BCBDATA1);
}

static int is_color_space_conversion(struct dw_hdmi *hdmi)
{
	return hdmi->hdmi_data.enc_in_format != hdmi->hdmi_data.enc_out_format;
}

static int is_color_space_decimation(struct dw_hdmi *hdmi)
{
	if (hdmi->hdmi_data.enc_out_format != YCBCR422_8BITS)
		return 0;
	if (hdmi->hdmi_data.enc_in_format == RGB ||
	    hdmi->hdmi_data.enc_in_format == YCBCR444)
		return 1;
	return 0;
}

static int is_color_space_interpolation(struct dw_hdmi *hdmi)
{
	if (hdmi->hdmi_data.enc_in_format != YCBCR422_8BITS)
		return 0;
	if (hdmi->hdmi_data.enc_out_format == RGB ||
	    hdmi->hdmi_data.enc_out_format == YCBCR444)
		return 1;
	return 0;
}

static void dw_hdmi_update_csc_coeffs(struct dw_hdmi *hdmi)
{
	const u16 (*csc_coeff)[3][4] = &csc_coeff_default;
	unsigned i;
	u32 csc_scale = 1;

	if (is_color_space_conversion(hdmi)) {
		if (hdmi->hdmi_data.enc_out_format == RGB) {
			if (hdmi->hdmi_data.colorimetry ==
					HDMI_COLORIMETRY_ITU_601)
				csc_coeff = &csc_coeff_rgb_out_eitu601;
			else
				csc_coeff = &csc_coeff_rgb_out_eitu709;
		} else if (hdmi->hdmi_data.enc_in_format == RGB) {
			if (hdmi->hdmi_data.colorimetry ==
					HDMI_COLORIMETRY_ITU_601)
				csc_coeff = &csc_coeff_rgb_in_eitu601;
			else
				csc_coeff = &csc_coeff_rgb_in_eitu709;
			csc_scale = 0;
		}
	}

	/* The CSC registers are sequential, alternating MSB then LSB */
	for (i = 0; i < ARRAY_SIZE(csc_coeff_default[0]); i++) {
		u16 coeff_a = (*csc_coeff)[0][i];
		u16 coeff_b = (*csc_coeff)[1][i];
		u16 coeff_c = (*csc_coeff)[2][i];

		hdmi->write(hdmi, coeff_a & 0xff,
			HDMI_CSC_COEF_A1_LSB + i * 2);
		hdmi->write(hdmi, coeff_a >> 8, HDMI_CSC_COEF_A1_MSB + i * 2);
		hdmi->write(hdmi, coeff_b & 0xff, HDMI_CSC_COEF_B1_LSB + i * 2);
		hdmi->write(hdmi, coeff_b >> 8, HDMI_CSC_COEF_B1_MSB + i * 2);
		hdmi->write(hdmi, coeff_c & 0xff,
			HDMI_CSC_COEF_C1_LSB + i * 2);
		hdmi->write(hdmi, coeff_c >> 8, HDMI_CSC_COEF_C1_MSB + i * 2);
	}

	hdmi->mod(hdmi, csc_scale, HDMI_CSC_SCALE_CSCSCALE_MASK,
		  HDMI_CSC_SCALE);
}

static void hdmi_video_csc(struct dw_hdmi *hdmi)
{
	int color_depth = 0;
	int interpolation = HDMI_CSC_CFG_INTMODE_DISABLE;
	int decimation = 0;

	/* YCC422 interpolation to 444 mode */
	if (is_color_space_interpolation(hdmi))
		interpolation = HDMI_CSC_CFG_INTMODE_CHROMA_INT_FORMULA1;
	else if (is_color_space_decimation(hdmi))
		decimation = HDMI_CSC_CFG_DECMODE_CHROMA_INT_FORMULA3;

	if (hdmi->hdmi_data.enc_color_depth == 8)
		color_depth = HDMI_CSC_SCALE_CSC_COLORDE_PTH_24BPP;
	else if (hdmi->hdmi_data.enc_color_depth == 10)
		color_depth = HDMI_CSC_SCALE_CSC_COLORDE_PTH_30BPP;
	else if (hdmi->hdmi_data.enc_color_depth == 12)
		color_depth = HDMI_CSC_SCALE_CSC_COLORDE_PTH_36BPP;
	else if (hdmi->hdmi_data.enc_color_depth == 16)
		color_depth = HDMI_CSC_SCALE_CSC_COLORDE_PTH_48BPP;
	else
		return;

	/* Configure the CSC registers */
	hdmi->write(hdmi, interpolation | decimation, HDMI_CSC_CFG);
	hdmi->mod(hdmi, color_depth, HDMI_CSC_SCALE_CSC_COLORDE_PTH_MASK,
		  HDMI_CSC_SCALE);

	dw_hdmi_update_csc_coeffs(hdmi);
}

/*
 * HDMI video packetizer is used to packetize the data.
 * for example, if input is YCC422 mode or repeater is used,
 * data should be repacked this module can be bypassed.
 */
static void hdmi_video_packetize(struct dw_hdmi *hdmi)
{
	unsigned int color_depth = 0;
	unsigned int remap_size = HDMI_VP_REMAP_YCC422_16bit;
	unsigned int output_select = HDMI_VP_CONF_OUTPUT_SELECTOR_PP;
	struct hdmi_data_info *hdmi_data = &hdmi->hdmi_data;
	u8 val, vp_conf;

	if (hdmi_data->enc_out_format == RGB ||
	    hdmi_data->enc_out_format == YCBCR444) {
		if (!hdmi_data->enc_color_depth) {
			output_select = HDMI_VP_CONF_OUTPUT_SELECTOR_BYPASS;
		} else if (hdmi_data->enc_color_depth == 8) {
			color_depth = 4;
			output_select = HDMI_VP_CONF_OUTPUT_SELECTOR_BYPASS;
		} else if (hdmi_data->enc_color_depth == 10) {
			color_depth = 5;
		} else if (hdmi_data->enc_color_depth == 12) {
			color_depth = 6;
		} else if (hdmi_data->enc_color_depth == 16) {
			color_depth = 7;
		} else {
			return;
		}
	} else if (hdmi_data->enc_out_format == YCBCR422_8BITS) {
		if (!hdmi_data->enc_color_depth ||
		    hdmi_data->enc_color_depth == 8)
			remap_size = HDMI_VP_REMAP_YCC422_16bit;
		else if (hdmi_data->enc_color_depth == 10)
			remap_size = HDMI_VP_REMAP_YCC422_20bit;
		else if (hdmi_data->enc_color_depth == 12)
			remap_size = HDMI_VP_REMAP_YCC422_24bit;
		else
			return;
		output_select = HDMI_VP_CONF_OUTPUT_SELECTOR_YCC422;
	} else {
		return;
	}

	/* set the packetizer registers */
	val = ((color_depth << HDMI_VP_PR_CD_COLOR_DEPTH_OFFSET) &
		HDMI_VP_PR_CD_COLOR_DEPTH_MASK) |
		((hdmi_data->pix_repet_factor <<
		HDMI_VP_PR_CD_DESIRED_PR_FACTOR_OFFSET) &
		HDMI_VP_PR_CD_DESIRED_PR_FACTOR_MASK);
	hdmi->write(hdmi, val, HDMI_VP_PR_CD);

	hdmi->mod(hdmi, HDMI_VP_STUFF_PR_STUFFING_STUFFING_MODE,
		  HDMI_VP_STUFF_PR_STUFFING_MASK, HDMI_VP_STUFF);

	/* Data from pixel repeater block */
	if (hdmi_data->pix_repet_factor > 1) {
		vp_conf = HDMI_VP_CONF_PR_EN_ENABLE |
			  HDMI_VP_CONF_BYPASS_SELECT_PIX_REPEATER;
	} else { /* data from packetizer block */
		vp_conf = HDMI_VP_CONF_PR_EN_DISABLE |
			  HDMI_VP_CONF_BYPASS_SELECT_VID_PACKETIZER;
	}

	hdmi->mod(hdmi, vp_conf,
		  HDMI_VP_CONF_PR_EN_MASK |
		  HDMI_VP_CONF_BYPASS_SELECT_MASK, HDMI_VP_CONF);

	hdmi->mod(hdmi, 1 << HDMI_VP_STUFF_IDEFAULT_PHASE_OFFSET,
		  HDMI_VP_STUFF_IDEFAULT_PHASE_MASK, HDMI_VP_STUFF);

	hdmi->write(hdmi, remap_size, HDMI_VP_REMAP);

	if (output_select == HDMI_VP_CONF_OUTPUT_SELECTOR_PP) {
		vp_conf = HDMI_VP_CONF_BYPASS_EN_DISABLE |
			  HDMI_VP_CONF_PP_EN_ENABLE |
			  HDMI_VP_CONF_YCC422_EN_DISABLE;
	} else if (output_select == HDMI_VP_CONF_OUTPUT_SELECTOR_YCC422) {
		vp_conf = HDMI_VP_CONF_BYPASS_EN_DISABLE |
			  HDMI_VP_CONF_PP_EN_DISABLE |
			  HDMI_VP_CONF_YCC422_EN_ENABLE;
	} else if (output_select == HDMI_VP_CONF_OUTPUT_SELECTOR_BYPASS) {
		vp_conf = HDMI_VP_CONF_BYPASS_EN_ENABLE |
			  HDMI_VP_CONF_PP_EN_DISABLE |
			  HDMI_VP_CONF_YCC422_EN_DISABLE;
	} else {
		return;
	}

	hdmi->mod(hdmi, vp_conf,
		  HDMI_VP_CONF_BYPASS_EN_MASK | HDMI_VP_CONF_PP_EN_ENMASK |
		  HDMI_VP_CONF_YCC422_EN_MASK, HDMI_VP_CONF);

	hdmi->mod(hdmi, HDMI_VP_STUFF_PP_STUFFING_STUFFING_MODE |
			HDMI_VP_STUFF_YCC422_STUFFING_STUFFING_MODE,
		  HDMI_VP_STUFF_PP_STUFFING_MASK |
		  HDMI_VP_STUFF_YCC422_STUFFING_MASK, HDMI_VP_STUFF);

	hdmi->mod(hdmi, output_select, HDMI_VP_CONF_OUTPUT_SELECTOR_MASK,
		  HDMI_VP_CONF);
}

static inline void hdmi_phy_test_clear(struct dw_hdmi *hdmi,
						unsigned char bit)
{
	hdmi->mod(hdmi, bit << HDMI_PHY_TST0_TSTCLR_OFFSET,
		  HDMI_PHY_TST0_TSTCLR_MASK, HDMI_PHY_TST0);
}

static inline void hdmi_phy_test_enable(struct dw_hdmi *hdmi,
						unsigned char bit)
{
	hdmi->mod(hdmi, bit << HDMI_PHY_TST0_TSTEN_OFFSET,
		  HDMI_PHY_TST0_TSTEN_MASK, HDMI_PHY_TST0);
}

static inline void hdmi_phy_test_clock(struct dw_hdmi *hdmi,
						unsigned char bit)
{
	hdmi->mod(hdmi, bit << HDMI_PHY_TST0_TSTCLK_OFFSET,
		  HDMI_PHY_TST0_TSTCLK_MASK, HDMI_PHY_TST0);
}

static inline void hdmi_phy_test_din(struct dw_hdmi *hdmi,
						unsigned char bit)
{
	hdmi->write(hdmi, bit, HDMI_PHY_TST1);
}

static inline void hdmi_phy_test_dout(struct dw_hdmi *hdmi,
						unsigned char bit)
{
	hdmi->write(hdmi, bit, HDMI_PHY_TST2);
}

static bool hdmi_phy_wait_i2c_done(struct dw_hdmi *hdmi, int msec)
{
	u32 val;

	while (((val = hdmi->read(hdmi, HDMI_IH_I2CMPHY_STAT0)) & 0x3) == 0) {
		if (msec-- == 0)
			return false;
		udelay(1000);
	}
	hdmi->write(hdmi, val, HDMI_IH_I2CMPHY_STAT0);

	return true;
}

static void __hdmi_phy_i2c_write(struct dw_hdmi *hdmi, unsigned short data,
			      unsigned char addr)
{
	hdmi->write(hdmi, 0xFF, HDMI_IH_I2CMPHY_STAT0);
	hdmi->write(hdmi, addr, HDMI_PHY_I2CM_ADDRESS_ADDR);
	hdmi->write(hdmi, (unsigned char)(data >> 8),
		HDMI_PHY_I2CM_DATAO_1_ADDR);
	hdmi->write(hdmi, (unsigned char)(data >> 0),
		HDMI_PHY_I2CM_DATAO_0_ADDR);
	hdmi->write(hdmi, HDMI_PHY_I2CM_OPERATION_ADDR_WRITE,
		HDMI_PHY_I2CM_OPERATION_ADDR);
	if (hdmi_phy_wait_i2c_done(hdmi, 1000) == false)
		dev_err(hdmi->dev, "hdmi phy write failed\n");
}

static int hdmi_phy_i2c_write(struct dw_hdmi *hdmi, unsigned short data,
				     unsigned char addr)
{
	__hdmi_phy_i2c_write(hdmi, data, addr);
	return 0;
}

static void dw_hdmi_phy_enable_power(struct dw_hdmi *hdmi, u8 enable)
{
	hdmi->mask_write(hdmi, enable, HDMI_PHY_CONF0,
			 HDMI_PHY_CONF0_PDZ_OFFSET,
			 HDMI_PHY_CONF0_PDZ_MASK);
}

static void dw_hdmi_phy_enable_tmds(struct dw_hdmi *hdmi, u8 enable)
{
	hdmi->mask_write(hdmi, enable, HDMI_PHY_CONF0,
			 HDMI_PHY_CONF0_ENTMDS_OFFSET,
			 HDMI_PHY_CONF0_ENTMDS_MASK);
}

static void dw_hdmi_phy_gen2_pddq(struct dw_hdmi *hdmi, u8 enable)
{
	hdmi->mask_write(hdmi, enable, HDMI_PHY_CONF0,
			 HDMI_PHY_CONF0_GEN2_PDDQ_OFFSET,
			 HDMI_PHY_CONF0_GEN2_PDDQ_MASK);
}

static void dw_hdmi_phy_gen2_txpwron(struct dw_hdmi *hdmi, u8 enable)
{
	hdmi->mask_write(hdmi, enable, HDMI_PHY_CONF0,
			 HDMI_PHY_CONF0_GEN2_TXPWRON_OFFSET,
			 HDMI_PHY_CONF0_GEN2_TXPWRON_MASK);
}

static void dw_hdmi_phy_sel_data_en_pol(struct dw_hdmi *hdmi, u8 enable)
{
	hdmi->mask_write(hdmi, enable, HDMI_PHY_CONF0,
			 HDMI_PHY_CONF0_SELDATAENPOL_OFFSET,
			 HDMI_PHY_CONF0_SELDATAENPOL_MASK);
}

static void dw_hdmi_phy_sel_interface_control(struct dw_hdmi *hdmi, u8 enable)
{
	hdmi->mask_write(hdmi, enable, HDMI_PHY_CONF0,
			 HDMI_PHY_CONF0_SELDIPIF_OFFSET,
			 HDMI_PHY_CONF0_SELDIPIF_MASK);
}

static int hdmi_phy_configure(struct dw_hdmi *hdmi, unsigned char prep,
			      unsigned char res, int cscon)
{
	unsigned res_idx, i;
	u8 val, msec;
	const struct mpll_config *mpll_cfg = hdmi->plat_data->mpll_cfg;
	const struct curr_ctrl   *curr_ctr = hdmi->plat_data->cur_ctr;

	if (prep)
		return -EINVAL;

	switch (res) {
	case 0:	/* color resolution 0 is 8 bit colour depth */
	case 8:
		res_idx = RES_8;
		break;
	case 10:
		res_idx = RES_10;
		break;
	case 12:
		res_idx = RES_12;
		break;
	default:
		return -EINVAL;
	}

	/* Enable csc path */
	if (cscon)
		val = HDMI_MC_FLOWCTRL_FEED_THROUGH_OFF_CSC_IN_PATH;
	else
		val = HDMI_MC_FLOWCTRL_FEED_THROUGH_OFF_CSC_BYPASS;

	hdmi->write(hdmi, val, HDMI_MC_FLOWCTRL);

	/* gen2 tx power off */
	dw_hdmi_phy_gen2_txpwron(hdmi, 0);

	/* gen2 pddq */
	dw_hdmi_phy_gen2_pddq(hdmi, 1);

	/* PHY reset */
	hdmi->write(hdmi, HDMI_MC_PHYRSTZ_DEASSERT, HDMI_MC_PHYRSTZ);
	hdmi->write(hdmi, HDMI_MC_PHYRSTZ_ASSERT, HDMI_MC_PHYRSTZ);

	hdmi->write(hdmi, HDMI_MC_HEACPHY_RST_ASSERT, HDMI_MC_HEACPHY_RST);

	hdmi_phy_test_clear(hdmi, 1);
	hdmi->write(hdmi, HDMI_PHY_I2CM_SLAVE_ADDR_PHY_GEN2,
			HDMI_PHY_I2CM_SLAVE_ADDR);
	hdmi_phy_test_clear(hdmi, 0);

	/* PLL/MPLL Cfg - always match on final entry */
	for (i = 0; mpll_cfg[i].mpixelclock != (~0UL); i++)
		if (hdmi->hdmi_data.video_mode.mpixelclock <=
		    mpll_cfg[i].mpixelclock)
			break;
	hdmi_phy_i2c_write(hdmi, mpll_cfg[i].res[res_idx].cpce, 0x06);
	hdmi_phy_i2c_write(hdmi, mpll_cfg[i].res[res_idx].gmp, 0x15);

	for (i = 0; curr_ctr[i].mpixelclock != (~0UL); i++)
		if (hdmi->hdmi_data.video_mode.mpixelclock <=
		    curr_ctr[i].mpixelclock)
			break;

	if (curr_ctr[i].mpixelclock == (~0UL)) {
		dev_err(hdmi->dev,
			"Pixel clock %d - unsupported by HDMI\n",
			hdmi->hdmi_data.video_mode.mpixelclock);
		return -EINVAL;
	}

	/* CURRCTRL */
	hdmi_phy_i2c_write(hdmi, curr_ctr[i].curr[res_idx], 0x10);

	hdmi_phy_i2c_write(hdmi, 0x0000, 0x13);  /* PLLPHBYCTRL */
	hdmi_phy_i2c_write(hdmi, 0x0006, 0x17);
	/* RESISTANCE TERM 133Ohm Cfg */
	hdmi_phy_i2c_write(hdmi, 0x0005, 0x19);  /* TXTERM */
	/* PREEMP Cgf 0.00 */
	hdmi_phy_i2c_write(hdmi, 0x800d, 0x09);  /* CKSYMTXCTRL */
	/* TX/CK LVL 10 */
	hdmi_phy_i2c_write(hdmi, 0x01ad, 0x0E);  /* VLEVCTRL */
	/* REMOVE CLK TERM */
	hdmi_phy_i2c_write(hdmi, 0x8000, 0x05);  /* CKCALCTRL */
	if (hdmi->dev_type != RK3288_HDMI) {
		dw_hdmi_phy_enable_power(hdmi, 1);

		/* toggle TMDS enable */
		dw_hdmi_phy_enable_tmds(hdmi, 0);
		dw_hdmi_phy_enable_tmds(hdmi, 1);

		/* gen2 tx power on */
		dw_hdmi_phy_gen2_txpwron(hdmi, 1);
		dw_hdmi_phy_gen2_pddq(hdmi, 0);
	} else {
		hdmi->write(hdmi, 0x6e, HDMI_PHY_CONF0);
	}

	/*Wait for PHY PLL lock */
	msec = 5;
	do {
		val = hdmi->read(hdmi, HDMI_PHY_STAT0) & HDMI_PHY_TX_PHY_LOCK;
		if (!val)
			break;

		if (msec == 0) {
			dev_err(hdmi->dev, "PHY PLL not locked\n");
			return -ETIMEDOUT;
		}

		udelay(1000);
		msec--;
	} while (1);

	return 0;
}

static int dw_hdmi_phy_init(struct dw_hdmi *hdmi)
{
	int i, ret;
	bool cscon = false;

	/*check csc whether needed activated in HDMI mode */
	cscon = (is_color_space_conversion(hdmi) &&
			!hdmi->hdmi_data.video_mode.mdvi);

	/* HDMI Phy spec says to do the phy initialization sequence twice */
	for (i = 0; i < 2; i++) {
		dw_hdmi_phy_sel_data_en_pol(hdmi, 1);
		dw_hdmi_phy_sel_interface_control(hdmi, 0);
		dw_hdmi_phy_enable_tmds(hdmi, 0);
		dw_hdmi_phy_enable_power(hdmi, 0);

		/* Enable CSC */
		ret = hdmi_phy_configure(hdmi, 0, 8, cscon);
		if (ret)
			return ret;
	}

	hdmi->phy_enabled = true;
	return 0;
}

static void hdmi_tx_hdcp_config(struct dw_hdmi *hdmi)
{
	u8 de;

	if (hdmi->hdmi_data.video_mode.mdataenablepolarity)
		de = HDMI_A_VIDPOLCFG_DATAENPOL_ACTIVE_HIGH;
	else
		de = HDMI_A_VIDPOLCFG_DATAENPOL_ACTIVE_LOW;

	/* disable rx detect */
	hdmi->mod(hdmi, HDMI_A_HDCPCFG0_RXDETECT_DISABLE,
		  HDMI_A_HDCPCFG0_RXDETECT_MASK, HDMI_A_HDCPCFG0);

	hdmi->mod(hdmi, de, HDMI_A_VIDPOLCFG_DATAENPOL_MASK, HDMI_A_VIDPOLCFG);

	hdmi->mod(hdmi, HDMI_A_HDCPCFG1_ENCRYPTIONDISABLE_DISABLE,
		  HDMI_A_HDCPCFG1_ENCRYPTIONDISABLE_MASK, HDMI_A_HDCPCFG1);
}

static void hdmi_config_AVI(struct dw_hdmi *hdmi)
{
	u8 val, pix_fmt, under_scan;
	u8 act_ratio, coded_ratio, colorimetry, ext_colorimetry;
	bool aspect_16_9;

	aspect_16_9 = false; /* FIXME */

	/* AVI Data Byte 1 */
	if (hdmi->hdmi_data.enc_out_format == YCBCR444)
		pix_fmt = HDMI_FC_AVICONF0_PIX_FMT_YCBCR444;
	else if (hdmi->hdmi_data.enc_out_format == YCBCR422_8BITS)
		pix_fmt = HDMI_FC_AVICONF0_PIX_FMT_YCBCR422;
	else
		pix_fmt = HDMI_FC_AVICONF0_PIX_FMT_RGB;

		under_scan =  HDMI_FC_AVICONF0_SCAN_INFO_NODATA;

	/*
	 * Active format identification data is present in the AVI InfoFrame.
	 * Under scan info, no bar data
	 */
	val = pix_fmt | under_scan |
		HDMI_FC_AVICONF0_ACTIVE_FMT_INFO_PRESENT |
		HDMI_FC_AVICONF0_BAR_DATA_NO_DATA;

	hdmi->write(hdmi, val, HDMI_FC_AVICONF0);

	/* AVI Data Byte 2 -Set the Aspect Ratio */
	if (aspect_16_9) {
		act_ratio = HDMI_FC_AVICONF1_ACTIVE_ASPECT_RATIO_16_9;
		coded_ratio = HDMI_FC_AVICONF1_CODED_ASPECT_RATIO_16_9;
	} else {
		act_ratio = HDMI_FC_AVICONF1_ACTIVE_ASPECT_RATIO_4_3;
		coded_ratio = HDMI_FC_AVICONF1_CODED_ASPECT_RATIO_4_3;
	}

	/* Set up colorimetry */
	if (hdmi->hdmi_data.enc_out_format == XVYCC444) {
		colorimetry = HDMI_FC_AVICONF1_COLORIMETRY_EXTENDED_INFO;
		if (hdmi->hdmi_data.colorimetry == HDMI_COLORIMETRY_ITU_601)
			ext_colorimetry =
				HDMI_FC_AVICONF2_EXT_COLORIMETRY_XVYCC601;
		else /*hdmi->hdmi_data.colorimetry == HDMI_COLORIMETRY_ITU_709*/
			ext_colorimetry =
				HDMI_FC_AVICONF2_EXT_COLORIMETRY_XVYCC709;
	} else if (hdmi->hdmi_data.enc_out_format != RGB) {
		if (hdmi->hdmi_data.colorimetry == HDMI_COLORIMETRY_ITU_601)
			colorimetry = HDMI_FC_AVICONF1_COLORIMETRY_SMPTE;
		else /*hdmi->hdmi_data.colorimetry == HDMI_COLORIMETRY_ITU_709*/
			colorimetry = HDMI_FC_AVICONF1_COLORIMETRY_ITUR;
		ext_colorimetry = HDMI_FC_AVICONF2_EXT_COLORIMETRY_XVYCC601;
	} else { /* Carries no data */
		colorimetry = HDMI_FC_AVICONF1_COLORIMETRY_NO_DATA;
		ext_colorimetry = HDMI_FC_AVICONF2_EXT_COLORIMETRY_XVYCC601;
	}

	val = colorimetry | coded_ratio | act_ratio;
	hdmi->write(hdmi, val, HDMI_FC_AVICONF1);

	/* AVI Data Byte 3 */
	val = HDMI_FC_AVICONF2_IT_CONTENT_NO_DATA | ext_colorimetry |
		HDMI_FC_AVICONF2_RGB_QUANT_DEFAULT |
		HDMI_FC_AVICONF2_SCALING_NONE;
	hdmi->write(hdmi, val, HDMI_FC_AVICONF2);

	/* AVI Data Byte 4 */
	hdmi->write(hdmi, hdmi->vic, HDMI_FC_AVIVID);

	/* AVI Data Byte 5- set up input and output pixel repetition */
	val = (((hdmi->hdmi_data.video_mode.mpixelrepetitioninput + 1) <<
		HDMI_FC_PRCONF_INCOMING_PR_FACTOR_OFFSET) &
		HDMI_FC_PRCONF_INCOMING_PR_FACTOR_MASK) |
		((hdmi->hdmi_data.video_mode.mpixelrepetitionoutput <<
		HDMI_FC_PRCONF_OUTPUT_PR_FACTOR_OFFSET) &
		HDMI_FC_PRCONF_OUTPUT_PR_FACTOR_MASK);
	hdmi->write(hdmi, val, HDMI_FC_PRCONF);

	/* IT Content and quantization range = don't care */
	val = HDMI_FC_AVICONF3_IT_CONTENT_TYPE_GRAPHICS |
		HDMI_FC_AVICONF3_QUANT_RANGE_LIMITED;
	hdmi->write(hdmi, val, HDMI_FC_AVICONF3);

	/* AVI Data Bytes 6-13 */
	hdmi->write(hdmi, 0, HDMI_FC_AVIETB0);
	hdmi->write(hdmi, 0, HDMI_FC_AVIETB1);
	hdmi->write(hdmi, 0, HDMI_FC_AVISBB0);
	hdmi->write(hdmi, 0, HDMI_FC_AVISBB1);
	hdmi->write(hdmi, 0, HDMI_FC_AVIELB0);
	hdmi->write(hdmi, 0, HDMI_FC_AVIELB1);
	hdmi->write(hdmi, 0, HDMI_FC_AVISRB0);
	hdmi->write(hdmi, 0, HDMI_FC_AVISRB1);
}

static void hdmi_av_composer(struct dw_hdmi *hdmi,
			     const struct drm_display_mode *mode)
{
	u8 inv_val;
	struct hdmi_vmode *vmode = &hdmi->hdmi_data.video_mode;
	int hblank, vblank, h_de_hs, v_de_vs, hsync_len, vsync_len;

	vmode->mhsyncpolarity = !!(mode->flags & DRM_MODE_FLAG_PHSYNC);
	vmode->mvsyncpolarity = !!(mode->flags & DRM_MODE_FLAG_PVSYNC);
	vmode->minterlaced = !!(mode->flags & DRM_MODE_FLAG_INTERLACE);
	vmode->mpixelclock = mode->clock * 1000;

	dev_dbg(hdmi->dev, "final pixclk = %d\n", vmode->mpixelclock);

	/* Set up HDMI_FC_INVIDCONF */
	inv_val = (hdmi->hdmi_data.hdcp_enable ?
		HDMI_FC_INVIDCONF_HDCP_KEEPOUT_ACTIVE :
		HDMI_FC_INVIDCONF_HDCP_KEEPOUT_INACTIVE);

	inv_val |= (vmode->mvsyncpolarity ?
		HDMI_FC_INVIDCONF_VSYNC_IN_POLARITY_ACTIVE_HIGH :
		HDMI_FC_INVIDCONF_VSYNC_IN_POLARITY_ACTIVE_LOW);

	inv_val |= (vmode->mhsyncpolarity ?
		HDMI_FC_INVIDCONF_HSYNC_IN_POLARITY_ACTIVE_HIGH :
		HDMI_FC_INVIDCONF_HSYNC_IN_POLARITY_ACTIVE_LOW);

	inv_val |= (vmode->mdataenablepolarity ?
		HDMI_FC_INVIDCONF_DE_IN_POLARITY_ACTIVE_HIGH :
		HDMI_FC_INVIDCONF_DE_IN_POLARITY_ACTIVE_LOW);

	if (hdmi->vic == 39)
		inv_val |= HDMI_FC_INVIDCONF_R_V_BLANK_IN_OSC_ACTIVE_HIGH;
	else
		inv_val |= (vmode->minterlaced ?
			HDMI_FC_INVIDCONF_R_V_BLANK_IN_OSC_ACTIVE_HIGH :
			HDMI_FC_INVIDCONF_R_V_BLANK_IN_OSC_ACTIVE_LOW);

	inv_val |= (vmode->minterlaced ?
		HDMI_FC_INVIDCONF_IN_I_P_INTERLACED :
		HDMI_FC_INVIDCONF_IN_I_P_PROGRESSIVE);

	inv_val |= (vmode->mdvi ?
		HDMI_FC_INVIDCONF_DVI_MODEZ_DVI_MODE :
		HDMI_FC_INVIDCONF_DVI_MODEZ_HDMI_MODE);

	hdmi->write(hdmi, inv_val, HDMI_FC_INVIDCONF);

	/* Set up horizontal active pixel width */
	hdmi->write(hdmi, mode->hdisplay >> 8, HDMI_FC_INHACTV1);
	hdmi->write(hdmi, mode->hdisplay, HDMI_FC_INHACTV0);

	/* Set up vertical active lines */
	hdmi->write(hdmi, mode->vdisplay >> 8, HDMI_FC_INVACTV1);
	hdmi->write(hdmi, mode->vdisplay, HDMI_FC_INVACTV0);

	/* Set up horizontal blanking pixel region width */
	hblank = mode->htotal - mode->hdisplay;
	hdmi->write(hdmi, hblank >> 8, HDMI_FC_INHBLANK1);
	hdmi->write(hdmi, hblank, HDMI_FC_INHBLANK0);

	/* Set up vertical blanking pixel region width */
	vblank = mode->vtotal - mode->vdisplay;
	hdmi->write(hdmi, vblank, HDMI_FC_INVBLANK);

	/* Set up HSYNC active edge delay width (in pixel clks) */
	h_de_hs = mode->hsync_start - mode->hdisplay;
	hdmi->write(hdmi, h_de_hs >> 8, HDMI_FC_HSYNCINDELAY1);
	hdmi->write(hdmi, h_de_hs, HDMI_FC_HSYNCINDELAY0);

	/* Set up VSYNC active edge delay (in lines) */
	v_de_vs = mode->vsync_start - mode->vdisplay;
	hdmi->write(hdmi, v_de_vs, HDMI_FC_VSYNCINDELAY);

	/* Set up HSYNC active pulse width (in pixel clks) */
	hsync_len = mode->hsync_end - mode->hsync_start;
	hdmi->write(hdmi, hsync_len >> 8, HDMI_FC_HSYNCINWIDTH1);
	hdmi->write(hdmi, hsync_len, HDMI_FC_HSYNCINWIDTH0);

	/* Set up VSYNC active edge delay (in lines) */
	vsync_len = mode->vsync_end - mode->vsync_start;
	hdmi->write(hdmi, vsync_len, HDMI_FC_VSYNCINWIDTH);
}

static void dw_hdmi_phy_disable(struct dw_hdmi *hdmi)
{
	if (!hdmi->phy_enabled)
		return;

	dw_hdmi_phy_enable_tmds(hdmi, 0);
	dw_hdmi_phy_enable_power(hdmi, 0);

	hdmi->phy_enabled = false;
}

/* HDMI Initialization Step B.4 */
static void dw_hdmi_enable_video_path(struct dw_hdmi *hdmi)
{
	u8 clkdis;

	/* control period minimum duration */
	hdmi->write(hdmi, 12, HDMI_FC_CTRLDUR);
	hdmi->write(hdmi, 32, HDMI_FC_EXCTRLDUR);
	hdmi->write(hdmi, 1, HDMI_FC_EXCTRLSPAC);

	/* Set to fill TMDS data channels */
	hdmi->write(hdmi, 0x0B, HDMI_FC_CH0PREAM);
	hdmi->write(hdmi, 0x16, HDMI_FC_CH1PREAM);
	hdmi->write(hdmi, 0x21, HDMI_FC_CH2PREAM);

	/* Enable pixel clock and tmds data path */
	clkdis = 0x7F;
	clkdis &= ~HDMI_MC_CLKDIS_PIXELCLK_DISABLE;
	hdmi->write(hdmi, clkdis, HDMI_MC_CLKDIS);

	clkdis &= ~HDMI_MC_CLKDIS_TMDSCLK_DISABLE;
	hdmi->write(hdmi, clkdis, HDMI_MC_CLKDIS);

	/* Enable csc path */
	if (is_color_space_conversion(hdmi)) {
		clkdis &= ~HDMI_MC_CLKDIS_CSCCLK_DISABLE;
		hdmi->write(hdmi, clkdis, HDMI_MC_CLKDIS);
	}
}

static void hdmi_enable_audio_clk(struct dw_hdmi *hdmi)
{
	hdmi->mod(hdmi, 0, HDMI_MC_CLKDIS_AUDCLK_DISABLE, HDMI_MC_CLKDIS);
}

/* Workaround to clear the overflow condition */
static void dw_hdmi_clear_overflow(struct dw_hdmi *hdmi)
{
	int count;
	u8 val;

	/* TMDS software reset */
	hdmi->write(hdmi, (u8)~HDMI_MC_SWRSTZ_TMDSSWRST_REQ, HDMI_MC_SWRSTZ);

	val = hdmi->read(hdmi, HDMI_FC_INVIDCONF);
	if (hdmi->dev_type == IMX6DL_HDMI) {
		hdmi->write(hdmi, val, HDMI_FC_INVIDCONF);
		return;
	}

	for (count = 0; count < 4; count++)
		hdmi->write(hdmi, val, HDMI_FC_INVIDCONF);
}

static void hdmi_enable_overflow_interrupts(struct dw_hdmi *hdmi)
{
	hdmi->write(hdmi, 0, HDMI_FC_MASK2);
	hdmi->write(hdmi, 0, HDMI_IH_MUTE_FC_STAT2);
}

static void hdmi_disable_overflow_interrupts(struct dw_hdmi *hdmi)
{
	hdmi->write(hdmi, HDMI_IH_MUTE_FC_STAT2_OVERFLOW_MASK,
		    HDMI_IH_MUTE_FC_STAT2);
}

static int dw_hdmi_setup(struct dw_hdmi *hdmi, struct drm_display_mode *mode)
{
	int ret;

	hdmi_disable_overflow_interrupts(hdmi);

	hdmi->vic = drm_match_cea_mode(mode);

	if (!hdmi->vic) {
		dev_dbg(hdmi->dev, "Non-CEA mode used in HDMI\n");
		hdmi->hdmi_data.video_mode.mdvi = true;
	} else {
		dev_dbg(hdmi->dev, "CEA mode used vic=%d\n", hdmi->vic);
		hdmi->hdmi_data.video_mode.mdvi = false;
	}

	if ((hdmi->vic == 6) || (hdmi->vic == 7) ||
	    (hdmi->vic == 21) || (hdmi->vic == 22) ||
	    (hdmi->vic == 2) || (hdmi->vic == 3) ||
	    (hdmi->vic == 17) || (hdmi->vic == 18))
		hdmi->hdmi_data.colorimetry = HDMI_COLORIMETRY_ITU_601;
	else
		hdmi->hdmi_data.colorimetry = HDMI_COLORIMETRY_ITU_709;

	if ((hdmi->vic == 10) || (hdmi->vic == 11) ||
	    (hdmi->vic == 12) || (hdmi->vic == 13) ||
	    (hdmi->vic == 14) || (hdmi->vic == 15) ||
	    (hdmi->vic == 25) || (hdmi->vic == 26) ||
	    (hdmi->vic == 27) || (hdmi->vic == 28) ||
	    (hdmi->vic == 29) || (hdmi->vic == 30) ||
	    (hdmi->vic == 35) || (hdmi->vic == 36) ||
	    (hdmi->vic == 37) || (hdmi->vic == 38))
		hdmi->hdmi_data.video_mode.mpixelrepetitionoutput = 1;
	else
		hdmi->hdmi_data.video_mode.mpixelrepetitionoutput = 0;

	hdmi->hdmi_data.video_mode.mpixelrepetitioninput = 0;

	/* TODO: Get input format from IPU (via FB driver interface) */
	hdmi->hdmi_data.enc_in_format = RGB;

	hdmi->hdmi_data.enc_out_format = RGB;

	hdmi->hdmi_data.enc_color_depth = 8;
	hdmi->hdmi_data.pix_repet_factor = 0;
	hdmi->hdmi_data.hdcp_enable = 0;
	hdmi->hdmi_data.video_mode.mdataenablepolarity = true;

	/* HDMI Initialization Step B.1 */
	hdmi_av_composer(hdmi, mode);

	/* HDMI Initializateion Step B.2 */
	ret = dw_hdmi_phy_init(hdmi);
	if (ret)
		return ret;

	/* HDMI Initialization Step B.3 */
	dw_hdmi_enable_video_path(hdmi);

	/* not for DVI mode */
	if (hdmi->hdmi_data.video_mode.mdvi) {
		dev_dbg(hdmi->dev, "%s DVI mode\n", __func__);
	} else {
		dev_dbg(hdmi->dev, "%s CEA mode\n", __func__);

		/* HDMI Initialization Step E - Configure audio */
		hdmi_clk_regenerator_update_pixel_clock(hdmi);
		hdmi_enable_audio_clk(hdmi);

		/* HDMI Initialization Step F - Configure AVI InfoFrame */
		hdmi_config_AVI(hdmi);
	}

	hdmi_video_packetize(hdmi);
	hdmi_video_csc(hdmi);
	hdmi_video_sample(hdmi);
	hdmi_tx_hdcp_config(hdmi);

	dw_hdmi_clear_overflow(hdmi);
	if (hdmi->cable_plugin && !hdmi->hdmi_data.video_mode.mdvi)
		hdmi_enable_overflow_interrupts(hdmi);

	return 0;
}

/* Wait until we are registered to enable interrupts */
static int dw_hdmi_fb_registered(struct dw_hdmi *hdmi)
{
	hdmi->write(hdmi, HDMI_PHY_I2CM_INT_ADDR_DONE_POL,
		    HDMI_PHY_I2CM_INT_ADDR);

	hdmi->write(hdmi, HDMI_PHY_I2CM_CTLINT_ADDR_NAC_POL |
		    HDMI_PHY_I2CM_CTLINT_ADDR_ARBITRATION_POL,
		    HDMI_PHY_I2CM_CTLINT_ADDR);

	/* enable cable hot plug irq */
	hdmi->write(hdmi, (u8)~HDMI_PHY_HPD, HDMI_PHY_MASK0);

	/* Clear Hotplug interrupts */
	hdmi->write(hdmi, HDMI_IH_PHY_STAT0_HPD, HDMI_IH_PHY_STAT0);

	return 0;
}

static void initialize_hdmi_ih_mutes(struct dw_hdmi *hdmi)
{
	u8 ih_mute;

	/*
	 * Boot up defaults are:
	 * HDMI_IH_MUTE   = 0x03 (disabled)
	 * HDMI_IH_MUTE_* = 0x00 (enabled)
	 *
	 * Disable top level interrupt bits in HDMI block
	 */
	ih_mute = hdmi->read(hdmi, HDMI_IH_MUTE) |
		  HDMI_IH_MUTE_MUTE_WAKEUP_INTERRUPT |
		  HDMI_IH_MUTE_MUTE_ALL_INTERRUPT;

	hdmi->write(hdmi, ih_mute, HDMI_IH_MUTE);

	/* by default mask all interrupts */
	hdmi->write(hdmi, 0xff, HDMI_VP_MASK);
	hdmi->write(hdmi, 0xff, HDMI_FC_MASK0);
	hdmi->write(hdmi, 0xff, HDMI_FC_MASK1);
	hdmi->write(hdmi, 0xff, HDMI_FC_MASK2);
	hdmi->write(hdmi, 0xff, HDMI_PHY_MASK0);
	hdmi->write(hdmi, 0xff, HDMI_PHY_I2CM_INT_ADDR);
	hdmi->write(hdmi, 0xff, HDMI_PHY_I2CM_CTLINT_ADDR);
	hdmi->write(hdmi, 0xff, HDMI_AUD_INT);
	hdmi->write(hdmi, 0xff, HDMI_AUD_SPDIFINT);
	hdmi->write(hdmi, 0xff, HDMI_AUD_HBR_MASK);
	hdmi->write(hdmi, 0xff, HDMI_GP_MASK);
	hdmi->write(hdmi, 0xff, HDMI_A_APIINTMSK);
	hdmi->write(hdmi, 0xff, HDMI_CEC_MASK);
	hdmi->write(hdmi, 0xff, HDMI_I2CM_INT);
	hdmi->write(hdmi, 0xff, HDMI_I2CM_CTLINT);

	/* Disable interrupts in the IH_MUTE_* registers */
	hdmi->write(hdmi, 0xff, HDMI_IH_MUTE_FC_STAT0);
	hdmi->write(hdmi, 0xff, HDMI_IH_MUTE_FC_STAT1);
	hdmi->write(hdmi, 0xff, HDMI_IH_MUTE_FC_STAT2);
	hdmi->write(hdmi, 0xff, HDMI_IH_MUTE_AS_STAT0);
	hdmi->write(hdmi, 0xff, HDMI_IH_MUTE_PHY_STAT0);
	hdmi->write(hdmi, 0xff, HDMI_IH_MUTE_I2CM_STAT0);
	hdmi->write(hdmi, 0xff, HDMI_IH_MUTE_CEC_STAT0);
	hdmi->write(hdmi, 0xff, HDMI_IH_MUTE_VP_STAT0);
	hdmi->write(hdmi, 0xff, HDMI_IH_MUTE_I2CMPHY_STAT0);
	hdmi->write(hdmi, 0xff, HDMI_IH_MUTE_AHBDMAAUD_STAT0);

	/* Enable top level interrupt bits in HDMI block */
	ih_mute &= ~(HDMI_IH_MUTE_MUTE_WAKEUP_INTERRUPT |
		    HDMI_IH_MUTE_MUTE_ALL_INTERRUPT);
	hdmi->write(hdmi, ih_mute, HDMI_IH_MUTE);
}

static void dw_hdmi_poweron(struct dw_hdmi *hdmi)
{
	dw_hdmi_setup(hdmi, &hdmi->previous_mode);
}

static void dw_hdmi_poweroff(struct dw_hdmi *hdmi)
{
	dw_hdmi_phy_disable(hdmi);
}

static enum drm_connector_status dw_hdmi_connector_detect(struct drm_connector
							*connector, bool force)
{
	struct dw_hdmi *hdmi = container_of(connector, struct dw_hdmi,
					     connector);

printk("%s: %s\n", __func__, hdmi->read(hdmi, HDMI_PHY_STAT0) & HDMI_PHY_HPD ? "connected" : "disconnected\n");
	return hdmi->read(hdmi, HDMI_PHY_STAT0) & HDMI_PHY_HPD ?
		connector_status_connected : connector_status_disconnected;
}

static int dw_hdmi_connector_get_modes(struct drm_connector *connector)
{
	struct dw_hdmi *hdmi = container_of(connector, struct dw_hdmi,
					     connector);
	struct edid *edid;
	int ret;

printk("%s: start\n", __func__);
	if (!hdmi->ddc)
		return 0;

printk("%s: reading edid\n", __func__);
	edid = drm_get_edid(connector, hdmi->ddc);
	if (edid) {
		dev_info(hdmi->dev, "got edid: width[%d] x height[%d]\n",
			edid->width_cm, edid->height_cm);

		drm_mode_connector_update_edid_property(connector, edid);
		ret = drm_add_edid_modes(connector, edid);
		kfree(edid);
	} else {
		dev_info(hdmi->dev, "failed to get edid\n");
	}

	return 0;
}

static struct drm_encoder *dw_hdmi_connector_best_encoder(struct drm_connector
							   *connector)
{
	struct dw_hdmi *hdmi = container_of(connector, struct dw_hdmi,
					     connector);

	return &hdmi->encoder;
}

static void dw_hdmi_encoder_mode_set(struct drm_encoder *encoder,
			struct drm_display_mode *mode,
			struct drm_display_mode *adjusted_mode)
{
	struct dw_hdmi *hdmi = container_of(encoder, struct dw_hdmi, encoder);

	dw_hdmi_setup(hdmi, mode);

	/* Store the display mode for plugin/DKMS poweron events */
	memcpy(&hdmi->previous_mode, mode, sizeof(hdmi->previous_mode));
}

static bool dw_hdmi_encoder_mode_fixup(struct drm_encoder *encoder,
			const struct drm_display_mode *mode,
			struct drm_display_mode *adjusted_mode)
{
	return true;
}

static void dw_hdmi_encoder_disable(struct drm_encoder *encoder)
{
}

static void dw_hdmi_encoder_dpms(struct drm_encoder *encoder, int mode)
{
	struct dw_hdmi *hdmi = container_of(encoder, struct dw_hdmi, encoder);

	if (mode)
		dw_hdmi_poweroff(hdmi);
	else
		dw_hdmi_poweron(hdmi);
}

static void dw_hdmi_encoder_prepare(struct drm_encoder *encoder)
{
	struct dw_hdmi *hdmi = container_of(encoder, struct dw_hdmi, encoder);
	dw_hdmi_poweroff(hdmi);

	if (hdmi->plat_data->encoder_prepare)
		hdmi->plat_data->encoder_prepare(&hdmi->connector, encoder);
}

static void dw_hdmi_encoder_commit(struct drm_encoder *encoder)
{
	struct dw_hdmi *hdmi = container_of(encoder, struct dw_hdmi, encoder);

	if (hdmi->plat_data->set_crtc_mux)
		hdmi->plat_data->set_crtc_mux(hdmi->priv, encoder);

	dw_hdmi_poweron(hdmi);
}

void dw_hdmi_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static int dw_hdmi_connector_mode_valid(struct drm_connector *connector,
					 struct drm_display_mode *mode)
{
	struct dw_hdmi *hdmi = container_of(connector, struct dw_hdmi,
					     connector);
	if (hdmi->plat_data->mode_valid)
		return hdmi->plat_data->mode_valid(connector, mode);
	else
		return MODE_OK;
}

static struct drm_encoder_funcs dw_hdmi_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static struct drm_encoder_helper_funcs dw_hdmi_encoder_helper_funcs = {
	.dpms = dw_hdmi_encoder_dpms,
	.prepare = dw_hdmi_encoder_prepare,
	.commit = dw_hdmi_encoder_commit,
	.mode_set = dw_hdmi_encoder_mode_set,
	.mode_fixup = dw_hdmi_encoder_mode_fixup,
	.disable = dw_hdmi_encoder_disable,
};

static struct drm_connector_funcs dw_hdmi_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.detect = dw_hdmi_connector_detect,
	.destroy = dw_hdmi_connector_destroy,
};

static struct drm_connector_helper_funcs dw_hdmi_connector_helper_funcs = {
	.get_modes = dw_hdmi_connector_get_modes,
	.mode_valid = dw_hdmi_connector_mode_valid,
	.best_encoder = dw_hdmi_connector_best_encoder,
};

static irqreturn_t dw_hdmi_hardirq(int irq, void *dev_id)
{
	struct dw_hdmi *hdmi = dev_id;
	u8 intr_stat;

	intr_stat = hdmi->read(hdmi, HDMI_IH_PHY_STAT0);
	if (intr_stat)
		hdmi->write(hdmi, ~0, HDMI_IH_MUTE_PHY_STAT0);

	return intr_stat ? IRQ_WAKE_THREAD : IRQ_NONE;
}

static irqreturn_t dw_hdmi_irq(int irq, void *dev_id)
{
	struct dw_hdmi *hdmi = dev_id;
	u8 intr_stat;
	u8 phy_int_pol;

	intr_stat = hdmi->read(hdmi, HDMI_IH_PHY_STAT0);

	phy_int_pol = hdmi->read(hdmi, HDMI_PHY_POL0);

	if (intr_stat & HDMI_IH_PHY_STAT0_HPD) {
		if (phy_int_pol & HDMI_PHY_HPD) {
			dev_dbg(hdmi->dev, "EVENT=plugin\n");

			hdmi->mod(hdmi, 0, HDMI_PHY_HPD, HDMI_PHY_POL0);

			dw_hdmi_poweron(hdmi);
		} else {
			dev_dbg(hdmi->dev, "EVENT=plugout\n");

			hdmi->mod(hdmi, HDMI_PHY_HPD, HDMI_PHY_HPD,
				HDMI_PHY_POL0);

			dw_hdmi_poweroff(hdmi);
		}
		drm_helper_hpd_irq_event(hdmi->connector.dev);
	}

	hdmi->write(hdmi, intr_stat, HDMI_IH_PHY_STAT0);
	hdmi->write(hdmi, ~HDMI_IH_PHY_STAT0_HPD, HDMI_IH_MUTE_PHY_STAT0);

	return IRQ_HANDLED;
}

static int dw_hdmi_register(struct drm_device *drm, struct dw_hdmi *hdmi)
{
	struct drm_encoder *encoder = &hdmi->encoder;
	struct device *dev = hdmi->dev;

	encoder->possible_crtcs = drm_of_find_possible_crtcs(drm, dev->of_node);

	hdmi->connector.polled = DRM_CONNECTOR_POLL_HPD;

	drm_encoder_helper_add(&hdmi->encoder, &dw_hdmi_encoder_helper_funcs);
	drm_encoder_init(drm, &hdmi->encoder, &dw_hdmi_encoder_funcs,
			 DRM_MODE_ENCODER_TMDS);

	drm_connector_helper_add(&hdmi->connector,
				 &dw_hdmi_connector_helper_funcs);
	drm_connector_init(drm, &hdmi->connector, &dw_hdmi_connector_funcs,
			   DRM_MODE_CONNECTOR_HDMIA);

	hdmi->connector.encoder = &hdmi->encoder;

	drm_mode_connector_attach_encoder(&hdmi->connector, &hdmi->encoder);

	return 0;
}

static int dw_hdmi_bind(struct device *dev, struct device *master, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct dw_hdmi *hdmi = platform_get_drvdata(pdev);
	struct drm_device *drm = data;
	struct device_node *np = dev->of_node;
	struct device_node *ddc_node;
	struct resource *iores;
	int ret, irq;
	u32 val;

	if (!of_property_read_u32(np, "reg-io-width", &val)) {
		switch (val) {
		case 4:
			hdmi->write = hdmi_writel;
			hdmi->read = hdmi_readl;
			hdmi->mod = hdmi_modl;
			hdmi->mask_write = hdmi_mask_writel;
			break;
		default:
			hdmi->write = hdmi_writeb;
			hdmi->read = hdmi_readb;
			hdmi->mod = hdmi_modb;
			hdmi->mask_write = hdmi_mask_writeb;
			break;
		}
	} else {
		hdmi->write = hdmi_writeb;
		hdmi->read = hdmi_readb;
		hdmi->mod = hdmi_modb;
		hdmi->mask_write = hdmi_mask_writeb;
	}

	ddc_node = of_parse_phandle(np, "ddc-i2c-bus", 0);
	if (ddc_node) {
		hdmi->ddc = of_find_i2c_adapter_by_node(ddc_node);
		if (!hdmi->ddc) {
			dev_dbg(hdmi->dev, "failed to read ddc node\n");
			of_node_put(ddc_node);
			return -EPROBE_DEFER;
		}

		of_node_put(ddc_node);
	} else {
		dev_dbg(hdmi->dev, "no ddc property found\n");
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_threaded_irq(dev, irq, dw_hdmi_hardirq,
					dw_hdmi_irq, IRQF_SHARED,
					dev_name(dev), hdmi);
	if (ret)
		return ret;

	iores = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	hdmi->regs = devm_ioremap_resource(dev, iores);
	if (IS_ERR(hdmi->regs))
		return PTR_ERR(hdmi->regs);

	if (hdmi->plat_data->setup)
		hdmi->priv = hdmi->plat_data->setup(pdev);
	/* Product and revision IDs */
	dev_info(dev,
		 "Detected HDMI controller 0x%x:0x%x:0x%x:0x%x\n",
		 hdmi->read(hdmi, HDMI_DESIGN_ID),
		 hdmi->read(hdmi, HDMI_REVISION_ID),
		 hdmi->read(hdmi, HDMI_PRODUCT_ID0),
		 hdmi->read(hdmi, HDMI_PRODUCT_ID1));

	initialize_hdmi_ih_mutes(hdmi);

	/*
	 * To prevent overflows in HDMI_IH_FC_STAT2, set the clk regenerator
	 * N and cts values before enabling phy
	 */
	hdmi_init_clk_regenerator(hdmi);

	/*
	 * Configure registers related to HDMI interrupt
	 * generation before registering IRQ.
	 */
	hdmi->write(hdmi, HDMI_PHY_HPD, HDMI_PHY_POL0);

	/* Clear Hotplug interrupts */
	hdmi->write(hdmi, HDMI_IH_PHY_STAT0_HPD, HDMI_IH_PHY_STAT0);

	ret = dw_hdmi_fb_registered(hdmi);
	if (ret)
		return ret;

	ret = dw_hdmi_register(drm, hdmi);
	if (ret)
		return ret;

	/* Unmute interrupts */
	hdmi->write(hdmi, ~HDMI_IH_PHY_STAT0_HPD, HDMI_IH_MUTE_PHY_STAT0);

	dev_set_drvdata(dev, hdmi);

	return 0;
}

static void dw_hdmi_unbind(struct device *dev, struct device *master,
	void *data)
{
	struct dw_hdmi *hdmi = dev_get_drvdata(dev);

	/* Disable all interrupts */
	hdmi->write(hdmi, ~0, HDMI_IH_MUTE_PHY_STAT0);

	hdmi->connector.funcs->destroy(&hdmi->connector);

	hdmi->encoder.funcs->destroy(&hdmi->encoder);
	if (hdmi->plat_data->exit)
		hdmi->plat_data->exit(hdmi->priv);
	i2c_put_adapter(hdmi->ddc);
}

static const struct component_ops hdmi_ops = {
	.bind	= dw_hdmi_bind,
	.unbind	= dw_hdmi_unbind,
};

static int dw_hdmi_platform_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &hdmi_ops);
}

static int dw_hdmi_platform_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &hdmi_ops);
	return 0;
}

int dw_hdmi_pltfm_register(struct platform_device *pdev,
			   const struct dw_hdmi_plat_data *plat_data)
{
	struct dw_hdmi *hdmi;

	hdmi = devm_kzalloc(&pdev->dev, sizeof(*hdmi), GFP_KERNEL);
	if (!hdmi)
		return -ENOMEM;

	hdmi->plat_data = plat_data;
	hdmi->dev = &pdev->dev;
	hdmi->dev_type = plat_data->dev_type;
	hdmi->sample_rate = 48000;
	hdmi->ratio = 100;

	platform_set_drvdata(pdev, hdmi);

	return dw_hdmi_platform_probe(pdev);
}
EXPORT_SYMBOL_GPL(dw_hdmi_pltfm_register);

int dw_hdmi_pltfm_unregister(struct platform_device *pdev)
{
	return dw_hdmi_platform_remove(pdev);
}
EXPORT_SYMBOL_GPL(dw_hdmi_pltfm_unregister);

MODULE_AUTHOR("Sascha Hauer <s.hauer@pengutronix.de>");
MODULE_DESCRIPTION("i.MX6 HDMI transmitter driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:dw-hdmi");
