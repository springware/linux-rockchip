/**
 * dwmac-rockchip.c - Rockchip DWMAC specific glue layer
 *
 * Copyright (C) 2014 Heiko Stuebner <heiko@sntech.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/stmmac.h>
#include <linux/clk.h>
#include <linux/phy.h>
#include <linux/of_net.h>
#include <linux/regulator/consumer.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>


#define RK3288_GRF_SOC_CON1		0x248
#define RK3288_GMAC_PHY_INTF_SEL_RGMII	(0x1 << 6)
#define RK3288_GMAC_PHY_INTF_SEL_RMII	(0x4 << 6)
#define RK3288_GMAC_PHY_INTF_SEL_MASK	(0x7 << 6)
#define RK3288_GMAC_FLOW_CTRL		BIT(9)
#define RK3288_GMAC_SPEED_100M		BIT(10) /* otherwise 10M */
#define RK3288_GMAC_RMII_CLK_2_5M	(0x0 << 11)
#define RK3288_GMAC_RMII_CLK_25M	(0x1 << 11)
#define RK3288_GMAC_RMII_CLK_MASK	(0x1 << 11)
#define RK3288_GMAC_RGMII_CLK_125M	(0x0 << 12)
#define RK3288_GMAC_RGMII_CLK_25M	(0x3 << 12)
#define RK3288_GMAC_RGMII_CLK_2_5M	(0x2 << 12)
#define RK3288_GMAC_RGMII_CLK_MASK	(0x3 << 12)
#define RK3288_GMAC_RMII_MODE		BIT(14)

#define RK3288_GRF_SOC_CON3		0x250
#define RK3288_GMAC_RXCLK_DLY_ENABLE	BIT(15)
#define RK3288_GMAC_TXCLK_DLY_ENABLE	BIT(14)
#define RK3288_GMAC_RXCLK_DLY_CFG(val)	(val << 7)
#define RK3288_GMAC_RXCLK_DLY_MASK	(0x7f << 7)
#define RK3288_GMAC_TXCLK_DLY_CFG(val)	(val)
#define RK3288_GMAC_TXCLK_DLY_MASK	0x7f

struct rockchip_priv_data {
	struct device *dev;
	struct regmap *grf;
	int interface;
	struct regulator *regulator;

//	int clk_enabled;
//	struct clk *tx_clk;
};

static void *rk3288_gmac_setup(struct platform_device *pdev)
{
	struct rockchip_priv_data *gmac;
	struct device *dev = &pdev->dev;

	gmac = devm_kzalloc(dev, sizeof(*gmac), GFP_KERNEL);
	if (!gmac)
		return ERR_PTR(-ENOMEM);

	gmac->dev = &pdev->dev;
	gmac->interface = of_get_phy_mode(dev->of_node);

	gmac->grf = syscon_regmap_lookup_by_phandle(dev->of_node, "rockchip,grf");
	if (IS_ERR(gmac->grf)) {
		dev_err(dev, "could not get grf syscon, %ld\n", PTR_ERR(gmac->grf));
		return gmac->grf;
	}

	/* Optional regulator for PHY */
	gmac->regulator = devm_regulator_get_optional(dev, "phy");
	if (IS_ERR(gmac->regulator)) {
		if (PTR_ERR(gmac->regulator) == -EPROBE_DEFER)
			return ERR_PTR(-EPROBE_DEFER);
		dev_info(dev, "no regulator found\n");
		gmac->regulator = NULL;
	}

/*	gmac->tx_clk = devm_clk_get(dev, "allwinner_gmac_tx");
	if (IS_ERR(gmac->tx_clk)) {
		dev_err(dev, "could not get tx clock\n");
		return gmac->tx_clk;
	}
*/
	return gmac;
}

//#define SUN7I_GMAC_GMII_RGMII_RATE	125000000
//#define SUN7I_GMAC_MII_RATE		25000000

