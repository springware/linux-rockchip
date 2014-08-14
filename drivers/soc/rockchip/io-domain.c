
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

struct rockchip_iodomain;

/**
 * @supplies: voltage settings matching the register bits.
 */
struct rockchip_iodomain_soc_data {
	int grf_offset;
	const char *supplies[16];
	void (*init)(struct rockchip_iodomain *iod);
};

struct rockchip_iodomain_supply {
	struct rockchip_iodomain *iod;
	struct regulator *reg;
	struct notifier_block nb;
	int idx;
};

struct rockchip_iodomain {
	struct device *dev;
	struct regmap *grf;
	struct rockchip_iodomain_soc_data *soc_data;
	struct rockchip_iodomain_supply supplies[10];
};

static int rockchip_iodomain_notify(struct notifier_block *nb,
				    unsigned long event,
				    void *data)
{
	struct rockchip_iodomain_supply *supply =
			container_of(nb, struct rockchip_iodomain_supply, nb);
	struct rockchip_iodomain *iod = supply->iod;

	if (event == REGULATOR_EVENT_VOLTAGE_CHANGE) {
		unsigned long volt = (unsigned long)data;
		u32 val;

		if (volt != 18000000 && volt != 3300000) {
			dev_warn(supply->iod->dev, "unsupported io voltage %lu\n",
				 volt);
		}

		/* set value bit */
		val = (volt == 18000000) ? 1 : 0;
		val <<= supply->idx;

		/* apply hiword-mask */
		val |= (BIT(supply->idx) << 16);

		regmap_write(iod->grf, iod->soc_data->grf_offset, val);
	}

	return NOTIFY_OK;
}


#define RK3288_SOC_CON2		0x24c
#define RK3288_SOC_CON2_FLASH0	BIT(7)
static void rk3288_iodomain_init(struct rockchip_iodomain *iod)
{
	int ret;
	u32 val;

	/*
	 * set flash0 iodomain to also use this framework
	 * instead of a special gpio.
	 */
	val = RK3288_SOC_CON2_FLASH0 | (RK3288_SOC_CON2_FLASH0 << 16);
	ret = regmap_update_bits(iod->grf, RK3288_SOC_CON2, val, val);
	if (ret < 0)
		dev_warn(iod->dev, "couldn't update flash0 ctrl\n");
}

static struct rockchip_iodomain_soc_data soc_data[3] = {
	{ /* rk3188 */
		.grf_offset = 0x104,
		.supplies = {
			NULL,
			NULL,
			NULL,
			NULL,
			NULL,
			NULL,
			NULL,
			NULL,
			"ap0",
			"ap1",
			"cif",
			"flash",
			"vccio0",
			"vccio1",
			"lcdc0",
			"lcdc1",
		},
	}, { /* rk3288 */
		.grf_offset = 0x380,
		.supplies = {
			"lcdc",
			"dvp",
			"flash0",
			"flash1",
			"wifi",
			"bb",
			"audio",
			"sdcard",
			"gpio30",
			"gpio1830",
		},
		.init = rk3288_iodomain_init,
	},
};

static const struct of_device_id rockchip_iodomain_match[] = {
	{ .compatible = "rockchip,rk3188-iodomain", .data = (void *)&soc_data[1] },
	{ .compatible = "rockchip,rk3288-iodomain", .data = (void *)&soc_data[2] },
	{},
};

static int rockchip_iodomain_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	const struct of_device_id *match;
	struct rockchip_iodomain *iod;
	int i, j, ret;

	if (!np)
		return -ENODEV;

	iod = devm_kzalloc(&pdev->dev, sizeof(*iod), GFP_KERNEL);
	if (!iod)
		return -ENOMEM;

	iod->dev = &pdev->dev;
	platform_set_drvdata(pdev, iod);

	match = of_match_node(rockchip_iodomain_match, np);
	iod->soc_data = (struct rockchip_iodomain_soc_data *)match->data;

	iod->grf = syscon_regmap_lookup_by_phandle(np, "rockchip,grf");
	if (IS_ERR(iod->grf)) {
		dev_err(&pdev->dev, "couldn't find grf regmap\n");
		return PTR_ERR(iod->grf);
	}

	if (iod->soc_data->init)
		iod->soc_data->init(iod);

	j = 0;
	for (i = 0; i < 16; i++) {
		const char *supply = iod->soc_data->supplies[i];
		struct rockchip_iodomain_supply *io_supply = &iod->supplies[j];
		unsigned long volt;
		u32 val;

		if (!supply)
			continue;

		io_supply->reg = devm_regulator_get(iod->dev, supply);
		if (IS_ERR(io_supply->reg)) {
			if (PTR_ERR(io_supply->reg) != -EPROBE_DEFER)
				dev_err(iod->dev, "couldn't get regulator %s\n",
					supply);
			return PTR_ERR(io_supply->reg);
		}

		/* set initial correct value */
		volt = regulator_get_voltage(io_supply->reg);
		if (volt != 18000000 && volt != 3300000)  {
			dev_err(iod->dev, "unsupported io voltage %lu\n",
				volt);
			ret = -EINVAL;
			goto unreg_notify;
		}

		/* set value bit */
		val = (volt == 18000000) ? 1 : 0;
		val <<= i;

		/* apply hiword-mask */
		val |= (BIT(i) << 16);

		regmap_write(iod->grf, iod->soc_data->grf_offset, val);

		io_supply->iod = iod;
		io_supply->idx = i;

		/* register regulator notifier */
		io_supply->nb.notifier_call = rockchip_iodomain_notify;
		ret = regulator_register_notifier(io_supply->reg, &io_supply->nb);
		if (ret) {
			dev_err(&pdev->dev,
				"regulator notifier request failed\n");
			return ret;
		}

		j++;
	}

	return 0;

unreg_notify:
	while (j > 0) {
		struct rockchip_iodomain_supply *io_supply = &iod->supplies[j - 1];
		regulator_unregister_notifier(io_supply->reg, &io_supply->nb);
		j--;
	}

	return ret;
}

static int rockchip_iodomain_remove(struct platform_device *pdev)
{
	struct rockchip_iodomain *iod = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < 10; i++) {
		struct rockchip_iodomain_supply *io_supply = &iod->supplies[i];
		if (!io_supply->reg)
			continue;

		regulator_unregister_notifier(io_supply->reg, &io_supply->nb);
	}

	return 0;
}

static struct platform_driver rockchip_iodomain_driver = {
	.probe   = rockchip_iodomain_probe,
	.remove  = rockchip_iodomain_remove,
	.driver  = {
		.owner = THIS_MODULE,
		.name  = "rockchip-iodomain",
		.of_match_table = rockchip_iodomain_match,
	},
};

module_platform_driver(rockchip_iodomain_driver);

MODULE_DESCRIPTION("Rockchip IO-domain driver");
MODULE_AUTHOR("Heiko Stuebner <heiko@sntech.de>");
MODULE_LICENSE("GPL v2");
