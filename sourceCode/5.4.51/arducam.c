// SPDX-License-Identifier: GPL-2.0
/*
 * A V4L2 driver for Sony arducam cameras.
 * Copyright (C) 2019, Raspberry Pi (Trading) Ltd
 *
 * Based on Sony imx258 camera driver
 * Copyright (C) 2018 Intel Corporation
 *
 * DT / fwnode changes, and regulator / GPIO control taken from ov5640.c
 * Copyright (C) 2011-2013 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright (C) 2014-2017 Mentor Graphics Inc.
 *
 */
#include "arducam.h"
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mediabus.h>
#include <asm/unaligned.h>

#define arducam_REG_VALUE_08BIT		1
#define arducam_REG_VALUE_16BIT		2
#define arducam_REG_VALUE_32BIT		4

#define arducam_REG_MODE_SELECT		0x0100
#define arducam_MODE_STANDBY		0x00
#define arducam_MODE_STREAMING		0x01

/* V_TIMING internal */
#define arducam_REG_VTS			0x0160
#define arducam_VTS_15FPS		0x0dc6
#define arducam_VTS_30FPS_1080P		0x06e3
#define arducam_VTS_30FPS_BINNED		0x06e3
#define arducam_VTS_MAX			0xffff

/*Frame Length Line*/
#define arducam_FLL_MIN			0x08a6
#define arducam_FLL_MAX			0xffff
#define arducam_FLL_STEP			1
#define arducam_FLL_DEFAULT		0x0c98

/* HBLANK control - read only */
#define arducam_PPL_DEFAULT		5352

/* Exposure control */
#define arducam_REG_EXPOSURE		0x015a
#define arducam_EXPOSURE_MIN		4
#define arducam_EXPOSURE_STEP		1
#define arducam_EXPOSURE_DEFAULT		0x640
#define arducam_EXPOSURE_MAX		65535

/* Analog gain control */
#define arducam_REG_ANALOG_GAIN		0x0157
#define arducam_ANA_GAIN_MIN		0
#define arducam_ANA_GAIN_MAX		232
#define arducam_ANA_GAIN_STEP		1
#define arducam_ANA_GAIN_DEFAULT		0x0

/* Digital gain control */
#define arducam_REG_DIGITAL_GAIN		0x0158
#define arducam_DGTL_GAIN_MIN		0x0100
#define arducam_DGTL_GAIN_MAX		0x0fff
#define arducam_DGTL_GAIN_DEFAULT	0x0100
#define arducam_DGTL_GAIN_STEP		1

/* Test Pattern Control */
#define arducam_REG_TEST_PATTERN		0x0600
#define arducam_TEST_PATTERN_DISABLE	0
#define arducam_TEST_PATTERN_SOLID_COLOR	1
#define arducam_TEST_PATTERN_COLOR_BARS	2
#define arducam_TEST_PATTERN_GREY_COLOR	3
#define arducam_TEST_PATTERN_PN9		4
static int debug = 0;

struct arducam_reg {
	u16 address;
	u8 val;
};

struct arducam_reg_list {
	u32 num_of_regs;
	const struct arducam_reg *regs;
};

/* Mode : resolution and related config&values */
struct arducam_mode {
	/* Frame width */
	u32 width;
	/* Frame height */
	u32 height;
	/* V-timing */
	u32 vts_def;
	/* Default register values */
	struct arducam_reg_list reg_list;
};




struct reg_value {
	u16 reg_addr;
	u8 val;
	u8 mask;
	u32 delay_ms;
};


enum ov5640_mode_id {
	OV5640_MODE_QCIF_176_144 = 0,
	OV5640_MODE_QVGA_320_240,
	OV5640_MODE_VGA_640_480,
	OV5640_MODE_NTSC_720_480,
	OV5640_MODE_PAL_720_576,
	OV5640_MODE_XGA_1024_768,
	OV5640_MODE_720P_1280_720,
	OV5640_MODE_1080P_1920_1080,
	OV5640_MODE_QSXGA_2592_1944,
	OV5640_NUM_MODES,
};
enum ov5640_downsize_mode {
	SUBSAMPLING,
	SCALING,
};
struct ov5640_mode_info {
	enum ov5640_mode_id id;
	enum ov5640_downsize_mode dn_mode;
	u32 hact;
	u32 htot;
	u32 vact;
	u32 vtot;
	const struct reg_value *reg_data;
	u32 reg_data_size;
};
static const struct arducam_reg mode_1920_1080_regs[] = {
};

static const char * const arducam_effect_menu[] = {
	"Normal",
	"Alien",
	"Antique",
	"Black/White",
	"Emboss",
	"Emboss/Color",
	"Grayscale",
	"Negative",
	"Blueish",
	"Greenish",
	"Redish",
	"Posterize 1",
	"Posterize 2",
	"Sepia 1",
	"Sepia 2",
	"Sketch",
	"Solarize",
	"Foggy",
};

static const char * const arducam_pan_menu[] = {
	"Center",
	"Top Left",
	"Top Right",
	"Bottom Left",
	"Bottom Right",
};
static const char * const arducam_zoom_menu[] = {
	"1X",
	"2X",
	"3X",
	"4X",
};
static const char * const arducam_pan_zoom_speed_menu[] = {
"Immediate",
"slow",
"fast",
};

static const char * const arducam_denoise_menu[] = {
"denoise = -8",
"denoise = -4",
"denoise = -2",
"denoise = -1",
"denoise = -0.5",
"denoise = 0",
"denoise = 0.5",
"denoise = 1",
"denoise = 2",
"denoise = 4",
"denoise = 8",
};

static const char * const arducam_test_pattern_menu[] = {
	"Disabled",
	"Color Bars",
	"Solid Color",
	"Grey Color Bars",
	"PN9"
};

static const int arducam_test_pattern_val[] = {
	arducam_TEST_PATTERN_DISABLE,
	arducam_TEST_PATTERN_COLOR_BARS,
	arducam_TEST_PATTERN_SOLID_COLOR,
	arducam_TEST_PATTERN_GREY_COLOR,
	arducam_TEST_PATTERN_PN9,
};

/* regulator supplies */
static const char * const arducam_supply_name[] = {
	/* Supplies can be enabled in any order */
	"VANA",  /* Analog (2.8V) supply */
	"VDIG",  /* Digital Core (1.8V) supply */
	"VDDL",  /* IF (1.2V) supply */
};

#define arducam_NUM_SUPPLIES ARRAY_SIZE(arducam_supply_name)

