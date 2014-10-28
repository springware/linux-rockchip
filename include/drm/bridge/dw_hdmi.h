/*
 * Copyright (C) 2011 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __DW_HDMI_H__
#define __DW_HDMI_H__

#include <drm/drmP.h>

#define HDMI_EDID_LEN           512

enum {
	RES_8,
	RES_10,
	RES_12,
	RES_MAX,
};

enum dw_hdmi_devtype {
	IMX6Q_HDMI,
	IMX6DL_HDMI,
	RK32_HDMI,
};

struct mpll_config {
	unsigned long mpixelclock;
	struct {
		u16 cpce;
		u16 gmp;
	} res[RES_MAX];
};

struct curr_ctrl {
	unsigned long mpixelclock;
	u16 curr[RES_MAX];
};

struct hdmi_vmode {
	bool mdvi;
	bool mhsyncpolarity;
	bool mvsyncpolarity;
	bool minterlaced;
	bool mdataenablepolarity;

	unsigned int mpixelclock;
	unsigned int mpixelrepetitioninput;
	unsigned int mpixelrepetitionoutput;
};

struct hdmi_data_info {
	unsigned int enc_in_format;
	unsigned int enc_out_format;
	unsigned int enc_color_depth;
	unsigned int colorimetry;
	unsigned int pix_repet_factor;
	unsigned int hdcp_enable;
	struct hdmi_vmode video_mode;
};


struct dw_hdmi {
	struct drm_connector connector;
	struct drm_encoder encoder;

	enum dw_hdmi_devtype dev_type;
	struct device *dev;

	struct hdmi_data_info hdmi_data;
	const struct dw_hdmi_plat_data *plat_data;
	void *priv;

	int vic;

	u8 edid[HDMI_EDID_LEN];
	bool cable_plugin;

	bool phy_enabled;
	struct drm_display_mode previous_mode;

	struct i2c_adapter *ddc;
	void __iomem *regs;

	unsigned int sample_rate;
	int ratio;

	void (*write)(struct dw_hdmi *hdmi, u32 val, int offset);
	u32 (*read)(struct dw_hdmi *hdmi, int offset);
	void (*mod)(struct dw_hdmi *hdmi, u32 data, u32 mask, unsigned reg);
	void (*mask_write)(struct dw_hdmi *hdmi, u32 data, unsigned int reg,
			      u32 shift, u32 mask);
};

struct dw_hdmi_plat_data {
	void * (*setup)(struct platform_device *pdev);
	void (*exit)(void *priv);
	void (*set_crtc_mux)(void *priv, struct drm_encoder *encoder);
	void (*encoder_prepare)(struct drm_connector *connector,
				struct drm_encoder *encoder);
	enum drm_mode_status (*mode_valid)(struct drm_connector *connector,
					   struct drm_display_mode *mode);
	const struct mpll_config *mpll_cfg;
	const struct curr_ctrl *cur_ctr;
	enum dw_hdmi_devtype dev_type;

};

int dw_hdmi_pltfm_register(struct platform_device *pdev,
			    const struct dw_hdmi_plat_data *plat_data);
int dw_hdmi_pltfm_unregister(struct platform_device *pdev);
#endif /* __IMX_HDMI_H__ */