static int rk3288_gmac_init(struct platform_device *pdev, void *priv)
{
	struct rockchip_priv_data *gmac = priv;
	int ret;
	u32 data;

	if (gmac->regulator) {
		ret = regulator_enable(gmac->regulator);
		if (ret)
			return ret;
	}

	/* Set GMAC interface port mode
	 */
	if (gmac->interface == PHY_INTERFACE_MODE_RGMII) {
//		clk_set_rate(gmac->tx_clk, SUN7I_GMAC_GMII_RGMII_RATE);
//		clk_prepare_enable(gmac->tx_clk);
//		gmac->clk_enabled = 1;

		data = (RK3288_GMAC_PHY_INTF_SEL_MASK << 16)
			| RK3288_GMAC_PHY_INTF_SEL_RGMII;
		data |= (RK3288_GMAC_RMII_MODE << 16);
		ret = regmap_write(gmac->grf, RK3288_GRF_SOC_CON1, data);
		if (ret < 0)
			return ret;

		data = (RK3288_GMAC_RXCLK_DLY_ENABLE << 16)
			| RK3288_GMAC_RXCLK_DLY_ENABLE;
		data |= (RK3288_GMAC_TXCLK_DLY_ENABLE << 16)
			| RK3288_GMAC_TXCLK_DLY_ENABLE;
		data |= (RK3288_GMAC_RXCLK_DLY_MASK << 16)
			| RK3288_GMAC_RXCLK_DLY_CFG(0x10);
		data |= (RK3288_GMAC_TXCLK_DLY_MASK << 16)
			| RK3288_GMAC_TXCLK_DLY_CFG(0x40);
		return regmap_write(gmac->grf, RK3288_GRF_SOC_CON3, data);
	} else if (gmac->interface == PHY_INTERFACE_MODE_RMII) {
//		clk_set_rate(gmac->tx_clk, SUN7I_GMAC_RMII_RATE);
//		clk_prepare(gmac->tx_clk);

		data = (RK3288_GMAC_PHY_INTF_SEL_MASK << 16)
			| RK3288_GMAC_PHY_INTF_SEL_RMII;
		data |= (RK3288_GMAC_RMII_MODE << 16)
			| RK3288_GMAC_RMII_MODE;
		return regmap_write(gmac->grf, RK3288_GRF_SOC_CON1, data);
	} else {
		dev_err(gmac->dev, "unsupported media interface %d\n", gmac->interface);
		return -ENOTSUPP;
	}
}

static void rk3288_gmac_exit(struct platform_device *pdev, void *priv)
{
	struct rockchip_priv_data *gmac = priv;

/*	if (gmac->clk_enabled) {
		clk_disable(gmac->tx_clk);
		gmac->clk_enabled = 0;
	}
	clk_unprepare(gmac->tx_clk);*/

	if (gmac->regulator)
		regulator_disable(gmac->regulator);
}

static void rk3288_fix_speed(void *priv, unsigned int speed)
{
	struct rockchip_priv_data *gmac = priv;
	u32 data;

	/* only GMII mode requires us to reconfigure the clock lines */
/*	if (gmac->interface != PHY_INTERFACE_MODE_GMII)
		return;

	if (gmac->clk_enabled) {
		clk_disable(gmac->tx_clk);
		gmac->clk_enabled = 0;
	}
	clk_unprepare(gmac->tx_clk);

	if (speed == 1000) {
		clk_set_rate(gmac->tx_clk, SUN7I_GMAC_GMII_RGMII_RATE);
		clk_prepare_enable(gmac->tx_clk);
		gmac->clk_enabled = 1;
	} else {
		clk_set_rate(gmac->tx_clk, SUN7I_GMAC_MII_RATE);
		clk_prepare(gmac->tx_clk);
	}*/

	if (gmac->interface == PHY_INTERFACE_MODE_RGMII) {
		switch (speed) {
		case 10:
			data = (RK3288_GMAC_RGMII_CLK_MASK << 16)
				| RK3288_GMAC_RGMII_CLK_2_5M;
			break;
		case 100:
			data = (RK3288_GMAC_RGMII_CLK_MASK << 16)
				| RK3288_GMAC_RGMII_CLK_25M;
			break;
		case 1000:
			data = (RK3288_GMAC_RGMII_CLK_MASK << 16)
				| RK3288_GMAC_RGMII_CLK_125M;
			break;
		default:
			dev_err(gmac->dev, "speed %d not supported\n", speed);
			return;
		}
	} else {
		switch (speed) {
		case 10:
			data = (RK3288_GMAC_RMII_CLK_MASK << 16)
				| RK3288_GMAC_RMII_CLK_2_5M;
			break;
		case 100:
			data = (RK3288_GMAC_RMII_CLK_MASK << 16)
				| RK3288_GMAC_RMII_CLK_25M;
			break;
		default:
			dev_err(gmac->dev, "speed %d not supported\n", speed);
			return;
		}
	}

	regmap_write(gmac->grf, RK3288_GRF_SOC_CON1, data);
}

/* of_data specifying hardware features and callbacks.
 * hardware features were copied from Allwinner drivers. */
const struct stmmac_of_data rk3288_gmac_data = {
	.has_gmac = 1,
	.tx_coe = 1,
	.fix_mac_speed = rk3288_fix_speed,
	.setup = rk3288_gmac_setup,
	.init = rk3288_gmac_init,
	.exit = rk3288_gmac_exit,
};