#define arducam_XCLR_DELAY_MS 10	/* Initialisation delay after XCLR low->high */
#define arducam_XCLR_MIN_DELAY_US	6200
#define arducam_XCLR_DELAY_RANGE_US	1000
/* Mode configs */
static const struct arducam_mode supported_modes[] = {
	{
		/* 1080P 30fps cropped */
		.width = 1920,
		.height = 1080,
		.vts_def = arducam_VTS_30FPS_1080P,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_1920_1080_regs),
			.regs = mode_1920_1080_regs,
		},
	},
};

struct arducam {
	struct v4l2_subdev sd;
	struct media_pad pad;

	struct v4l2_fwnode_endpoint ep; /* the parsed DT endpoint info */
	struct clk *xclk; /* system clock to arducam */
	u32 xclk_freq;
	struct gpio_desc *reset_gpio;
    struct i2c_client *client;
	struct arducam_format *supported_formats;
	int num_supported_formats;
	int current_format_idx;
	int current_resolution_idx;
	int lanes;
	struct gpio_desc *xclr_gpio;
	struct regulator_bulk_data supplies[arducam_NUM_SUPPLIES];

	struct v4l2_ctrl_handler ctrl_handler;
	/* V4L2 Controls */
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *exposure;

	/* Current mode */
	const struct arducam_mode *mode;

	/*
	 * Mutex for serialized access:
	 * Protect sensor module set pad format and start/stop streaming safely.
	 */
	struct mutex mutex;

	int power_count;
	/* Streaming on/off */
	bool streaming;
	struct v4l2_ctrl *ctrls[32];
};

static inline struct arducam *to_arducam(struct v4l2_subdev *_sd)
{
	return container_of(_sd, struct arducam, sd);
}

/* Write registers up to 2 at a time */
static int arducam_write_reg(struct arducam *arducam, u16 reg, u32 len, u32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&arducam->sd);
	u8 buf[6];

	if (len > 4)
		return -EINVAL;

	put_unaligned_be16(reg, buf);
	put_unaligned_be32(val << (8 * (4 - len)), buf + 2);
	if (i2c_master_send(client, buf, len + 2) != len + 2)
		return -EIO;

	return 0;
}


static int arducam_readl_reg(struct i2c_client *client,
								   u16 addr, u32 *val)
{
    u16 buf = htons(addr);
    u32 data;
    struct i2c_msg msgs[2] = {
		{
			.addr = client->addr,
			.flags= 0,
			.len = 2,
			.buf = (u8 *)&buf,
		},
		{
			.addr = client->addr,
			.flags= I2C_M_RD,
			.len = 4,
			.buf = (u8 *)&data,
		},
	};

	if(i2c_transfer(client->adapter, msgs, 2) != 2){
		return -1;
	}

	*val = ntohl(data);

	return 0;
}

static int arducam_writel_reg(struct i2c_client *client,
									u16 addr, u32 val)
{
	u8 data[6];
	struct i2c_msg msgs[2] = {
		{
			.addr = client->addr,
			.flags= 0,
			.len = 6,
			.buf = data,
		},
	};
	addr = htons(addr);
	val = htonl(val);
	memcpy(data, &addr, 2);
	memcpy(data + 2, &val, 4);

	if(i2c_transfer(client->adapter, msgs, 1) != 1)
		return -1;

	return 0;
}

int arducam_read(struct i2c_client *client, u16 addr, u32 *value)
{
	int ret;
	int count = 0;
	while (count++ < I2C_READ_RETRY_COUNT) {
		ret = arducam_readl_reg(client, addr, value);
		if(!ret) {
			v4l2_dbg(1, debug, client, "%s: 0x%02x 0x%04x\n",
				__func__, addr, *value);
			return ret;
		}
	}
	
	v4l2_err(client, "%s: Reading register 0x%02x failed\n",
			 __func__, addr);
	return ret;
}

int arducam_write(struct i2c_client *client, u16 addr, u32 value)
{
	int ret;
	int count = 0;
	while (count++ < I2C_WRITE_RETRY_COUNT) {
		ret = arducam_writel_reg(client, addr, value);
		if(!ret)
			return ret;
	}
	v4l2_err(client, "%s: Write 0x%04x to register 0x%02x failed\n",
			 __func__, value, addr);
	return ret;
}

#if 0
/* Power/clock management functions */
static void arducam_power(struct arducam *arducam, bool enable)
{
	gpiod_set_value_cansleep(arducam->xclr_gpio, enable ? 1 : 0);
}

static int arducam_set_power_on(struct arducam *arducam)
{
	struct i2c_client *client = v4l2_get_subdevdata(&arducam->sd);
	int ret;

	ret = clk_prepare_enable(arducam->xclk);
	if (ret) {
		dev_err(&client->dev, "%s: failed to enable clock\n",
			__func__);
		return ret;
	}

	ret = regulator_bulk_enable(arducam_NUM_SUPPLIES,
				    arducam->supplies);
	if (ret) {
		dev_err(&client->dev, "%s: failed to enable regulators\n",
			__func__);
		goto xclk_off;
	}

	arducam_power(arducam, true);
	msleep(arducam_XCLR_DELAY_MS);

	return 0;
xclk_off:
	clk_disable_unprepare(arducam->xclk);
	return ret;
}

static void arducam_set_power_off(struct arducam *arducam)
{
	arducam_power(arducam, false);
	regulator_bulk_disable(arducam_NUM_SUPPLIES, arducam->supplies);
	clk_disable_unprepare(arducam->xclk);
}

static int arducam_set_power(struct arducam *arducam, bool on)
{
	int ret = 0;

	if (on) {
		ret = arducam_set_power_on(arducam);
		if (ret)
			return ret;
	} else {
		arducam_set_power_off(arducam);
	}

	return 0;
}

/* Open sub-device */
static int arducam_s_power(struct v4l2_subdev *sd, int on)
{
	struct arducam *arducam = to_arducam(sd);
	int ret = 0;

	mutex_lock(&arducam->mutex);

	/*
	 * If the power count is modified from 0 to != 0 or from != 0 to 0,
	 * update the power state.
	 */
	if (arducam->power_count == !on) {
		ret = arducam_set_power(arducam, !!on);
		if (ret)
			goto out;
	}

	/* Update the power count. */
	arducam->power_count += on ? 1 : -1;
	WARN_ON(arducam->power_count < 0);
out:
	mutex_unlock(&arducam->mutex);

	return ret;
}

#endif


