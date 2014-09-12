/*
 * Regulator driver for syr82x DCDC chip for rk32xx
 * Copyright (C) 2014 ROCKCHIP, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/kernel.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regmap.h>

#define SYR82X_VSEL0		0x00
#define SYR82X_VSEL1		0x01
#define SYR82X_CTRL		0x02
#define SYR82X_ID1		0x03
#define SYR82X_ID2		0x04
#define SYR82X_PGOOD		0x05

#define SYR82X_VSEL_EN		BIT(7)
#define SYR82X_VSEL_MODE	BIT(6)
#define SYR82X_VSEL_VMASK	0x3f

#define	SYR82X_VOLTAGE_NUM	64

struct syr82x {
	struct regulator_desc desc;
	struct regmap *regmap;
};

static const struct regmap_config syr82x_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};


static const struct regulator_linear_range syr82x_voltage_ranges[] = {
	REGULATOR_LINEAR_RANGE(712500, 0, 63, 12500),
};

static struct regulator_ops syr82x_ops = {
	.list_voltage		= regulator_list_voltage_linear_range,
	.map_voltage		= regulator_map_voltage_linear_range,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
};


static int syr82x_probe(struct i2c_client *client,
			const struct i2c_device_id *i2c_id)
{
	struct device_node *np = client->dev.of_node;
	struct regulator_config config = { };
	struct regulator_init_data *init_data;
	struct regulator_dev *rdev;
	struct syr82x *syr82x;

	if (!np)
		return -ENODEV;

	syr82x = devm_kzalloc(&client->dev, sizeof(struct syr82x), GFP_KERNEL);
	if (!syr82x)
		return -ENOMEM;

	i2c_set_clientdata(client, syr82x);

	syr82x->regmap = devm_regmap_init_i2c(client, &syr82x_regmap_config);
	if (IS_ERR(syr82x->regmap)) {
		dev_err(&client->dev, "Failed to allocate register map: %ld\n",
			PTR_ERR(syr82x->regmap));
		return PTR_ERR(syr82x->regmap);
	}

	init_data = of_get_regulator_init_data(&client->dev, np);

	syr82x->desc.name = devm_kstrdup(&client->dev,
					 init_data->constraints.name,
					 GFP_KERNEL);
	if (!syr82x->desc.name)
		return -ENOMEM;

	syr82x->desc.type = REGULATOR_VOLTAGE;
	syr82x->desc.supply_name = "vin";
	syr82x->desc.ops = &syr82x_ops;
	syr82x->desc.n_voltages = SYR82X_VOLTAGE_NUM;
	syr82x->desc.linear_ranges = syr82x_voltage_ranges;
	syr82x->desc.n_linear_ranges = ARRAY_SIZE(syr82x_voltage_ranges);
	syr82x->desc.vsel_reg = SYR82X_VSEL0;
	syr82x->desc.vsel_mask = SYR82X_VSEL_VMASK;
	syr82x->desc.enable_reg = SYR82X_VSEL0;
	syr82x->desc.enable_mask = SYR82X_VSEL_EN;
	syr82x->desc.owner = THIS_MODULE;

	config.dev = &client->dev;
	config.init_data = init_data;
	config.of_node = np;
	config.driver_data = syr82x;
	config.regmap = syr82x->regmap;

	rdev = devm_regulator_register(&client->dev, &syr82x->desc, &config);
	if (IS_ERR(rdev)) {
		dev_err(&client->dev, "failed to register %s\n",
			syr82x->desc.name);
		return PTR_ERR(rdev);
	}

	return 0;
}

static const struct of_device_id syr82x_dt_ids[] = {
	{ .compatible = "silergy,syr827" },
	{ .compatible = "silergy,syr828" },
	{ }
};
MODULE_DEVICE_TABLE(of, syr82x_dt_ids);

static const struct i2c_device_id syr82x_i2c_id[] = {
	{ "syr82x", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, syr82x_i2c_id);

static struct i2c_driver syr82x_driver = {
	.driver = {
		.name = "syr82x",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(syr82x_dt_ids),
	},
	.probe    = syr82x_probe,
	.id_table = syr82x_i2c_id,
};

module_i2c_driver(syr82x_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Heiko Stuebner <heiko@sntech.de>");
MODULE_DESCRIPTION("SYR82x PMIC driver");

