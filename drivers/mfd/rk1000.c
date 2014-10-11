/*
 * MFD core driver for Rockchip RK808
 *
 * Copyright (c) 2014, Fuzhou Rockchip Electronics Co., Ltd
 *
 * Author: Heiko Stuebner <heiko@sntech.de>
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

#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/mfd/rk808.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/mfd/rk1000.h>


static const struct mfd_cell rk1000_cells[] = {
	{ .name = "rk1000-codec", },
	{ .name = "rk1000-tve", },
};

/* int rk1000_i2c_send(const u8 addr, const u8 reg, const u8 value)
{
	struct i2c_adapter *adap;
	struct i2c_msg msg;
	int ret;
	char buf[2];
	
	if(rk1000 == NULL || rk1000->client == NULL){
		printk("rk1000 not init!\n");
		return -1;
	}
	
	adap = rk1000->client->adapter;
	
	buf[0] = reg;
	buf[1] = value;
	
	msg.addr = addr;
	msg.flags = rk1000->client->flags;
	msg.len = 2;
	msg.buf = buf;
	msg.scl_rate = RK1000_I2C_RATE;

	ret = i2c_transfer(adap, &msg, 1);
	if (ret != 1) {
            printk("rk1000 control i2c write err,ret =%d\n",ret);
            return -1;
    }
    else
    	return 0;
} 

int rk1000_i2c_recv(const u8 addr, const u8 reg, const char *buf)
{
	struct i2c_adapter *adap;
	struct i2c_msg msgs[2];
	int ret;
	
	if(rk1000 == NULL || rk1000->client == NULL){
		printk("rk1000 not init!\n");
		return -1;
	}
	adap = rk1000->client->adapter;
	msgs[0].addr = addr;
	msgs[0].flags = rk1000->client->flags;
	msgs[0].len = 1;
	msgs[0].buf = (char*)(&reg);
	msgs[0].scl_rate = RK1000_I2C_RATE;

	msgs[1].addr = addr;
	msgs[1].flags = rk1000->client->flags | I2C_M_RD;
	msgs[1].len = 1;
	msgs[1].buf = (char*)buf;
	msgs[1].scl_rate = RK1000_I2C_RATE;

	ret = i2c_transfer(adap, msgs, 2);

	return (ret == 2)? 0 : -1;
} */

static bool rk1000_is_volatile_reg_core(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case RK1000_SRESET_CON:
	case RK1000_ADC_STAT:
		return true;
	}

	return false;
}

static const struct regmap_config rk1000_regmap_config_core = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = RK1000_ADC_STAT,
	.cache_type = REGCACHE_RBTREE,
	.volatile_reg = rk1000_is_volatile_reg_core,
};

static const struct regmap_config rk1000_regmap_config_tve = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = RK1000_TVE_HVER,
//	.cache_type = REGCACHE_RBTREE,
//	.volatile_reg = rk1000_is_volatile_reg_core,
};

static const struct regmap_config rk1000_regmap_config_codec = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0x1f,
//	.cache_type = REGCACHE_RBTREE,
//	.volatile_reg = rk1000_is_volatile_reg_core,
};

static void rk1000_unregister_clients(void *data)
{
	struct rk1000 *rk1000 = data;

	i2c_unregister_device(rk1000->client_codec);
	i2c_unregister_device(rk1000->client_tve);
}

static int rk1000_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct rk1000 *rk1000;
	u32 val;
	int ret;

	rk1000 = devm_kzalloc(&client->dev, sizeof(*rk1000), GFP_KERNEL);
	if (!rk1000)
		return -ENOMEM;

	rk1000->dev = &client->dev;
	rk1000->client_core = client;
	i2c_set_clientdata(client, rk1000);

	rk1000->client_tve = i2c_new_dummy(client->adapter, 0x42);
	if (!rk1000->client_tve)
		return -ENODEV;

	rk1000->client_codec = i2c_new_dummy(client->adapter, 0x60);
	if (!rk1000->client_codec) {
		i2c_unregister_device(rk1000->client_tve);
		return -ENODEV;
	}

	ret = devm_add_action(&client->dev, rk1000_unregister_clients, rk1000);
	if (ret) {
		i2c_unregister_device(rk1000->client_codec);
		i2c_unregister_device(rk1000->client_tve);
		return ret;
	}

	rk1000->regmap_core = devm_regmap_init_i2c(rk1000->client_core,
						&rk1000_regmap_config_core);
	if (IS_ERR(rk1000->regmap_core)) {
		dev_err(&client->dev, "regmap initialization failed\n");
		return PTR_ERR(rk1000->regmap_core);
	}

	rk1000->regmap_tve = devm_regmap_init_i2c(rk1000->client_tve,
						&rk1000_regmap_config_tve);
	if (IS_ERR(rk1000->regmap_tve)) {
		dev_err(&client->dev, "regmap initialization failed\n");
		return PTR_ERR(rk1000->regmap_tve);
	}

	rk1000->regmap_codec = devm_regmap_init_i2c(rk1000->client_codec,
						&rk1000_regmap_config_codec);
	if (IS_ERR(rk1000->regmap_codec)) {
		dev_err(&client->dev, "regmap initialization failed\n");
		return PTR_ERR(rk1000->regmap_codec);
	}

	rk1000->gpio_rst = devm_gpiod_get(&client->dev, "reset");
	if (IS_ERR(rk1000->gpio_rst)) {
		dev_err(&client->dev, "cannot get reset-gpio\n");
		return PTR_ERR(rk1000->gpio_rst);
	}

	ret = gpiod_direction_output(rk1000->gpio_rst, 0);
	if (ret < 0) {
		dev_err(&client->dev, "cannot configure reset-gpio\n");
		return ret;
	}

	msleep(100);
	gpiod_set_value(rk1000->gpio_rst, 1);