/* Power/clock management functions */
static int arducam_power_on(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct arducam *arducam = to_arducam(sd);
	int ret;

	ret = regulator_bulk_enable(arducam_NUM_SUPPLIES,
				    arducam->supplies);
	if (ret) {
		dev_err(&client->dev, "%s: failed to enable regulators\n",
			__func__);
		return ret;
	}

	ret = clk_prepare_enable(arducam->xclk);
	if (ret) {
		dev_err(&client->dev, "%s: failed to enable clock\n",
			__func__);
		goto reg_off;
	}

	gpiod_set_value_cansleep(arducam->reset_gpio, 1);
	usleep_range(arducam_XCLR_MIN_DELAY_US,
		     arducam_XCLR_MIN_DELAY_US + arducam_XCLR_DELAY_RANGE_US);

	return 0;

reg_off:
	regulator_bulk_disable(arducam_NUM_SUPPLIES, arducam->supplies);

	return ret;
}

static int arducam_power_off(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct arducam *arducam = to_arducam(sd);

	gpiod_set_value_cansleep(arducam->reset_gpio, 0);
	regulator_bulk_disable(arducam_NUM_SUPPLIES, arducam->supplies);
	clk_disable_unprepare(arducam->xclk);

	return 0;
}

static int arducam_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct v4l2_mbus_framefmt *try_fmt =
		v4l2_subdev_get_try_format(sd, fh->pad, 0);

	/* Initialize try_fmt */
	try_fmt->width = supported_modes[0].width;
	try_fmt->height = supported_modes[0].height;
	try_fmt->code = MEDIA_BUS_FMT_SBGGR10_1X10;
	try_fmt->field = V4L2_FIELD_NONE;

	return 0;
}

static int arducam_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct arducam *arducam =
		container_of(ctrl->handler, struct arducam, ctrl_handler);
	struct i2c_client *client = v4l2_get_subdevdata(&arducam->sd);
	int ret = 0;

	/*
	 * Applying V4L2 control value only happens
	 * when power is up for streaming
	 */
	if (pm_runtime_get_if_in_use(&client->dev) == 0)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN:
		ret = arducam_write_reg(arducam, arducam_REG_ANALOG_GAIN,
				       arducam_REG_VALUE_08BIT, ctrl->val);
		break;
	case V4L2_CID_EXPOSURE:
		ret = arducam_write_reg(arducam, arducam_REG_EXPOSURE,
				       arducam_REG_VALUE_16BIT, ctrl->val);
		break;
	case V4L2_CID_DIGITAL_GAIN:
		ret = arducam_write_reg(arducam, arducam_REG_DIGITAL_GAIN,
				       arducam_REG_VALUE_16BIT, ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = arducam_write_reg(arducam, arducam_REG_TEST_PATTERN,
				       arducam_REG_VALUE_16BIT,
				       arducam_test_pattern_val[ctrl->val]);
		break;
	default:
		dev_info(&client->dev,
			 "ctrl(id:0x%x,val:0x%x) is not handled\n",
			 ctrl->id, ctrl->val);
		ret = -EINVAL;
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static int arducam_s_ctrl(struct v4l2_ctrl *ctrl)
{
	int ret;
	struct arducam *priv = 
		container_of(ctrl->handler, struct arducam, ctrl_handler);

	v4l2_dbg(1, debug, priv->client, "%s: cid = (0x%X), value = (%d).\n",
			 __func__, ctrl->id, ctrl->val);
		ret = arducam_write(priv->client, CTRL_ID_REG, ctrl->id);
		ret += arducam_write(priv->client, CTRL_VALUE_REG, ctrl->val);
		if (ret < 0)
			return -EINVAL;
	
	return 0;
}



//static const struct v4l2_ctrl_ops arducam_ctrl_ops = {
//	.s_ctrl = arducam_set_ctrl,
//};
static const struct v4l2_ctrl_ops arducam_ctrl_ops = {
	.s_ctrl = arducam_s_ctrl,
};


static int arducam_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	/* Only one bayer order(GRBG) is supported */
	if (code->index > 0)
		return -EINVAL;
	code->code = MEDIA_BUS_FMT_UYVY8_1X16;//MEDIA_BUS_FMT_SBGGR10_1X10;
	return 0;
}

static int arducam_csi2_enum_mbus_code(
			struct v4l2_subdev *sd,
			struct v4l2_subdev_pad_config *cfg,
			struct v4l2_subdev_mbus_code_enum *code)
{
	struct arducam *priv = to_arducam(sd);
	struct arducam_format *supported_formats = priv->supported_formats;
	int num_supported_formats = priv->num_supported_formats;
	v4l2_dbg(1, debug, sd, "%s: index = (%d)\n", __func__, code->index);
	if (code->index >= num_supported_formats)
		return -EINVAL;
	// code->code = MEDIA_BUS_FMT_Y8_1X8;
	code->code = supported_formats[code->index].mbus_code;

	return 0;
}

static int arducam_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fse->code != MEDIA_BUS_FMT_UYVY8_1X16)
		return -EINVAL;
	fse->min_width = supported_modes[fse->index].width;
	fse->max_width = fse->min_width;
	fse->min_height = supported_modes[fse->index].height;
	fse->max_height = fse->min_height;

	return 0;
}
static int arducam_csi2_enum_framesizes(
			struct v4l2_subdev *sd,
			struct v4l2_subdev_pad_config *cfg,
			struct v4l2_subdev_frame_size_enum *fse)
{
	int i;
	struct arducam *priv = to_arducam(sd);

	struct arducam_format *supported_formats = priv->supported_formats;

	int num_supported_formats = priv->num_supported_formats;

	v4l2_dbg(1, debug, sd, "%s: code = (0x%X), index = (%d)\n",
			 __func__, fse->code, fse->index);

	for (i = 0; i < num_supported_formats; i++) {
		if (fse->code == supported_formats[i].mbus_code) {
			if (fse->index >= supported_formats[i].num_resolution_set)
				return -EINVAL;
			fse->min_width = fse->max_width =
				supported_formats[i].resolution_set[fse->index].width;
			fse->min_height = fse->max_height =
				supported_formats[i].resolution_set[fse->index].height;
			return 0;
		}
	}
	return -EINVAL;
}
enum arducam_frame_rate {
	ARDUCAM_5_FPS = 0,
	ARDUCAM_NUM_FRAMERATES,
};
static int arducam_csi2_enum_frame_interval(	
	struct v4l2_subdev *sd,
	struct v4l2_subdev_pad_config *cfg,
	struct v4l2_subdev_frame_interval_enum *fie)
{
	int i;
	int resolution_index = 0;
	struct arducam *priv = to_arducam(sd);
	struct arducam_format *supported_formats = priv->supported_formats;
	int num_supported_formats = priv->num_supported_formats;
	v4l2_dbg(1, debug, sd, "%s: code = (0x%X), index = (%d)\n",
			 __func__, fie->code, fie->index);

