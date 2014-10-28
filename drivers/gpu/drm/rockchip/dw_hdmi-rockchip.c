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
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/clk.h>
#include <drm/bridge/dw_hdmi.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_vop.h"

#define GRF_SOC_CON6                    0x025c
#define HDMI_SEL_VOP_LIT                (1 << 4)

struct rockchip_hdmi {
	struct device *dev;
	struct clk *clk;
	struct clk *hdcp_clk;
	struct regmap *regmap;
};

static const struct mpll_config rockchip_mpll_cfg[] = {
	{
		27000000, {
			{ 0x00b3, 0x0003},
			{ 0x2153, 0x0000},
			{ 0x40f4, 0x0000}
		},
	}, {
		66000000, {
			{ 0x013e, 0x0003},
			{ 0x217e, 0x0002},
			{ 0x4061, 0x0002}
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


static const struct curr_ctrl rockchip_cur_ctr[] = {
	/*      pixelclk    bpp8    bpp10   bpp12 */
	{
		27000000,  { 0x0038, 0x0018, 0x0018 },
	}, {
		66000000,  { 0x0038, 0x0038, 0x0038 },
	}, {
		74250000,  { 0x0038, 0x0038, 0x0038 },
	}, {
		148500000, { 0x0000, 0x0000, 0x0000 },
	}, {
		~0UL,      {0x0000, 0x0000, 0x0000},
	}
};

static void dw_hdmi_rockchip_set_crtc_mux(void *priv,
					  struct drm_encoder *encoder)
{
	u32 val;
	int mux;
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)priv;
	mux = rockchip_drm_encoder_get_mux_id(hdmi->dev->of_node, encoder);
	if (mux)
		val = HDMI_SEL_VOP_LIT | (HDMI_SEL_VOP_LIT << 16);
	else
		val = HDMI_SEL_VOP_LIT << 16;

	regmap_write(hdmi->regmap, GRF_SOC_CON6, val);
	dev_dbg(hdmi->dev, "vop %s output to hdmi\n",
		(mux) ? "LIT" : "BIG");
}

static int rockchip_hdmi_parse_dt(struct rockchip_hdmi *hdmi)
{
	struct device_node *np = hdmi->dev->of_node;

	hdmi->regmap = syscon_regmap_lookup_by_phandle(np, "rockchip,grf");
	if (IS_ERR(hdmi->regmap)) {
		dev_err(hdmi->dev, "Unable to get rockchip,grf\n");
		return PTR_ERR(hdmi->regmap);
	}

	hdmi->clk = devm_clk_get(hdmi->dev, "clk");
	if (IS_ERR(hdmi->clk)) {
		dev_err(hdmi->dev, "Unable to get HDMI clk\n");
	       return PTR_ERR(hdmi->clk);
	}

	hdmi->hdcp_clk = devm_clk_get(hdmi->dev, "hdcp_clk");
	if (IS_ERR(hdmi->hdcp_clk)) {
		dev_err(hdmi->dev, "Unable to get HDMI iahb clk\n");
		return PTR_ERR(hdmi->hdcp_clk);
	}

	return 0;
}

static void *dw_hdmi_rockchip_setup(struct platform_device *pdev)
{
	struct rockchip_hdmi *hdmi;
	int ret;

	hdmi = devm_kzalloc(&pdev->dev, sizeof(*hdmi), GFP_KERNEL);
	if (!hdmi)
		return ERR_PTR(-ENOMEM);
	hdmi->dev = &pdev->dev;

	ret = rockchip_hdmi_parse_dt(hdmi);
	if (ret) {
		dev_err(hdmi->dev, "Unable to parse OF data\n");
		return ERR_PTR(ret);
	}

	ret = clk_prepare_enable(hdmi->clk);
	if (ret) {
		dev_err(hdmi->dev, "Cannot enable HDMI clock: %d\n", ret);
		return ERR_PTR(ret);
	}

	ret = clk_prepare_enable(hdmi->hdcp_clk);
	if (ret) {
		dev_err(hdmi->dev, "Cannot enable HDMI hdcp clock: %d\n", ret);
		return ERR_PTR(ret);
	}

	return hdmi;
}

static void dw_hdmi_rockchip_exit(void *priv)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)priv;

	clk_disable_unprepare(hdmi->clk);
	clk_disable_unprepare(hdmi->hdcp_clk);
}

static void dw_hdmi_rockchip_encoder_prepare(struct drm_connector *connector,
					     struct drm_encoder *encoder)
{
	rockchip_drm_crtc_mode_config(encoder->crtc, connector->connector_type,
				      ROCKCHIP_OUT_MODE_AAAA);
}

static enum drm_mode_status
dw_hdmi_rockchip_mode_valid(struct drm_connector *connector,
			    struct drm_display_mode *mode)
{
	int pclk = mode->clock * 1000;
	bool valid = false;
	int i;
	const struct mpll_config *mpll_cfg = rockchip_mpll_cfg;
	for (i = 0; mpll_cfg[i].mpixelclock != (~0UL); i++) {
		if (pclk == mpll_cfg[i].mpixelclock) {
			valid = true;
			break;
		}
	}

	return (valid) ? MODE_OK : MODE_BAD;
}
static const struct dw_hdmi_plat_data rockchip_hdmi_drv_data = {
	.setup			= dw_hdmi_rockchip_setup,
	.exit			= dw_hdmi_rockchip_exit,
	.set_crtc_mux		= dw_hdmi_rockchip_set_crtc_mux,
	.encoder_prepare	= dw_hdmi_rockchip_encoder_prepare,
	.mpll_cfg		= rockchip_mpll_cfg,
	.mode_valid		= dw_hdmi_rockchip_mode_valid,
	.cur_ctr		= rockchip_cur_ctr,
	.dev_type		= RK32_HDMI,
};

static const struct of_device_id dw_hdmi_rockchip_ids[] = {
	{ .compatible = "rockchip,rk3288-dw-hdmi",
	  .data = &rockchip_hdmi_drv_data
	},
	{},
};
MODULE_DEVICE_TABLE(of, dw_hdmi_rockchip_dt_ids);

static int dw_hdmi_rockchip_probe(struct platform_device *pdev)
{
	const struct dw_hdmi_plat_data *plat_data;
	const struct of_device_id *match;

	if (!pdev->dev.of_node)
		return -ENODEV;

	match = of_match_node(dw_hdmi_rockchip_ids, pdev->dev.of_node);
	plat_data = match->data;

	return dw_hdmi_pltfm_register(pdev, plat_data);
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
MODULE_DESCRIPTION("Rockchip Specific DW-HDMI Driver Extension");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:dwhdmi-rockchip");
