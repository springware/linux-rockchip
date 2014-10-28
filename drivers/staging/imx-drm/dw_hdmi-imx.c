/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mfd/syscon.h>
#include <linux/mfd/syscon/imx6q-iomuxc-gpr.h>
#include <drm/bridge/dw_hdmi.h>
#include <video/imx-ipu-v3.h>
#include <linux/regmap.h>
#include <linux/clk.h>

#include "imx-drm.h"

struct imx_hdmi_priv {
	struct device *dev;
	struct clk *isfr_clk;
	struct clk *iahb_clk;
	struct regmap *regmap;
};

static const struct mpll_config imx_mpll_cfg[] = {
	{
		45250000, {
			{ 0x01e0, 0x0000 },
			{ 0x21e1, 0x0000 },
			{ 0x41e2, 0x0000 }
		},
	}, {
		92500000, {
			{ 0x0140, 0x0005 },
			{ 0x2141, 0x0005 },
			{ 0x4142, 0x0005 },
	},
	}, {
		148500000, {
			{ 0x00a0, 0x000a },
			{ 0x20a1, 0x000a },
			{ 0x40a2, 0x000a },
		},
	}, {
		~0UL, {
			{ 0x00a0, 0x000a },
			{ 0x2001, 0x000f },
			{ 0x4002, 0x000f },
		},
	}
};

static const struct curr_ctrl imx_cur_ctr[] = {
	/*      pixelclk     bpp8    bpp10   bpp12 */
	{
		54000000, { 0x091c, 0x091c, 0x06dc },
	}, {
		58400000, { 0x091c, 0x06dc, 0x06dc },
	}, {
		72000000, { 0x06dc, 0x06dc, 0x091c },
	}, {
		74250000, { 0x06dc, 0x0b5c, 0x091c },
	}, {
		118800000, { 0x091c, 0x091c, 0x06dc },
	}, {
		216000000, { 0x06dc, 0x0b5c, 0x091c },
	}
};

static int imx_hdmi_parse_dt(struct imx_hdmi_priv *hdmi)
{
	struct device_node *np = hdmi->dev->of_node;

	hdmi->regmap = syscon_regmap_lookup_by_phandle(np, "gpr");
	if (IS_ERR(hdmi->regmap)) {
		dev_err(hdmi->dev, "Unable to get gpr\n");
		return PTR_ERR(hdmi->regmap);
	}

	hdmi->isfr_clk = devm_clk_get(hdmi->dev, "isfr");
	if (IS_ERR(hdmi->isfr_clk)) {
		dev_err(hdmi->dev, "Unable to get HDMI isfr clk\n");
		return PTR_ERR(hdmi->isfr_clk);
	}

	hdmi->iahb_clk = devm_clk_get(hdmi->dev, "iahb");
	if (IS_ERR(hdmi->iahb_clk)) {
		dev_err(hdmi->dev, "Unable to get HDMI iahb clk\n");
		return PTR_ERR(hdmi->iahb_clk);
	}

	return 0;
}

static void *imx_hdmi_imx_setup(struct platform_device *pdev)
{
	struct imx_hdmi_priv *hdmi;
	int ret;

	hdmi = devm_kzalloc(&pdev->dev, sizeof(*hdmi), GFP_KERNEL);
	if (!hdmi)
		return ERR_PTR(-ENOMEM);
	hdmi->dev = &pdev->dev;

	ret = imx_hdmi_parse_dt(hdmi);
	if (ret < 0)
		return ERR_PTR(ret);
	ret = clk_prepare_enable(hdmi->isfr_clk);
	if (ret) {
		dev_err(hdmi->dev,
			"Cannot enable HDMI isfr clock: %d\n", ret);
		return ERR_PTR(ret);
	}

	ret = clk_prepare_enable(hdmi->iahb_clk);
	if (ret) {
		dev_err(hdmi->dev,
			"Cannot enable HDMI iahb clock: %d\n", ret);
		return ERR_PTR(ret);
	}

	return hdmi;
}

static void imx_hdmi_imx_exit(void *priv)
{
	struct imx_hdmi_priv *hdmi = (struct imx_hdmi_priv *)priv;

	clk_disable_unprepare(hdmi->isfr_clk);

	clk_disable_unprepare(hdmi->iahb_clk);
}

static void imx_hdmi_imx_set_crtc_mux(void *priv, struct drm_encoder *encoder)
{
	struct imx_hdmi_priv *hdmi = (struct imx_hdmi_priv *)priv;
	int mux = imx_drm_encoder_get_mux_id(hdmi->dev->of_node, encoder);

	regmap_update_bits(hdmi->regmap, IOMUXC_GPR3,
			   IMX6Q_GPR3_HDMI_MUX_CTL_MASK,
			   mux << IMX6Q_GPR3_HDMI_MUX_CTL_SHIFT);
}

static void imx_hdmi_imx_encoder_prepare(struct drm_connector *connector,
					struct drm_encoder *encoder)
{
	imx_drm_panel_format(encoder, V4L2_PIX_FMT_RGB24);
}

static struct imx_hdmi_plat_data imx6q_hdmi_drv_data = {
	.setup			= imx_hdmi_imx_setup,
	.exit			= imx_hdmi_imx_exit,
	.set_crtc_mux		= imx_hdmi_imx_set_crtc_mux,
	.encoder_prepare	= imx_hdmi_imx_encoder_prepare,
	.mpll_cfg		= imx_mpll_cfg,
	.cur_ctr		= imx_cur_ctr,
	.dev_type		= IMX6Q_HDMI,
};

static struct imx_hdmi_plat_data imx6dl_hdmi_drv_data = {
	.setup			= imx_hdmi_imx_setup,
	.exit			= imx_hdmi_imx_exit,
	.set_crtc_mux		= imx_hdmi_imx_set_crtc_mux,
	.encoder_prepare	= imx_hdmi_imx_encoder_prepare,
	.mpll_cfg		= imx_mpll_cfg,
	.cur_ctr		= imx_cur_ctr,
	.dev_type		= IMX6DL_HDMI,
};

static const struct of_device_id imx_hdmi_imx_ids[] = {
	{ .compatible = "fsl,imx6q-hdmi",
	  .data = &imx6q_hdmi_drv_data
	}, {
	  .compatible = "fsl,imx6dl-hdmi",
	  .data = &imx6dl_hdmi_drv_data
	},
	{},
};
MODULE_DEVICE_TABLE(of, imx_hdmi_imx_dt_ids);

static int imx_hdmi_imx_probe(struct platform_device *pdev)
{
	const struct imx_hdmi_plat_data *plat_data;
	const struct of_device_id *match;

	if (!pdev->dev.of_node)
		return -ENODEV;

	match = of_match_node(imx_hdmi_imx_ids, pdev->dev.of_node);
	plat_data = match->data;

	return imx_hdmi_pltfm_register(pdev, plat_data);
}

static int imx_hdmi_imx_remove(struct platform_device *pdev)
{
	return imx_hdmi_pltfm_unregister(pdev);
}

static struct platform_driver imx_hdmi_imx_pltfm_driver = {
	.probe  = imx_hdmi_imx_probe,
	.remove = imx_hdmi_imx_remove,
	.driver = {
		.name = "dwhdmi-imx",
		.owner = THIS_MODULE,
		.of_match_table = imx_hdmi_imx_ids,
	},
};

module_platform_driver(imx_hdmi_imx_pltfm_driver);

MODULE_AUTHOR("Andy Yan <andy.yan@rock-chips.com>");
MODULE_DESCRIPTION("IMX6 Specific DW-HDMI Driver Extension");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:dwhdmi-imx");