	for (i = 0; i < num_supported_formats; i++) {
		if (fie->code == supported_formats[i].mbus_code) {
			if (fie->index >= 1)
				return -EINVAL;
			//printk("supported_formats[i].num_resolution_set:%d\r\n",supported_formats[i].num_resolution_set);
		
			fie->interval.denominator = supported_formats[i].resolution_set[supported_formats[i].num_frame_rate++].frame_rate;
			fie->interval.numerator = 1;
		//	printk("supported_formats[i].num_frame_rate: %d\r\n",supported_formats[i].num_frame_rate);
			if (supported_formats[i].num_frame_rate >= supported_formats[i].num_resolution_set){
					supported_formats[i].num_frame_rate = 0;
			}
			return 0;
		}
	}
	return -EINVAL;
}

static void arducam_update_pad_format(const struct arducam_mode *mode,
				     struct v4l2_subdev_format *fmt)
{
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.code = MEDIA_BUS_FMT_UYVY8_1X16;
	fmt->format.field = V4L2_FIELD_NONE;
}


static int arducam_csi2_get_fmt(struct v4l2_subdev *sd,
								struct v4l2_subdev_pad_config *cfg,
								struct v4l2_subdev_format *format)
{
	struct arducam *priv = to_arducam(sd);
	
	
	mutex_lock(&priv->mutex);
	struct arducam_format *current_format = 
		&priv->supported_formats[priv->current_format_idx];
	format->format.width =
		current_format->resolution_set[priv->current_resolution_idx].width;
	format->format.height =
		current_format->resolution_set[priv->current_resolution_idx].height;
	format->format.code = current_format->mbus_code;
	format->format.field = V4L2_FIELD_NONE;
	format->format.colorspace = V4L2_COLORSPACE_SRGB;

	v4l2_dbg(1, debug, sd, "%s: width: (%d) height: (%d) code: (0x%X)\n",
		__func__, format->format.width,format->format.height,
			format->format.code);
	mutex_unlock(&priv->mutex);
	return 0;
}

static int arducam_set_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_format *fmt)
{
	struct arducam *arducam = to_arducam(sd);
	const struct arducam_mode *mode;
	struct v4l2_mbus_framefmt *framefmt;

	mutex_lock(&arducam->mutex);

	/* Only one raw bayer(BGGR) order is supported */
	fmt->format.code = MEDIA_BUS_FMT_UYVY8_1X16;//MEDIA_BUS_FMT_SBGGR10_1X10;
	mode = v4l2_find_nearest_size(supported_modes,
				      ARRAY_SIZE(supported_modes),
				      width, height,
				      fmt->format.width, fmt->format.height);
	arducam_update_pad_format(mode, fmt);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		framefmt = v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
		*framefmt = fmt->format;
	} else {
		arducam->mode = mode;
	}

	mutex_unlock(&arducam->mutex);

	return 0;
}
static int arducam_csi2_get_fmt_idx_by_code(struct arducam *priv,
											u32 mbus_code)
{
	int i;
	struct arducam_format *formats = priv->supported_formats;
	for (i = 0; i < priv->num_supported_formats; i++) {
		if (formats[i].mbus_code == mbus_code)
			return i; 
	}
	return -EINVAL;
}
static int arducam_csi2_set_fmt(struct v4l2_subdev *sd,
								struct v4l2_subdev_pad_config *cfg,
								struct v4l2_subdev_format *format)
{
	int i, j;
	struct arducam *priv = to_arducam(sd);
	struct arducam_format *supported_formats = priv->supported_formats;

	//show_csi_params(sd);
	format->format.colorspace = V4L2_COLORSPACE_SRGB;
	format->format.field = V4L2_FIELD_NONE;

	v4l2_dbg(1, debug, sd, "%s: code: 0x%X, width: %d, height: %d\n",
			 __func__, format->format.code, format->format.width,
			 	format->format.height);

	i = arducam_csi2_get_fmt_idx_by_code(priv, format->format.code);
	if (i < 0)
		return -EINVAL;

	for (j = 0; j < supported_formats[i].num_resolution_set; j++) {
		if (supported_formats[i].resolution_set[j].width 
				== format->format.width && 
			supported_formats[i].resolution_set[j].height
				== format->format.height) {

			v4l2_dbg(1, debug, sd, "%s: format match.\n", __func__);
			v4l2_dbg(1, debug, sd, "%s: set format to device: %d %d.\n",
				__func__, supported_formats[i].index, j);

			arducam_write(priv->client, PIXFORMAT_INDEX_REG,
				supported_formats[i].index);
			arducam_write(priv->client, RESOLUTION_INDEX_REG, j);
            
			priv->current_format_idx = i;
			priv->current_resolution_idx = j;

			return 0;
		}
	}
	format->format.width = supported_formats[i].resolution_set[0].width;
	format->format.height = supported_formats[i].resolution_set[0].height;

	arducam_write(priv->client, PIXFORMAT_INDEX_REG,
		supported_formats[i].index);
	arducam_write(priv->client, RESOLUTION_INDEX_REG, 0);

	priv->current_format_idx = i;
	priv->current_resolution_idx = 0;

	return 0;
}

/* Start streaming */
static int arducam_start_streaming(struct arducam *arducam)
{
	struct i2c_client *client = v4l2_get_subdevdata(&arducam->sd);
	const struct arducam_reg_list *reg_list;
	int ret;
	/* Apply customized values from user */
	ret =  __v4l2_ctrl_handler_setup(arducam->sd.ctrl_handler);
	if (ret)
		return ret;

	/* set stream on register */
	return arducam_write_reg(arducam, arducam_REG_MODE_SELECT,
				arducam_REG_VALUE_32BIT, arducam_MODE_STREAMING);
}

/* Stop streaming */
static int arducam_stop_streaming(struct arducam *arducam)
{
	struct i2c_client *client = v4l2_get_subdevdata(&arducam->sd);
	int ret;

	/* set stream off register */
	ret = arducam_write_reg(arducam, arducam_REG_MODE_SELECT,
			       arducam_REG_VALUE_32BIT, arducam_MODE_STANDBY);
	if (ret)
		dev_err(&client->dev, "%s failed to set stream\n", __func__);

	/*
	 * Return success even if it was an error, as there is nothing the
	 * caller can do about it.
	 */
	return 0;
}

