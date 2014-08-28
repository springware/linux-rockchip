/*
 * An IIO channel based thermal sensor driver
 *
 * Copyright (C) 2014 Sony Mobile Communications, AB
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/iio/iio.h>
#include <linux/iio/consumer.h>
#include <linux/thermal.h>
#include <linux/err.h>
#include <linux/of.h>

#define MILLIKELVIN_0C 273150

struct iio_thermal_conv {
	s32 *values;
	int ntuple;
	long (*convert)(const struct iio_thermal_conv *, int val);
};

struct iio_thermal {
	struct thermal_zone_device *tz;
	struct iio_channel *chan;
	struct iio_thermal_conv conv;
};

static int iio_bsearch(const struct iio_thermal_conv *conv, s32 input)
{
	int h = conv->ntuple - 1;
	int l = 0;

	while (h - 1 > l) {
		int m = (l + h) & ~1;
		s32 v = conv->values[m];
		if (v < input)
			l = m >> 1;
		else
			h = m >> 1;
	}

	return h;
}

static long iio_thermal_interpolate(const struct iio_thermal_conv *conv, int v)
{
	int n;

	n = iio_bsearch(conv, v);
	if (n == 0 || n == conv->ntuple) {
		BUG();
	} else {
		s32 *pts = &conv->values[(n-1)*2];
		s32 sx = pts[3] - pts[1];
		s32 sy = pts[2] - pts[0];
		return sx * (v - pts[0]) / sy + pts[1];
	}
}

static long iio_thermal_scale(const struct iio_thermal_conv *conv, int v)
{
	return div_s64((s64)v * conv->values[0], conv->values[1]);
}

static int iio_thermal_get_temp(void *pdata, long *value)
{
	struct iio_thermal *iio = pdata;
	long temp;
	int val;
	int rc;

	rc = iio_read_channel_processed(iio->chan, &val);
	if (rc)
		return rc;

	temp = iio->conv.convert(&iio->conv, val);

	/* thermal core wants milli-celsius; we deal in milli-kelvin */
	temp = temp - MILLIKELVIN_0C;

	/*
	 * Although it would appear that the temperature value here is a
	 * signed value for sensors, the underlying thermal zone core
	 * doesn't deal with negative temperature. Cut this value off at 0C.
	 */
	*value = (temp < 0) ? 0 : temp;

	return 0;
}

static int iio_thermal_probe(struct platform_device *pdev)
{
	struct iio_thermal *iio;
	const void *values;
	const char *type;
	int rc;

	dev_info(&pdev->dev, "registering IIO thermal device\n");
	iio = devm_kzalloc(&pdev->dev, sizeof(*iio), GFP_KERNEL);
	if (!iio)
		return -ENOMEM;

	values = of_get_property(pdev->dev.of_node,
			"conversion-values", &iio->conv.ntuple);
	if (values == NULL || (iio->conv.ntuple % (sizeof(u32) * 2)) != 0) {
		dev_err(&pdev->dev, "invalid/missing conversion values\n");
		return -EINVAL;
	}
	iio->conv.ntuple /= sizeof(u32) * 2;
	iio->conv.values = devm_kzalloc(&pdev->dev,
			sizeof(u32) * 2 * iio->conv.ntuple, GFP_KERNEL);
	if (!iio->conv.values)
		return -ENOMEM;

	rc = of_property_read_u32_array(pdev->dev.of_node,
			"conversion-values", (u32 *)iio->conv.values,
			iio->conv.ntuple * 2);
	if (rc) {
		dev_err(&pdev->dev, "invalid/missing conversion values\n");
		return -EINVAL;
	}

	rc = of_property_read_string(pdev->dev.of_node,
			"conversion-method", &type);
	if (rc) {
		dev_err(&pdev->dev, "invalid/missing conversion method\n");
		return rc;
	}
	if (!strcmp(type, "interpolation") && iio->conv.ntuple > 1) {
		if (iio->conv.values[0] > iio->conv.values[2]) {
			dev_err(&pdev->dev,
					"conversion values should ascend\n");
			return rc;
		}
		iio->conv.convert = iio_thermal_interpolate;
	} else if (!strcmp(type, "scalar") && iio->conv.ntuple == 1) {
		iio->conv.convert = iio_thermal_scale;
	} else {
		dev_err(&pdev->dev, "invalid conversion method for values\n");
		return rc;
	}

	iio->chan = iio_channel_get(&pdev->dev, NULL);
	if (IS_ERR(iio->chan)) {
		dev_err(&pdev->dev, "invalid/missing iio channel\n");
		return PTR_ERR(iio->chan);
	}
	if (iio->chan->channel->type != IIO_VOLTAGE) {
		dev_err(&pdev->dev, "specified iio channel is not voltage\n");
		iio_channel_release(iio->chan);
		return -EINVAL;
	}
	platform_set_drvdata(pdev, iio);

	iio->tz = thermal_zone_of_sensor_register(&pdev->dev, 0, iio,
			iio_thermal_get_temp, NULL);
	if (IS_ERR(iio->tz)) {
		dev_err(&pdev->dev, "failed to register thermal sensor\n");
		iio_channel_release(iio->chan);
		return PTR_ERR(iio->tz);
	}
	dev_info(&pdev->dev, "successfully registered IIO thermal sensor\n");

	return 0;
}

static int iio_thermal_remove(struct platform_device *pdev)
{
	struct iio_thermal *iio;

	iio = platform_get_drvdata(pdev);

	thermal_zone_of_sensor_unregister(&pdev->dev, iio->tz);
	iio_channel_release(iio->chan);

	return 0;
}

static const struct of_device_id iio_thermal_match[] = {
	{ .compatible = "iio-thermal", },
	{ }
};
MODULE_DEVICE_TABLE(of, iio_thermal_match);

static struct platform_driver iio_thermal = {
	.driver = {
		.name = "iio-thermal",
		.owner = THIS_MODULE,
		.of_match_table = iio_thermal_match,
	},
	.probe = iio_thermal_probe,
	.remove = iio_thermal_remove,
};
module_platform_driver(iio_thermal);

MODULE_DESCRIPTION("Thermal driver for IIO ADCs");
MODULE_LICENSE("GPL v2");
