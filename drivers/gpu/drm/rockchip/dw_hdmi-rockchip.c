/*
 * Copyright (c) 2014, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <drm/bridge/dw_hdmi.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_vop.h"

#define GRF_SOC_CON6                    0x025c
#define HDMI_SEL_VOP_LIT                (1 << 4)

static const struct mpll_config rk3288_mpll_cfg[] = {
	{
		27000000, {
			{ 0x00b3, 0x0003},
			{ 0x2153, 0x0000},
			{ 0x40f4, 0x0000}
		},
	}, {
		74250000, {
			{ 0x013e, 0x0003},
			{ 0x217e, 0x0002},
			{ 0x4061, 0x0002}
		},
	}, {
		148500000, {
			{ 0x0051, 0x0003},
			{ 0x215d, 0x0003},
			{ 0x4064, 0x0003}
		},
	}, {
		~0UL, {
			{ 0x00a0, 0x000a },
			{ 0x2001, 0x000f },
			{ 0x4002, 0x000f },
		},
	}
};


static const struct curr_ctrl rk3288_cur_ctr[] = {
	/*      pixelclk    bpp8    bpp10   bpp12 */
	{
		27000000,  { 0x0038, 0x0018, 0x0018 },
	}, {
		74250000,  { 0x0038, 0x0038, 0x0038 },
	}, {
		148500000, { 0x0000, 0x0000, 0x0000 },
	}, {
		~0UL,      {0x0000, 0x0000, 0x0000},
	}
};

static void dw_hdmi_rk3288_set_crtc_mux(struct dw_hdmi *hdmi)
{
	u32 val;
	int mux;
	struct drm_encoder *encoder = &hdmi->encoder;

	mux = rockchip_drm_encoder_get_mux_id(hdmi->dev->of_node, encoder);
	if (mux)
		val = HDMI_SEL_VOP_LIT | (HDMI_SEL_VOP_LIT << 16);
	else
		val = HDMI_SEL_VOP_LIT << 16;

	regmap_write(hdmi->regmap, GRF_SOC_CON6, val);
	dev_dbg(hdmi->dev, "vop %s output to hdmi\n",
		(mux) ? "LIT" : "BIG");
}

static void dw_hdmi_rk3288_encoder_prepare(struct dw_hdmi *hdmi)
{
	struct drm_encoder *encoder = &hdmi->encoder;
	struct drm_connector *connector = &hdmi->connector;

	rockchip_drm_crtc_mode_config(encoder->crtc, connector->connector_type,
				      ROCKCHIP_OUT_MODE_AAAA);
}

static const struct dw_hdmi_drv_data rk3288_hdmi_drv_data = {
	.set_crtc_mux        = dw_hdmi_rk3288_set_crtc_mux,
	.encoder_prepare     = dw_hdmi_rk3288_encoder_prepare,
	.mpll_cfg            = rk3288_mpll_cfg,
	.cur_ctr             = rk3288_cur_ctr,
	.dev_type	     = RK3288_HDMI,
};

static const struct of_device_id dw_hdmi_rockchip_ids[] = {
	{ .compatible = "rockchip,rk3288-dw-hdmi",
	  .data = &rk3288_hdmi_drv_data
	},
	{},
};
MODULE_DEVICE_TABLE(of, dw_hdmi_rockchip_dt_ids);

static int dw_hdmi_rockchip_probe(struct platform_device *pdev)
{
	const struct dw_hdmi_drv_data *drv_data;
	const struct of_device_id *match;

	if (!pdev->dev.of_node)
		return -ENODEV;

	match = of_match_node(dw_hdmi_rockchip_ids, pdev->dev.of_node);
	drv_data = match->data;

	return dw_hdmi_pltfm_register(pdev, drv_data);
}

static int dw_hdmi_rockchip_remove(struct platform_device *pdev)
{
	return dw_hdmi_pltfm_unregister(pdev);
}

static struct platform_driver dw_hdmi_rockchip_pltfm_driver = {
	.probe  = dw_hdmi_rockchip_probe,
	.remove = dw_hdmi_rockchip_remove,
	.driver = {
		.name = "dwhdmi-rockchip",
		.owner = THIS_MODULE,
		.of_match_table = dw_hdmi_rockchip_ids,
	},
};

module_platform_driver(dw_hdmi_rockchip_pltfm_driver);

MODULE_AUTHOR("Andy Yan <andy.yan@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip RK3288 Specific DW-HDMI Driver Extension");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:dwhdmi-rockchip");