#if 0
static int arducam_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct arducam *arducam = to_arducam(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;
	mutex_lock(&arducam->mutex);
	if (enable) {
		//printk("start streaming\r\n");
		ret = arducam_start_streaming(arducam);
		if (ret) {
			pm_runtime_put(&client->dev);
			goto err_unlock;
		}
	} else {
		//printk("stop streaming\r\n");
		arducam_stop_streaming(arducam);
		pm_runtime_put(&client->dev);
	}
	arducam->streaming = enable;
	mutex_unlock(&arducam->mutex);
	return ret;
err_unlock:
	mutex_unlock(&arducam->mutex);
	return ret;
}
#endif 


static int arducam_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct arducam *arducam = to_arducam(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

	mutex_lock(&arducam->mutex);
	if (arducam->streaming == enable) {
		mutex_unlock(&arducam->mutex);
		return 0;
	}

	if (enable) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto err_unlock;
		}

		/*
		 * Apply default & customized values
		 * and then start streaming.
		 */
		ret = arducam_start_streaming(arducam);
		if (ret)
			goto err_rpm_put;
	} else {
		arducam_stop_streaming(arducam);
		pm_runtime_put(&client->dev);
	}

	arducam->streaming = enable;

	/* vflip and hflip cannot change during streaming */
	// __v4l2_ctrl_grab(arducam->vflip, enable);
	// __v4l2_ctrl_grab(arducam->hflip, enable);

	mutex_unlock(&arducam->mutex);

	return ret;

err_rpm_put:
	pm_runtime_put(&client->dev);
err_unlock:
	mutex_unlock(&arducam->mutex);

	return ret;
}

static int __maybe_unused arducam_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct arducam *arducam = to_arducam(sd);

	if (arducam->streaming)
		arducam_stop_streaming(arducam);

	return 0;
}

static int __maybe_unused arducam_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct arducam *arducam = to_arducam(sd);
	int ret;

	if (arducam->streaming) {
		ret = arducam_start_streaming(arducam);
		if (ret)
			goto error;
	}

	return 0;

error:
	arducam_stop_streaming(arducam);
	arducam->streaming = 0;
	return ret;
}

static int arducam_get_regulators(struct arducam *arducam)
{
	struct i2c_client *client = v4l2_get_subdevdata(&arducam->sd);
	int i;

	for (i = 0; i < arducam_NUM_SUPPLIES; i++)
		arducam->supplies[i].supply = arducam_supply_name[i];

	return devm_regulator_bulk_get(&client->dev,
				       arducam_NUM_SUPPLIES,
				       arducam->supplies);
}

static const struct v4l2_subdev_core_ops arducam_core_ops = {
	//.s_power = arducam_s_power,
};

static const struct v4l2_subdev_video_ops arducam_video_ops = {
	//.g_frame_interval = arducam_g_frame_interval,
//	.s_frame_interval = arducam_s_frame_interval,
	.s_stream = arducam_set_stream,
};

static const struct v4l2_subdev_pad_ops arducam_pad_ops = {
	.enum_mbus_code = arducam_csi2_enum_mbus_code,//arducam_enum_mbus_code,
	.get_fmt = arducam_csi2_get_fmt,//arducam_get_pad_format, //
	.set_fmt = arducam_csi2_set_fmt,//arducam_set_pad_format,//
	.enum_frame_size = arducam_csi2_enum_framesizes,//arducam_enum_frame_size,
	.enum_frame_interval = arducam_csi2_enum_frame_interval,
};

static const struct v4l2_subdev_ops arducam_subdev_ops = {
	.core = &arducam_core_ops,
	.video = &arducam_video_ops,
	.pad = &arducam_pad_ops,
};

static const struct v4l2_subdev_internal_ops arducam_internal_ops = {
	.open = arducam_open,
};

/* Initialize control handlers */
static int arducam_init_controls(struct arducam *arducam)
{
	struct i2c_client *client = v4l2_get_subdevdata(&arducam->sd);
	struct v4l2_ctrl_handler *ctrl_hdlr;
	int ret;

	ctrl_hdlr = &arducam->ctrl_handler;
	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 8);
	if (ret)
		return ret;

	mutex_init(&arducam->mutex);
	ctrl_hdlr->lock = &arducam->mutex;

	arducam->exposure = v4l2_ctrl_new_std(ctrl_hdlr, &arducam_ctrl_ops,
					     V4L2_CID_EXPOSURE,
					     arducam_EXPOSURE_MIN,
					     arducam_EXPOSURE_MAX,
					     arducam_EXPOSURE_STEP,
					     arducam_EXPOSURE_DEFAULT);

	v4l2_ctrl_new_std(ctrl_hdlr, &arducam_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
			  arducam_ANA_GAIN_MIN, arducam_ANA_GAIN_MAX,
			  arducam_ANA_GAIN_STEP, arducam_ANA_GAIN_DEFAULT);

	v4l2_ctrl_new_std(ctrl_hdlr, &arducam_ctrl_ops, V4L2_CID_DIGITAL_GAIN,
			  arducam_DGTL_GAIN_MIN, arducam_DGTL_GAIN_MAX,
			  arducam_DGTL_GAIN_STEP, arducam_DGTL_GAIN_DEFAULT);

	v4l2_ctrl_new_std_menu_items(ctrl_hdlr, &arducam_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(arducam_test_pattern_menu) - 1,
				     0, 0, arducam_test_pattern_menu);

	if (ctrl_hdlr->error) {
		ret = ctrl_hdlr->error;
		dev_err(&client->dev, "%s control init failed (%d)\n",
			__func__, ret);
		goto error;
	}

	arducam->sd.ctrl_handler = ctrl_hdlr;

	return 0;

error:
	v4l2_ctrl_handler_free(ctrl_hdlr);
	mutex_destroy(&arducam->mutex);

	return ret;
}

static void arducam_free_controls(struct arducam *arducam)
{
	v4l2_ctrl_handler_free(arducam->sd.ctrl_handler);
	mutex_destroy(&arducam->mutex);
}