/*
    // rk1000 is drived by i2s_mclk, we enable i2s_clk first.
    i2s_clk= clk_get(&client->dev, "i2s_clk");
    if (IS_ERR(i2s_clk)) {
        dev_err(&client->dev, "Can't retrieve i2s clock\n");
        ret = PTR_ERR(i2s_clk);
        goto err;
    }else{
        printk("rk1000 get i2s clk success!\n");   
    }
    
    clk_set_rate(i2s_clk, 12288000);	
    clk_set_rate(i2s_clk, 11289600);
    clk_prepare_enable(i2s_clk);
    printk("rk1000 enable i2s clk\n");
    
    i2s_mclk= clk_get(&client->dev, "i2s_mclk");
    if (IS_ERR(i2s_mclk)) {
        dev_err(&client->dev, "Can't retrieve i2s mclock\n");
    }else{
        clk_set_rate(i2s_mclk, 12288000);
        clk_set_rate(i2s_mclk, 11289600);
        clk_prepare_enable(i2s_mclk);
        printk("rk1000 enable i2s mclk\n");
    }
*/
//    rk1000_i2c_send(I2C_ADDR_CTRL, CTRL_I2C, 0x22);

	/* turn off the adc */
	val = RK1000_ADC_CON_ADC0_PDOWN | RK1000_ADC_CON_ADC1_PDOWN;
	ret = regmap_update_bits(rk1000->regmap_core, RK1000_ADC_CON,
				 val, val);
	if (ret < 0) {
		dev_err(&client->dev, "failed to update core regmap\n");
		return ret;
	}

	/* turn off the codec */
	val = RK1000_CODEC_CON_DAC_DISABLE | RK1000_CODEC_CON_ADC_DISABLE
					   | RK1000_CODEC_CON_DISABLE;
	ret = regmap_update_bits(rk1000->regmap_core, RK1000_CODEC_CON,
				 val, val);
	if (ret < 0) {
		dev_err(&client->dev, "failed to update core regmap\n");
		return ret;
	}

	/* turn off the rgb2ccir converter */
	ret = regmap_update_bits(rk1000->regmap_core, RK1000_TVE_CON,
				 RK1000_TVE_CON_RGB2CCIR_ENABLE, 0);
	if (ret < 0) {
		dev_err(&client->dev, "failed to update core regmap\n");
		return ret;
	}

	ret = mfd_add_devices(&client->dev, -1, rk1000_cells,
			      ARRAY_SIZE(rk1000_cells), NULL, 0, NULL);
	if (ret) {
		dev_err(&client->dev, "failed to add mfd devices %d\n", ret);
		return ret;
	}

	return 0;
}

static int rk1000_remove(struct i2c_client *client)
{
	mfd_remove_devices(&client->dev);

	return 0;
}

static struct of_device_id rk1000_of_match[] = {
	{ .compatible = "rockchip,rk1000" },
	{ },
};
MODULE_DEVICE_TABLE(of, rk1000_of_match);

static const struct i2c_device_id rk1000_ids[] = {
	{ "rk1000", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, rk1000_ids);

static struct i2c_driver rk1000_i2c_driver = {
	.driver = {
		.name = "rk1000",
		.of_match_table = rk1000_of_match,
	},
	.probe = rk1000_probe,
	.remove = rk1000_remove,
	.id_table = rk1000_ids,
};

module_i2c_driver(rk1000_i2c_driver);

MODULE_DESCRIPTION("RK1000 TV encoder driver");
MODULE_AUTHOR("Heiko Stuebner <heiko@sntech.de>");
MODULE_LICENSE("GPL");