static int arducam_get_length_of_set(struct i2c_client *client,
									u16 idx_reg, u16 val_reg)
{
	int ret;
	int index = 0;
	u32 val;
	while (1) {
		ret = arducam_write(client, idx_reg, index);
		ret += arducam_read(client, val_reg, &val);

		if (ret < 0)
			return -1;

		if (val == NO_DATA_AVAILABLE)
			break;
		index++;
	}
	arducam_write(client, idx_reg, 0);
	return index;
}
static int is_raw(int pixformat)
{
	return pixformat >= 0x28 && pixformat <= 0x2D;
}
static u32 bayer_to_mbus_code(int data_type, int bayer_order)
{
	const uint32_t depth8[] = {
        MEDIA_BUS_FMT_SBGGR8_1X8,
		MEDIA_BUS_FMT_SGBRG8_1X8,
        MEDIA_BUS_FMT_SGRBG8_1X8,
		MEDIA_BUS_FMT_SRGGB8_1X8,
		MEDIA_BUS_FMT_Y8_1X8,
	};
    const uint32_t depth10[] = {
        MEDIA_BUS_FMT_SBGGR10_1X10,
		MEDIA_BUS_FMT_SGBRG10_1X10,
        MEDIA_BUS_FMT_SGRBG10_1X10,
		MEDIA_BUS_FMT_SRGGB10_1X10,
		MEDIA_BUS_FMT_Y10_1X10,
	};
    const uint32_t depth12[] = {
        MEDIA_BUS_FMT_SBGGR12_1X12,
		MEDIA_BUS_FMT_SGBRG12_1X12,
        MEDIA_BUS_FMT_SGRBG12_1X12,
		MEDIA_BUS_FMT_SRGGB12_1X12,
		MEDIA_BUS_FMT_Y12_1X12,
	};
    // const uint32_t depth16[] = {
	// 	MEDIA_BUS_FMT_SBGGR16_1X16,
	// 	MEDIA_BUS_FMT_SGBRG16_1X16,
    //     MEDIA_BUS_FMT_SGRBG16_1X16,
	// 	MEDIA_BUS_FMT_SRGGB16_1X16,
    // };
    if (bayer_order < 0 || bayer_order > 4) {
        return 0;
    }

    switch (data_type) {
    case IMAGE_DT_RAW8:
        return depth8[bayer_order];
    case IMAGE_DT_RAW10:
        return depth10[bayer_order];
    case IMAGE_DT_RAW12:
        return depth12[bayer_order];
    }
    return 0;
}
static u32 yuv422_to_mbus_code(int data_type, int order)
{
	const uint32_t depth8[] = {
        MEDIA_BUS_FMT_YUYV8_1X16,
		MEDIA_BUS_FMT_YVYU8_1X16,
        MEDIA_BUS_FMT_UYVY8_1X16,
		MEDIA_BUS_FMT_VYUY8_1X16,
	};

	const uint32_t depth10[] = {
        MEDIA_BUS_FMT_YUYV10_1X20,
		MEDIA_BUS_FMT_YVYU10_1X20,
		MEDIA_BUS_FMT_UYVY10_1X20,
		MEDIA_BUS_FMT_VYUY10_1X20,
	};

	if (order < 0 || order > 3) {
        return 0;
    }

	switch(data_type) {
	case IMAGE_DT_YUV422_8:
		return depth8[order];
	case IMAGE_DT_YUV422_10:
		return depth10[order];
	}
    return 0;
}

static u32 data_type_to_mbus_code(int data_type, int bayer_order)
{
    if(is_raw(data_type)) {
		return bayer_to_mbus_code(data_type, bayer_order);
	}

	switch(data_type) {
	case IMAGE_DT_YUV422_8:
	case IMAGE_DT_YUV422_10:
		return yuv422_to_mbus_code(data_type, bayer_order);
	case IMAGE_DT_RGB565:
		//printk("The image id is IMAGE_DT_RGB565\r\n");
		return MEDIA_BUS_FMT_RGB565_2X8_LE;//MEDIA_BUS_FMT_RGB565_1X16;
	case IMAGE_DT_RGB888:
		return MEDIA_BUS_FMT_RGB888_1X24;//MEDIA_BUS_FMT_RGB565_1X16;
	}
	return 0;
}
static int arducam_enum_resolution(struct i2c_client *client,
								struct arducam_format *format)
{
	int index = 0;
	u32 width, height,frame_rate;
	int num_resolution = 0;
	int ret;
	
	num_resolution = arducam_get_length_of_set(client,
						RESOLUTION_INDEX_REG, FORMAT_WIDTH_REG);
	if (num_resolution < 0)
		goto err;

	format->resolution_set = devm_kzalloc(&client->dev,
			sizeof(*(format->resolution_set)) * num_resolution, GFP_KERNEL);
	while (1) {
		ret = arducam_write(client, RESOLUTION_INDEX_REG, index);
		ret += arducam_read(client, FORMAT_WIDTH_REG, &width);
		ret += arducam_read(client, FORMAT_HEIGHT_REG, &height);
		ret += arducam_read(client, FORMAT_FRAMERATE_REG, &frame_rate);
		if (ret < 0)
			goto err;

		if (width == NO_DATA_AVAILABLE || height == NO_DATA_AVAILABLE)
			break;

		format->resolution_set[index].width = width;
		format->resolution_set[index].height= height;
		format->resolution_set[index].frame_rate = frame_rate;
		index++;
	}
	format->num_resolution_set = index;
    arducam_write(client, RESOLUTION_INDEX_REG, 0);
	return 0;
err:
	return -ENODEV;
}

static int arducam_add_extension_pixformat(struct arducam *priv)
{
	int i;
	struct arducam_format *formats = priv->supported_formats;
	for (i = 0; i < priv->num_supported_formats; i++) {
		switch (formats[i].mbus_code){
		case MEDIA_BUS_FMT_SBGGR10_1X10:
		case MEDIA_BUS_FMT_SGBRG10_1X10:
        case MEDIA_BUS_FMT_SGRBG10_1X10:
		case MEDIA_BUS_FMT_SRGGB10_1X10:
		case MEDIA_BUS_FMT_Y10_1X10:
			formats[priv->num_supported_formats] = formats[i];
			formats[priv->num_supported_formats].mbus_code = 
				MEDIA_BUS_FMT_ARDUCAM_Y102Y16_1x16;
			priv->num_supported_formats++;
			return 0;
        case MEDIA_BUS_FMT_SBGGR12_1X12:
		case MEDIA_BUS_FMT_SGBRG12_1X12:
        case MEDIA_BUS_FMT_SGRBG12_1X12:
		case MEDIA_BUS_FMT_SRGGB12_1X12:
		case MEDIA_BUS_FMT_Y12_1X12:
			formats[priv->num_supported_formats] = formats[i];
			formats[priv->num_supported_formats].mbus_code = 
				MEDIA_BUS_FMT_ARDUCAM_Y122Y16_1x16;
			priv->num_supported_formats++;
			return 0;
		}
	}
	return -1;
}


static int arducam_enum_pixformat(struct arducam *priv)
{
	int ret = 0;
	u32 mbus_code = 0;
	int pixformat_type;
	int bayer_order;
	int lanes;
	int index = 0;
	int num_pixformat = 0;
	struct i2c_client *client = priv->client;

	num_pixformat = arducam_get_length_of_set(client,
						PIXFORMAT_INDEX_REG, PIXFORMAT_TYPE_REG);

	if (num_pixformat < 0)
		goto err;

	priv->supported_formats = devm_kzalloc(&client->dev,
		sizeof(*(priv->supported_formats)) * (num_pixformat + 1), GFP_KERNEL);

	while (1) {
		ret = arducam_write(client, PIXFORMAT_INDEX_REG, index);
		ret += arducam_read(client, PIXFORMAT_TYPE_REG, &pixformat_type);

		if (pixformat_type == NO_DATA_AVAILABLE)
			break;

		ret += arducam_read(client, MIPI_LANES_REG, &lanes);
		if (lanes == NO_DATA_AVAILABLE)
			break;

		// if (is_raw(pixformat_type))
		ret += arducam_read(client, PIXFORMAT_ORDER_REG, &bayer_order);
		if (ret < 0)
			goto err;
		//printk("pixformat_type: %x, bayer_order:%x\r\n",pixformat_type,bayer_order);
		mbus_code = data_type_to_mbus_code(pixformat_type, bayer_order);
		//printk("mbus_code: %x\r\n",mbus_code);
		priv->supported_formats[index].index = index;
		priv->supported_formats[index].mbus_code = mbus_code;
		priv->supported_formats[index].data_type = pixformat_type;
		if (arducam_enum_resolution(client,
				&priv->supported_formats[index]))
			goto err;

		index++;
	}
	arducam_write(client, PIXFORMAT_INDEX_REG, 0);
	priv->num_supported_formats = index;
	priv->current_format_idx = 0;
	priv->current_resolution_idx = 0;
	priv->lanes = lanes;
	arducam_add_extension_pixformat(priv);
	return 0;

err:
	return -ENODEV;
}
static const char *arducam_ctrl_get_name(u32 id) {
	switch(id) {
	case V4L2_CID_ARDUCAM_EXT_TRI:
		return "trigger_mode";
	case V4L2_CID_ARDUCAM_FACE_DETECTION:
		return "face_detection";
	case V4L2_CID_EXPOSURE_AUTO:
		return "exposure_auto";
	case V4L2_CID_ARDUCAM_IRCUT:
		return "ircut";
	case V4L2_CID_ARDUCAM_FRAME_RATE:
		return "frame_rate";
	case V4L2_CID_ARDUCAM_EFFECTS:
		return "effects";
	case V4L2_CID_PAN_ABSOLUTE:
		return "pan";
	case V4L2_CID_ZOOM_ABSOLUTE:
		return "zoom";
	case V4L2_CID_ARDUCAM_PAN_X_ABSOLUTE:
		return "Pan Horizontal";
	case V4L2_CID_ARDUCAM_PAN_Y_ABSOLUTE:
		return "Pan Vertical";
	case V4L2_CID_ARDUCAM_ZOOM_PAN_SPEED:
		return "pan_zoom_speed";
	case V4L2_CID_ARDUCAM_HDR:
		return "hdr";
	case V4L2_CID_ARDUCAM_DENOISE:
		return "denoise";
	default:
		return NULL;
	}
}
enum v4l2_ctrl_type arducam_get_v4l2_ctrl_type(u32 id) {
	switch(id) {
	case V4L2_CID_ARDUCAM_EXT_TRI:
		return V4L2_CTRL_TYPE_BOOLEAN;
	case V4L2_CID_ARDUCAM_FACE_DETECTION:
		return V4L2_CTRL_TYPE_BOOLEAN;
	case V4L2_CID_EXPOSURE_AUTO:
		return V4L2_CTRL_TYPE_BOOLEAN;
	case V4L2_CID_ARDUCAM_IRCUT:
		return V4L2_CTRL_TYPE_BOOLEAN;
	case V4L2_CID_ARDUCAM_HDR:
		return V4L2_CTRL_TYPE_BOOLEAN;
	case V4L2_CID_ARDUCAM_FRAME_RATE:
		return V4L2_CTRL_TYPE_INTEGER;
	case V4L2_CID_ARDUCAM_EFFECTS:
		return V4L2_CTRL_TYPE_MENU;
	case V4L2_CID_PAN_ABSOLUTE:
		return V4L2_CTRL_TYPE_MENU;
	case V4L2_CID_ZOOM_ABSOLUTE:
		return V4L2_CTRL_TYPE_INTEGER;
	case V4L2_CID_ARDUCAM_PAN_X_ABSOLUTE:
		return V4L2_CTRL_TYPE_INTEGER;
	case V4L2_CID_ARDUCAM_PAN_Y_ABSOLUTE:
		return V4L2_CTRL_TYPE_INTEGER;
	case V4L2_CID_ARDUCAM_ZOOM_PAN_SPEED:
		return V4L2_CTRL_TYPE_MENU;
	case V4L2_CID_ARDUCAM_DENOISE:
		return V4L2_CTRL_TYPE_MENU;
	default:
		return V4L2_CTRL_TYPE_INTEGER;
	}
}
const char * const* arducam_get_v4l2_ctrl_menu(u32 id) {
	switch(id) {
	case V4L2_CID_ARDUCAM_EFFECTS:
		return arducam_effect_menu;
	case V4L2_CID_PAN_ABSOLUTE:
		return arducam_pan_menu;
	case V4L2_CID_ARDUCAM_ZOOM_PAN_SPEED:
		return arducam_pan_zoom_speed_menu;
	case V4L2_CID_ARDUCAM_DENOISE:
		return arducam_denoise_menu;
	default:
		return NULL;
	}
}

static struct v4l2_ctrl *v4l2_ctrl_new_arducam(struct v4l2_ctrl_handler *hdl,
			const struct v4l2_ctrl_ops *ops,
			u32 id, s64 min, s64 max, u64 step, s64 def)
{
	struct v4l2_ctrl_config cfg = {
		.ops = ops,
		.id = id,
		.name = NULL,
		.type = V4L2_CTRL_TYPE_BOOLEAN,//V4L2_CTRL_TYPE_INTEGER,
		.flags = 0,
		.min = min,
		.max = max,
		.def = def,
		.step = step,
	};
	cfg.name = arducam_ctrl_get_name(id);
	cfg.type = arducam_get_v4l2_ctrl_type(id);
	cfg.qmenu = arducam_get_v4l2_ctrl_menu(id);
	return v4l2_ctrl_new_custom(hdl, &cfg, NULL);
}
static int arducam_enum_controls(struct arducam *priv)
{
	int ret;
	int index = 0;
	int i = 0;
	int num_ctrls = 0;
	struct v4l2_ctrl_handler *ctrl_hdlr;
	u32 id, min, max, def, step;
	struct i2c_client *client;
	ctrl_hdlr = &priv->ctrl_handler;
	client = priv->client;
	num_ctrls = arducam_get_length_of_set(client,
					CTRL_INDEX_REG, CTRL_ID_REG);
	if (num_ctrls < 0)
		goto err;
    v4l2_dbg(1, debug, priv->client, "%s: num_ctrls = %d\n",
				__func__,num_ctrls);
	ret = v4l2_ctrl_handler_init(ctrl_hdlr, num_ctrls);
	if(ret)
		return ret;
	  v4l2_dbg(1, debug, priv->client, "v4l2_ctrl_handler_init successfully\n",
				__func__);
	index = 0;
	while (1) {
		ret = arducam_write(client, CTRL_INDEX_REG, index);
		ret += arducam_read(client, CTRL_ID_REG, &id);
		ret += arducam_read(client, CTRL_MAX_REG, &max);
		ret += arducam_read(client, CTRL_MIN_REG, &min);
		ret += arducam_read(client, CTRL_DEF_REG, &def);
		ret += arducam_read(client, CTRL_STEP_REG, &step);
		if (ret < 0)
			goto err;
		if (id == NO_DATA_AVAILABLE || max == NO_DATA_AVAILABLE ||
			min == NO_DATA_AVAILABLE || def == NO_DATA_AVAILABLE ||
			step == NO_DATA_AVAILABLE)
			break;
		if (arducam_ctrl_get_name(id) != NULL) {
			priv->ctrls[index] = v4l2_ctrl_new_arducam(ctrl_hdlr,
						&arducam_ctrl_ops, id, min, max, step, def);
			v4l2_dbg(1, debug, priv->client, "%s: new custom ctrl, ctrl: %p.\n",
				__func__, priv->ctrls[index]);
		} else {
		v4l2_dbg(1, debug, priv->client, "%s: index = %x, id = %x, max = %x, min = %x\n",
				__func__, index, id, max, min);
			priv->ctrls[index] = v4l2_ctrl_new_std(ctrl_hdlr,
						&arducam_ctrl_ops, id,
						min, max, step, def);
		}
		index++;
	}
	priv->sd.ctrl_handler = ctrl_hdlr;
	v4l2_ctrl_handler_setup(ctrl_hdlr);
	arducam_write(client, CTRL_INDEX_REG, 0);
	return 0;
err:
	return -ENODEV;
}

static int arducam_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct fwnode_handle *endpoint;
	struct arducam *arducam;
    u32 device_id;
	int ret;
	arducam = devm_kzalloc(&client->dev, sizeof(*arducam), GFP_KERNEL);
	if (!arducam)
		return -ENOMEM;
	/* Initialize subdev */
	v4l2_i2c_subdev_init(&arducam->sd, client, &arducam_subdev_ops);
	arducam->client = client;

	/* Get CSI2 bus config */
	endpoint = fwnode_graph_get_next_endpoint(dev_fwnode(&client->dev),
						  NULL);
	if (!endpoint) {
		dev_err(dev, "endpoint node not found\n");
		return -EINVAL;
	}

	ret = v4l2_fwnode_endpoint_parse(endpoint, &arducam->ep);
	fwnode_handle_put(endpoint);
	if (ret) {
		dev_err(dev, "Could not parse endpoint\n");
		return ret;
	}

	/* Get system clock (xclk) */
	arducam->xclk = devm_clk_get(dev, "xclk");
	if (IS_ERR(arducam->xclk)) {
		dev_err(dev, "failed to get xclk\n");
		return PTR_ERR(arducam->xclk);
	}
	arducam->xclk_freq = clk_get_rate(arducam->xclk);
	if (arducam->xclk_freq != 24000000) {
		dev_err(dev, "xclk frequency not supported: %d Hz\n",
			arducam->xclk_freq);
		return -EINVAL;
	}
	ret = arducam_get_regulators(arducam);
	if (ret)
		return ret;

	/* request optional power down pin */
	// arducam->xclr_gpio = devm_gpiod_get_optional(dev, "xclr",
	// 					    GPIOD_OUT_HIGH);
	/* Request optional enable pin */
	arducam->reset_gpio = devm_gpiod_get_optional(dev, "reset",
						     GPIOD_OUT_HIGH);


		/*
	 * The sensor must be powered for imx219_identify_module()
	 * to be able to read the CHIP_ID register
	 */
	ret = arducam_power_on(dev);
	if (ret)
		return ret;


	ret = arducam_read(client, DEVICE_ID_REG, &device_id);
	if (ret || device_id != DEVICE_ID) {
		dev_err(&client->dev, "probe failed\n");
		ret = -ENODEV;
		goto error_power_off;
	}
	if (arducam_enum_pixformat(arducam)) {
		dev_err(&client->dev, "enum pixformat failed.\n");
		ret = -ENODEV;
		goto error_power_off;
	}
	
	if (arducam_enum_controls(arducam)) {
		dev_err(dev, "enum controls failed.\n");
		ret = -ENODEV;
		goto error_power_off;
	}
	/* Initialize subdev */
	arducam->sd.internal_ops = &arducam_internal_ops;
	arducam->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	arducam->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	/* Initialize source pad */
	arducam->pad.flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&arducam->sd.entity, 1, &arducam->pad);
	if (ret)
		goto error_handler_free;

	ret = v4l2_async_register_subdev_sensor_common(&arducam->sd);
	if (ret < 0)
		goto error_media_entity;

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	return 0;

error_media_entity:
	media_entity_cleanup(&arducam->sd.entity);

error_handler_free:
	arducam_free_controls(arducam);

error_power_off:
	arducam_power_off(dev);

	return ret;
}

static int arducam_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct arducam *arducam = to_arducam(sd);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	arducam_free_controls(arducam);

	pm_runtime_disable(&client->dev);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

static const struct dev_pm_ops arducam_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(arducam_suspend, arducam_resume)
	SET_RUNTIME_PM_OPS(arducam_power_off, arducam_power_on, NULL)
};

static const struct of_device_id arducam_dt_ids[] = {
	{ .compatible = "sony,arducam" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, arducam_dt_ids);

static struct i2c_driver arducam_i2c_driver = {
	.driver = {
		.name = "arducam",
		.of_match_table	= arducam_dt_ids,
		.pm = &arducam_pm_ops,
	},
	.probe = arducam_probe,
	.remove = arducam_remove,
};

module_i2c_driver(arducam_i2c_driver);

MODULE_AUTHOR("Arducam <www.arducam.com");
MODULE_DESCRIPTION("Arducam sensor v4l2 driver");
MODULE_LICENSE("GPL v2");
