/*
 * Support for mt9m114 Camera Sensor.
 *
 * Copyright (c) 2010 Intel Corporation. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */

#include "mt9m114.h"

static struct regulator *cam1_regulator, *cam2_regulator, *cam3_regulator;
#define to_mt9m114(sd)          container_of(sd, struct mt9m114_device, sd)


/*
 * TODO: use debug parameter to actually define when debug messages should
 * be printed.
 */
static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Debug level (0-1)");

static int mt9m114_t_vflip(struct v4l2_subdev *sd, int value);
static int mt9m114_t_hflip(struct v4l2_subdev *sd, int value);
static int
mt9m114_read_reg(struct i2c_client *client, u16 data_length, u32 reg, u32 *val)
{
	int err;
	struct i2c_msg msg[2];
	unsigned char data[4];
	//u16 *wreg;

	if (!client->adapter) {
		v4l2_err(client, "%s error, no client->adapter\n", __func__);
		return -ENODEV;
	}

	if (data_length != MISENSOR_8BIT && data_length != MISENSOR_16BIT
					 && data_length != MISENSOR_32BIT) {
		v4l2_err(client, "%s error, invalid data length\n", __func__);
		return -EINVAL;
	}

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].len = MSG_LEN_OFFSET;
	msg[0].buf = data;

	/* high byte goes out first */
	data[0] = (u16) (reg >> 8);
	data[1] = (u16) (reg & 0xff);

	msg[1].addr = client->addr;
	msg[1].len = data_length;
	msg[1].flags = I2C_M_RD;
	msg[1].buf = data;

	err = i2c_transfer(client->adapter, msg, 2);

	if (err >= 0) {
		*val = 0;
		/* high byte comes first */
		if (data_length == MISENSOR_8BIT)
			*val = data[0];
		else if (data_length == MISENSOR_16BIT)
			*val = data[1] + (data[0] << 8);
		else
			*val = data[3] + (data[2] << 8) +
			    (data[1] << 16) + (data[0] << 24);

		return 0;
	}

	dev_err(&client->dev, "read from offset 0x%x error %d", reg, err);
	return err;
}

static int
mt9m114_write_reg_test(struct i2c_client *client, u16 data_length, u16 reg, u32 val)
{
	int num_msg;
	struct i2c_msg msg;
	unsigned char data[6] = {0};
	u16 *wreg;
	int retry = 0;
	if (!client->adapter) {
		v4l2_err(client, "%s error, no client->adapter\n", __func__);
		return -ENODEV;
	}

	if (data_length != MISENSOR_8BIT && data_length != MISENSOR_16BIT
					 && data_length != MISENSOR_32BIT) {
		v4l2_err(client, "%s error, invalid data_length\n", __func__);
		return -EINVAL;
	}
	
	memset(&msg, 0, sizeof(msg));
again:	
	msg.addr = client->addr;
	msg.flags = 0;
	msg.len = 2 + data_length;
	msg.buf = data;

	data[0] = reg>>8;
	data[1] = reg&0xff;
	
	switch(data_length){
		case MISENSOR_8BIT:
			data[2] =val;
			break;
		case MISENSOR_16BIT:
			data[2] = val>>8;
			data[3] = val&0x00ff;
			break;
		case MISENSOR_32BIT:
			data[2] = val>>24;
			data[3] = (val>>16)&0x00ff;
			data[4] = (val>>8)&0x0000ff;
			data[5] = val&0x000000ff;
			break;
		default:
			printk("error val in I2C write length\n");
			break;
	}
	num_msg = i2c_transfer(client->adapter, &msg, 1);
	if (num_msg >= 0){			
		return num_msg;
	}
	 dev_err(&client->dev, "write error: wrote 0x%x to offset 0x%x error %d",
		val, reg, num_msg);
	if (retry <= I2C_RETRY_COUNT) {
		dev_err(&client->dev, "retrying... %d", retry);
		retry++;
		msleep(20);
		goto again;
	}
	return -EINVAL;
}


static int
mt9m114_write_reg(struct i2c_client *client, u16 data_length, u16 reg, u32 val)
{
	int num_msg;
	struct i2c_msg msg;
	unsigned char data[6] = {0};
	u16 *wreg;
	int retry = 0;

	if (!client->adapter) {
		v4l2_err(client, "%s error, no client->adapter\n", __func__);
		return -ENODEV;
	}

	if (data_length != MISENSOR_8BIT && data_length != MISENSOR_16BIT
					 && data_length != MISENSOR_32BIT) {
		v4l2_err(client, "%s error, invalid data_length\n", __func__);
		return -EINVAL;
	}

	memset(&msg, 0, sizeof(msg));

again:
	msg.addr = client->addr;
	msg.flags = 0;
	msg.len = 2 + data_length;
	msg.buf = data;

	/* high byte goes out first */
	wreg = (u16 *)data;
	*wreg = cpu_to_be16(reg);

	if (data_length == MISENSOR_8BIT) {
		data[2] = (u8)(val);
	} else if (data_length == MISENSOR_16BIT) {
		u16 *wdata = (u16 *)&data[2];
		*wdata = be16_to_cpu((u16)val);
	} else {
		/* MISENSOR_32BIT */
		u32 *wdata = (u32 *)&data[2];
		*wdata = be32_to_cpu(val);
	}

	num_msg = i2c_transfer(client->adapter, &msg, 1);

	/*
	 * HACK: Need some delay here for Rev 2 sensors otherwise some
	 * registers do not seem to load correctly.
	 */
	mdelay(1);

	if (num_msg >= 0)
		return 0;

	dev_err(&client->dev, "write error: wrote 0x%x to offset 0x%x error %d",
		val, reg, num_msg);
	if (retry <= I2C_RETRY_COUNT) {
		dev_err(&client->dev, "retrying... %d", retry);
		retry++;
		msleep(20);
		goto again;
	}

	return num_msg;
}

/**
 * misensor_rmw_reg - Read/Modify/Write a value to a register in the sensor
 * device
 * @client: i2c driver client structure
 * @data_length: 8/16/32-bits length
 * @reg: register address
 * @mask: masked out bits
 * @set: bits set
 *
 * Read/modify/write a value to a register in the  sensor device.
 * Returns zero if successful, or non-zero otherwise.
 */
int misensor_rmw_reg(struct i2c_client *client, u16 data_length, u16 reg,
		     u32 mask, u32 set)
{
	int err;
	u32 val;

	/* Exit when no mask */
	if (mask == 0)
		return 0;

	/* @mask must not exceed data length */
	switch (data_length) {
	case MISENSOR_8BIT:
		if (mask & ~0xff)
			return -EINVAL;
		break;
	case MISENSOR_16BIT:
		if (mask & ~0xffff)
			return -EINVAL;
		break;
	case MISENSOR_32BIT:
		break;
	default:
		/* Wrong @data_length */
		return -EINVAL;
	}

	err = mt9m114_read_reg(client, data_length, reg, &val);
	if (err) {
		v4l2_err(client, "misensor_rmw_reg error exit, read failed\n");
		return -EINVAL;
	}

	val &= ~mask;

	/*
	 * Perform the OR function if the @set exists.
	 * Shift @set value to target bit location. @set should set only
	 * bits included in @mask.
	 *
	 * REVISIT: This function expects @set to be non-shifted. Its shift
	 * value is then defined to be equal to mask's LSB position.
	 * How about to inform values in their right offset position and avoid
	 * this unneeded shift operation?
	 */
	set <<= ffs(mask) - 1;
	val |= set & mask;

	err = mt9m114_write_reg(client, data_length, reg, val);
	if (err) {
		v4l2_err(client, "misensor_rmw_reg error exit, write failed\n");
		return -EINVAL;
	}

	return 0;
}


/*
 * mt9m114_write_reg_array - Initializes a list of MT9M114 registers
 * @client: i2c driver client structure
 * @reglist: list of registers to be written
 *
 * This function initializes a list of registers. When consecutive addresses
 * are found in a row on the list, this function creates a buffer and sends
 * consecutive data in a single i2c_transfer().
 *
 * __mt9m114_flush_reg_array, __mt9m114_buf_reg_array() and
 * __mt9m114_write_reg_is_consecutive() are internal functions to
 * mt9m114_write_reg_array() and should be not used anywhere else.
 *
 */

static int __mt9m114_flush_reg_array(struct i2c_client *client,
				     struct mt9m114_write_ctrl *ctrl)
{
	struct i2c_msg msg;
	const int num_msg = 1;
	int ret;
	int retry = 0;

	if (ctrl->index == 0)
		return 0;

again:
	msg.addr = client->addr;
	msg.flags = 0;
	msg.len = 2 + ctrl->index;
	ctrl->buffer.addr = cpu_to_be16(ctrl->buffer.addr);
	msg.buf = (u8 *)&ctrl->buffer;

	ret = i2c_transfer(client->adapter, &msg, num_msg);
	if (ret != num_msg) {
		dev_err(&client->dev, "%s: i2c transfer error\n", __func__);
		if (++retry <= I2C_RETRY_COUNT) {
			dev_err(&client->dev, "retrying... %d\n", retry);
			msleep(20);
			goto again;
		}
		return -EIO;
	}

	ctrl->index = 0;

	/*
	 * REVISIT: Previously we had a delay after writing data to sensor.
	 * But it was removed as our tests have shown it is not necessary
	 * anymore.
	 */

	return 0;
}

static int __mt9m114_buf_reg_array(struct i2c_client *client,
				   struct mt9m114_write_ctrl *ctrl,
				   const struct misensor_reg *next)
{
	u16 *data16;
	u32 *data32;

	/* Insufficient buffer? Let's flush and get more free space. */
	if (ctrl->index + next->length >= MT9M114_MAX_WRITE_BUF_SIZE)
		__mt9m114_flush_reg_array(client, ctrl);

	switch (next->length) {
	case MISENSOR_8BIT:
		ctrl->buffer.data[ctrl->index] = (u8)next->val;
		break;
	case MISENSOR_16BIT:
		data16 = (u16 *)&ctrl->buffer.data[ctrl->index];
		*data16 = cpu_to_be16((u16)next->val);
		break;
	case MISENSOR_32BIT:
		data32 = (u32 *)&ctrl->buffer.data[ctrl->index];
		*data32 = cpu_to_be32(next->val);
		break;
	default:
		return -EINVAL;
	}

	/* When first item is added, we need to store its starting address */
	if (ctrl->index == 0)
		ctrl->buffer.addr = next->reg;

	ctrl->index += next->length;

	return 0;
}

static int
__mt9m114_write_reg_is_consecutive(struct i2c_client *client,
				   struct mt9m114_write_ctrl *ctrl,
				   const struct misensor_reg *next)
{
	if (ctrl->index == 0)
		return 1;

	return ctrl->buffer.addr + ctrl->index == next->reg;
}

static int mt9m114_write_reg_array(struct i2c_client *client,
				   const struct misensor_reg *reglist)
{
	const struct misensor_reg *next = reglist;
	struct mt9m114_write_ctrl ctrl;
	int err;

	ctrl.index = 0;
	for (; next->length != MISENSOR_TOK_TERM; next++) {
		switch (next->length & MISENSOR_TOK_MASK) {
		case MISENSOR_TOK_DELAY:
			err = __mt9m114_flush_reg_array(client, &ctrl);
			if (err)
				return err;
			msleep(next->val);
			break;
		case MISENSOR_TOK_RMW:
			err = __mt9m114_flush_reg_array(client, &ctrl);
			err |= misensor_rmw_reg(client,
						next->length &
							~MISENSOR_TOK_RMW,
						next->reg, next->val,
						next->val2);
			if (err) {
				dev_err(&client->dev, "%s read err. aborted\n",
					__func__);
				return -EINVAL;
			}
			break;
		default:
			/*
			 * If next address is not consecutive, data needs to be
			 * flushed before proceed.
			 */
			if (!__mt9m114_write_reg_is_consecutive(client, &ctrl,
								next)) {
				err = __mt9m114_flush_reg_array(client, &ctrl);
				if (err)
					return err;
			}
			err = __mt9m114_buf_reg_array(client, &ctrl, next);
			if (err) {
				v4l2_err(client, "%s: write error, aborted\n",
					 __func__);
				return err;
			}
			break;
		}
	}

	return __mt9m114_flush_reg_array(client, &ctrl);
}


static int mt9m114_wait_3a(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	//struct mt9m114_device *dev = to_mt9m114(sd);
	int timeout = 100;
	int status;

	while (timeout--) {
		mt9m114_read_reg(client, MISENSOR_16BIT, 0xA800, &status);
		if (status & 0x8) {
			v4l2_info(client, "3a stablize time:%dms.\n",
				  (100-timeout)*20);
			return 0;
		}
		msleep(20);
	}

	return -EINVAL;
}

static int mt9m114_wait_state(struct v4l2_subdev *sd, int timeout)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	//struct mt9m114_device *dev = to_mt9m114(sd);
	int val, ret;

	while (timeout-- > 0) {
		ret = mt9m114_read_reg(client, MISENSOR_16BIT, 0x0080, &val);
		if (ret)
			return ret;
		if ((val & 0x2) == 0)
			return 0;
		msleep(20);
	}

	return -EINVAL;

}

static int mt9m114_set_suspend(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;

	ret = mt9m114_write_reg_array(client, mt9m114_suspend);
	if (ret)
		return ret;

	ret = mt9m114_wait_state(sd, MT9M114_WAIT_STAT_TIMEOUT);

	return ret;
}

static int mt9m114_set_streaming(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return mt9m114_write_reg_array(client, mt9m114_streaming);
}
#if 0
static void read_regs(struct v4l2_subdev *sd, const struct misensor_reg *reglist)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret, val;
	const struct misensor_reg *next = reglist;

	for (; next->length != MISENSOR_TOK_TERM; next++) {
		ret = mt9m114_read_reg(client, next->length, next->reg, &val);
		if (ret)
			printk("%s error = %d\n", __func__, ret);
		printk("0x%4x, 0x%4x\n", next->reg, val);
	}	

}
#endif
static int mt9m114_init_common(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;
	const struct misensor_reg *next = mt9m114_iq;

	ret = mt9m114_write_reg_array(client, next);
	if (ret)
		return -EINVAL;
#if 0
	for (; next->length != MISENSOR_TOK_TERM; next++) {
		mt9m114_read_reg(client, next->length, next->reg, &val);
		printk("0x%4x, 0x%4x\n", next->reg, val);
	}	
#endif
	return ret;
}


#if 0
static int mt9m114_standby(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct mt9m114_device *dev = to_mt9m114(sd);
	int timeout, delay, val;
	int ret;

	ret = mt9m114_write_reg_array(client, mt9m114_standby_reg);
	if (ret)
		return ret;

	/* Wait for the FW to complete the command */
	timeout = 100;
	delay = 10;
	while (timeout > 0) {
		ret = mt9m114_read_reg(client, MISENSOR_16BIT, 0x0080, &val);
		if (ret)
			return ret;
		if ((val & 0x2) == 0)
			break;
		msleep(delay);
		timeout--;
	}
	if (timeout == 0)
		return ret;

	/* Wait for the FW to fully enter standby */
	timeout = 10;
	delay = 50;
	ret = mt9m114_write_reg(client, MISENSOR_16BIT, 0x098E, 0xDC01);
	if (ret)
		return ret;
	while (timeout > 0) {
		ret = mt9m114_read_reg(client, MISENSOR_8BIT, 0x0990, &val);
		if (ret)
			return ret;
		if (val == 0x52)
			break;
		msleep(delay);
		timeout--;
	}
	if (timeout == 0)
		return ret;

	/* turn EXTCLK off */
	ret = dev->platform_data->flisclk_ctrl(sd, 0);
	if (ret)
		return ret;

	/* FIXME: need to wait for 100 EXTCLK cycles */
	msleep(20);

	return 0;
}

static int mt9m114_wakeup(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct mt9m114_device *dev = to_mt9m114(sd);
	int timeout, delay, val;
	int ret;

	/* turn EXTCLK on */
	ret = dev->platform_data->flisclk_ctrl(sd, 1);
	if (ret)
		return ret;

	/* FIXME: need to wait for 100 EXTCLK cycles */
	msleep(20);

	ret = mt9m114_write_reg_array(client, mt9m114_wakeup_reg);
	if (ret)
		return ret;
	/* Wait for the FW to complete the command */
	timeout = 100;
	delay = 10;
	while (timeout > 0) {
		ret = mt9m114_read_reg(client, MISENSOR_16BIT, 0x0080, &val);
		if (ret)
			return ret;
		if ((val & 0x2) == 0)
			break;
		msleep(delay);
		timeout--;
	}
	if (timeout == 0)
		return ret;

	/* Wait for the FW to fully out of standby */
	timeout = 10;
	delay = 50;
	ret = mt9m114_write_reg(client, MISENSOR_16BIT, 0x098E, 0xDC01);
	if (ret)
		return ret;
	while (timeout > 0) {
		ret = mt9m114_read_reg(client, MISENSOR_8BIT, 0x0990, &val);
		if (ret)
			return ret;
		if (val == 0x31)
			break;
		msleep(delay);
		timeout--;
	}
	if (timeout == 0)
		return ret;

	return 0;
}
#endif

static int power_up( struct v4l2_subdev *sd)
{
#if 0
	struct mt9m114_device *dev = to_mt9m114(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;
	if (NULL == dev->platform_data) {
		dev_err(&client->dev, "no camera_sensor_platform_data");
		return -ENODEV;
	}

	/* power control */
	ret = dev->platform_data->power_ctrl(sd, 1);
	if (ret)
		goto fail_power;

	/* flis clock control */
	ret = dev->platform_data->flisclk_ctrl(sd, 1);
	if (ret)
		goto fail_clk;

	/* gpio ctrl */
	ret = dev->platform_data->gpio_ctrl(sd, 1);
	if (ret)
		dev_err(&client->dev, "gpio failed 1\n");
	/*
	 * according to DS, 44ms is needed between power up and first i2c
	 * commend
	 */
	msleep(50);
#endif

	printk("%s\n", __func__);
	//return regulator_enable(cam3_regulator);
	return 0;

	/*
fail_clk:
	dev->platform_data->flisclk_ctrl(sd, 0);
fail_power:
	dev->platform_data->power_ctrl(sd, 0);
	dev_err(&client->dev, "sensor power-up failed\n");

	return ret;
	*/
}

static int power_down(struct v4l2_subdev *sd)
{
	printk("%s\n", __func__);
#if 0
	struct mt9m114_device *dev = to_mt9m114(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;

	if (NULL == dev->platform_data) {
		dev_err(&client->dev, "no camera_sensor_platform_data");
		return -ENODEV;
	}

	
	ret = dev->platform_data->flisclk_ctrl(sd, 0);
	if (ret)
		dev_err(&client->dev, "flisclk failed\n");

	/* gpio ctrl */
	ret = dev->platform_data->gpio_ctrl(sd, 0);
	if (ret)
		dev_err(&client->dev, "gpio failed 1\n");

	/* power control */
	ret = dev->platform_data->power_ctrl(sd, 0);
	if (ret)
		dev_err(&client->dev, "vprog failed.\n");

	/*according to DS, 20ms is needed after power down*/
	msleep(20);
#endif 
	//return regulator_disable(cam3_regulator);
	return 0;
}
int mt9m114_power(int power)
{
	printk("%s \n", __func__);
	return 0;
}

static int mt9m114_s_power(struct v4l2_subdev *sd, int power)
{
	//struct mt9m114_device *dev = to_mt9m114(sd);
	//struct i2c_client *client = v4l2_get_subdevdata(sd);
	return 0;

	printk("%s \n", __func__);
	if (power == 0)
		return power_down(sd);
	else {
		if (power_up(sd))
			return -EINVAL;

		//return mt9m114_init_common(sd);
	}
	return 0;
}

static int mt9m114_try_res(u32 *w, u32 *h)
{
	int i;

	/*
	 * The mode list is in ascending order. We're done as soon as
	 * we have found the first equal or bigger size.
	 */
	for (i = 0; i < N_RES; i++) {
		if ((mt9m114_res[i].width >= *w) &&
		    (mt9m114_res[i].height >= *h))
			break;
	}

	/*
	 * If no mode was found, it means we can provide only a smaller size.
	 * Returning the biggest one available in this case.
	 */
	if (i == N_RES)
		i--;

	*w = mt9m114_res[i].width;
	*h = mt9m114_res[i].height;

	return 0;
}

static struct mt9m114_res_struct *mt9m114_to_res(u32 w, u32 h)
{
	int  index;

	for (index = 0; index < N_RES; index++) {
		if ((mt9m114_res[index].width == w) &&
		    (mt9m114_res[index].height == h))
			break;
	}

	/* No mode found */
	if (index >= N_RES)
		return NULL;

	return &mt9m114_res[index];
}

static int mt9m114_try_mbus_fmt(struct v4l2_subdev *sd,
				struct v4l2_mbus_framefmt *fmt)
{
	//return mt9m114_try_res(&fmt->width, &fmt->height);
	return 0;
}

static int mt9m114_res2size(unsigned int res, int *h_size, int *v_size)
{
	unsigned short hsize;
	unsigned short vsize;

	switch (res) {
	case MT9M114_RES_QVGA:
		hsize = MT9M114_RES_QVGA_SIZE_H;
		vsize = MT9M114_RES_QVGA_SIZE_V;
		break;
	case MT9M114_RES_VGA:
		hsize = MT9M114_RES_VGA_SIZE_H;
		vsize = MT9M114_RES_VGA_SIZE_V;
		break;
	case MT9M114_RES_720P:
		hsize = MT9M114_RES_720P_SIZE_H;
		vsize = MT9M114_RES_720P_SIZE_V;
		break;
	case MT9M114_RES_960P:
		hsize = MT9M114_RES_960P_SIZE_H;
		vsize = MT9M114_RES_960P_SIZE_V;
		break;
	default:
		WARN(1, "%s: Resolution 0x%08x unknown\n", __func__, res);
		return -EINVAL;
	}

	if (h_size != NULL)
		*h_size = hsize;
	if (v_size != NULL)
		*v_size = vsize;

	return 0;
}

static int mt9m114_get_mbus_fmt(struct v4l2_subdev *sd,
				struct v4l2_mbus_framefmt *fmt)
{
	struct mt9m114_device *dev = to_mt9m114(sd);
	int width, height;
	int ret;

	ret = mt9m114_res2size(dev->res, &width, &height);
	if (ret)
		return ret;
	fmt->width = width;
	fmt->height = height;

	return 0;
}

static int mt9m114_set_mbus_fmt(struct v4l2_subdev *sd,
			      struct v4l2_mbus_framefmt *fmt)
{
	struct i2c_client *c = v4l2_get_subdevdata(sd);
	struct mt9m114_device *dev = to_mt9m114(sd);
	struct mt9m114_res_struct *res_index;
	u32 width = fmt->width;
	u32 height = fmt->height;
	int ret;

	printk("width = %d, height = %d\n", width, height);
	mt9m114_try_res(&width, &height);
	res_index = mt9m114_to_res(width, height);

	return 0;
	/* Sanity check */
	if (unlikely(!res_index)) {
		WARN_ON(1);
		return -EINVAL;
	}

	switch (res_index->res) {
	case MT9M114_RES_QVGA:
		ret = mt9m114_write_reg_array(c, mt9m114_qvga_init);
		/* set sensor read_mode to Skipping */
		ret += misensor_rmw_reg(c, MISENSOR_16BIT, MISENSOR_READ_MODE,
				MISENSOR_R_MODE_MASK, MISENSOR_SKIPPING_SET);
		break;
	case MT9M114_RES_VGA:
		ret = mt9m114_write_reg_array(c, mt9m114_vga_init);
		/* set sensor read_mode to Summing */
		ret += misensor_rmw_reg(c, MISENSOR_16BIT, MISENSOR_READ_MODE,
				MISENSOR_R_MODE_MASK, MISENSOR_SUMMING_SET);
		break;
	case MT9M114_RES_720P:
		ret = mt9m114_write_reg_array(c, mt9m114_720p_init);
		/* set sensor read_mode to Normal */
		ret += misensor_rmw_reg(c, MISENSOR_16BIT, MISENSOR_READ_MODE,
				MISENSOR_R_MODE_MASK, MISENSOR_NORMAL_SET);
		break;
	case MT9M114_RES_960P:
		ret = mt9m114_write_reg_array(c, mt9m114_960P_init);
		/* set sensor read_mode to Normal */
		ret += misensor_rmw_reg(c, MISENSOR_16BIT, MISENSOR_READ_MODE,
				MISENSOR_R_MODE_MASK, MISENSOR_NORMAL_SET);
		break;
	default:
		v4l2_err(sd, "set resolution: %d failed!\n", res_index->res);
		return -EINVAL;
	}

	if (ret)
		return -EINVAL;

#if 0
	if (mt9m114_write_reg_array(c, mt9m114_common))
		return -EINVAL;
	if (mt9m114_wait_state(sd, MT9M114_WAIT_STAT_TIMEOUT))
		return -EINVAL;
#endif

	if (mt9m114_set_suspend(sd))
		return -EINVAL;

	if (dev->res != res_index->res) {
		int index;

		/* Switch to different size */
		if (width <= 640) {
			dev->nctx = 0x00; /* Set for context A */
		} else {
			/*
			 * Context B is used for resolutions larger than 640x480
			 * Using YUV for Context B.
			 */
			dev->nctx = 0x01; /* set for context B */
		}

		/*
		 * Marked current sensor res as being "used"
		 *
		 * REVISIT: We don't need to use an "used" field on each mode
		 * list entry to know which mode is selected. If this
		 * information is really necessary, how about to use a single
		 * variable on sensor dev struct?
		 */
		for (index = 0; index < N_RES; index++) {
			if ((width == mt9m114_res[index].width) &&
			    (height == mt9m114_res[index].height)) {
				mt9m114_res[index].used = 1;
				continue;
			}
			mt9m114_res[index].used = 0;
		}
	}

	/*
	 * mt9m114 - we don't poll for context switch
	 * because it does not happen with streaming disabled.
	 */
	dev->res = res_index->res;

	fmt->width = width;
	fmt->height = height;

	return 0;
}

/* TODO: Update to SOC functions, remove exposure and gain */
#if 0
static int mt9m114_g_focal(struct v4l2_subdev *sd, s32 * val)
{
	*val = (MT9M114_FOCAL_LENGTH_NUM << 16) | MT9M114_FOCAL_LENGTH_DEM;
	return 0;
}

static int mt9m114_g_fnumber(struct v4l2_subdev *sd, s32 * val)
{
	/*const f number for mt9m114*/
	*val = (MT9M114_F_NUMBER_DEFAULT_NUM << 16) | MT9M114_F_NUMBER_DEM;
	return 0;
}

static int mt9m114_g_fnumber_range(struct v4l2_subdev *sd, s32 * val)
{
	*val = (MT9M114_F_NUMBER_DEFAULT_NUM << 24) |
		(MT9M114_F_NUMBER_DEM << 16) |
		(MT9M114_F_NUMBER_DEFAULT_NUM << 8) | MT9M114_F_NUMBER_DEM;
	return 0;
}

static int mt9m114_s_freq(struct v4l2_subdev *sd, s32  val)
{
	struct i2c_client *c = v4l2_get_subdevdata(sd);
	struct mt9m114_device *dev = to_mt9m114(sd);
	int ret;

	if (val != MT9M114_FLICKER_MODE_50HZ &&
			val != MT9M114_FLICKER_MODE_60HZ)
		return -EINVAL;

	if (val == MT9M114_FLICKER_MODE_50HZ) {
		ret = mt9m114_write_reg_array(c, mt9m114_antiflicker_50hz);
		if (ret < 0)
			return ret;
	} else {
		ret = mt9m114_write_reg_array(c, mt9m114_antiflicker_60hz);
		if (ret < 0)
			return ret;
	}

	ret = mt9m114_wait_state(sd, MT9M114_WAIT_STAT_TIMEOUT);
	if (ret == 0)
		dev->lightfreq = val;

	return ret;
}
#endif

static struct mt9m114_control mt9m114_controls[] = {
	{
		.qc = {
			.id = V4L2_CID_VFLIP,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "Image v-Flip",
			.minimum = 0,
			.maximum = 1,
			.step = 1,
			.default_value = 0,
		},
		.tweak = mt9m114_t_vflip,
	},
	{
		.qc = {
			.id = V4L2_CID_HFLIP,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "Image h-Flip",
			.minimum = 0,
			.maximum = 1,
			.step = 1,
			.default_value = 0,
		},
		.tweak = mt9m114_t_hflip,
	},
};
#define N_CONTROLS (ARRAY_SIZE(mt9m114_controls))

static struct mt9m114_control *mt9m114_find_control(__u32 id)
{
	int i;

	for (i = 0; i < N_CONTROLS; i++) {
		if (mt9m114_controls[i].qc.id == id)
			return &mt9m114_controls[i];
	}
	return NULL;
}

static int mt9m114_detect(struct mt9m114_device *dev, struct i2c_client *client)
{
	struct i2c_adapter *adapter = client->adapter;
	u32 retvalue;

	if (!i2c_check_functionality(adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "%s: i2c error", __func__);
		return -ENODEV;
	}
	mt9m114_read_reg(client, MISENSOR_16BIT, (u32)MT9M114_PID, &retvalue);
	dev->real_model_id = retvalue;
	dev_info(&client->dev, "%s: module_ID = 0x%x\n", __func__, retvalue);

	if (retvalue != MT9M114_MOD_ID) {
		dev_err(&client->dev, "%s: failed: client->addr = %x\n",
			__func__, client->addr);
		return -ENODEV;
	}

	return 0;
}
static int mt9m114_reset(struct v4l2_subdev *sd, u32 val)
{
	printk(KERN_ERR "######## %s ######## (%d)\n",__func__,val);
	s3c_gpio_cfgpin(EXYNOS4_GPF3(2), S3C_GPIO_OUTPUT);
	s3c_gpio_cfgpin(EXYNOS4_GPF3(3), S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(EXYNOS4_GPF3(2), S3C_GPIO_PULL_NONE);

	gpio_set_value(EXYNOS4_GPF3(3), 0);
	gpio_set_value(EXYNOS4_GPF3(2), 1);
	udelay(100);
	gpio_set_value(EXYNOS4_GPF3(2), 0);
	udelay(100);
	gpio_set_value(EXYNOS4_GPF3(2), 1);
	gpio_set_value(EXYNOS4_GPF3(3), 1);
	mdelay(50);
	gpio_set_value(EXYNOS4_GPF3(3), 0);

	return 0;
}

static int
mt9m114_init(struct v4l2_subdev *sd, u32 val)
{
	struct mt9m114_device *dev = to_mt9m114(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;
	int ini_q;
	int i;
	printk(KERN_ERR "%s \n",__func__);
	mt9m114_reset(sd, val);

	msleep(10);
	ret = mt9m114_s_power(sd, 1);
	if (ret) {
		v4l2_err(client, "mt9m114 power-up err");
		return ret;
	}
///////////////////for test
	ini_q = sizeof(mt9m114_iq)/sizeof(mt9m114_iq[0]);
    printk("++++++++++ini_q = %d \n",ini_q);
	for (i=0; i<ini_q; i++) {
	  switch(mt9m114_iq[i].length){
	  	case MISENSOR_TOK_DELAY:
			mdelay(mt9m114_iq[i].val);
			break;
		case  MISENSOR_TOK_TERM:
			break;
		default :	
			ret = mt9m114_write_reg_test(client,mt9m114_iq[i].length,mt9m114_iq[i].reg,mt9m114_iq[i].val);
			break;

	  }
	}
		
	return ret;
/////////////////

	
	ret = mt9m114_init_common(sd);
	if (ret)
		printk("mt9m114_init_common err, errno = %d\n", ret);

	/* config & detect sensor */
	ret = mt9m114_detect(dev, client);
	if (ret) {
		v4l2_err(client, "mt9m114_detect err s_config.\n");
		goto fail_detect;
	}

	/*
	ret = dev->platform_data->csi_cfg(sd, 1);
	if (ret)
		goto fail_csi_cfg;

	mt9m114_write_reg_array(client, mt9m114_suspend);
	*/

	ret = mt9m114_s_power(sd, 0);
	if (ret) {
		v4l2_err(client, "mt9m114 power down err");
		return ret;
	}

	return 0;

//fail_csi_cfg:
//	dev->platform_data->csi_cfg(sd, 0);
fail_detect:
	mt9m114_s_power(sd, 0);
	dev_err(&client->dev, "sensor power-gating failed\n");
	return ret;
}

static int mt9m114_queryctrl(struct v4l2_subdev *sd, struct v4l2_queryctrl *qc)
{
	struct mt9m114_control *ctrl = mt9m114_find_control(qc->id);

	if (ctrl == NULL)
		return -EINVAL;
	*qc = ctrl->qc;
	return 0;
}

/* HFLIP - read context A value and set to true if set */
#if 0
static int mt9m114_q_hflip(struct v4l2_subdev *sd, __s32 * value)
{
	struct i2c_client *c = v4l2_get_subdevdata(sd);
	int err;
	u32 val;

	/* set for direct mode */
	mt9m114_write_reg(c, MISENSOR_16BIT, 0x098E, 0x4850);
	err = mt9m114_read_reg(c, MISENSOR_8BIT, 0xC850, &val);

	*value = val & 0x1;

	return err;
}
#endif
/* Horizontal flip the image. */
static int mt9m114_t_hflip(struct v4l2_subdev *sd, int value)
{
	struct i2c_client *c = v4l2_get_subdevdata(sd);
	struct mt9m114_device *dev = to_mt9m114(sd);
	int err;

	/* set for direct mode */
	err = mt9m114_write_reg(c, MISENSOR_16BIT, 0x098E, 0xC850);
	if (value) {
		/* enable H flip ctx A */
		err += misensor_rmw_reg(c, MISENSOR_8BIT, 0xC850, 0x01, 0x01);
		err += misensor_rmw_reg(c, MISENSOR_8BIT, 0xC851, 0x01, 0x01);
		/* ctx B */
		err += misensor_rmw_reg(c, MISENSOR_8BIT, 0xC888, 0x01, 0x01);
		err += misensor_rmw_reg(c, MISENSOR_8BIT, 0xC889, 0x01, 0x01);

		/* enable vert_flip and horz_mirror */
		err += misensor_rmw_reg(c, MISENSOR_16BIT, MISENSOR_READ_MODE,
					MISENSOR_F_M_MASK, MISENSOR_F_M_EN);

		dev->bpat = MT9M114_BPAT_GRGRBGBG;
	} else {
		/* disable H flip ctx A */
		err += misensor_rmw_reg(c, MISENSOR_8BIT, 0xC850, 0x01, 0x00);
		err += misensor_rmw_reg(c, MISENSOR_8BIT, 0xC851, 0x01, 0x00);
		/* ctx B */
		err += misensor_rmw_reg(c, MISENSOR_8BIT, 0xC888, 0x01, 0x00);
		err += misensor_rmw_reg(c, MISENSOR_8BIT, 0xC889, 0x01, 0x00);

		/* enable vert_flip and disable horz_mirror */
		err += misensor_rmw_reg(c, MISENSOR_16BIT, MISENSOR_READ_MODE,
					MISENSOR_F_M_MASK, MISENSOR_F_EN);

		dev->bpat = MT9M114_BPAT_BGBGGRGR;
	}

	err += mt9m114_write_reg(c, MISENSOR_8BIT, 0x8404, 0x06);
	udelay(10);

	return !!err;
}

/* Determine VFLIP status by reading context A register */
#if 0
static int mt9m114_q_vflip(struct v4l2_subdev *sd, __s32 * value)
{
	struct i2c_client *c = v4l2_get_subdevdata(sd);
	int err;
	u32 val;

	/* set for direct mode */
	mt9m114_write_reg(c, MISENSOR_16BIT, 0x098E, 0x4850);
	err = mt9m114_read_reg(c, MISENSOR_8BIT, 0xC850, &val);

	*value = (val & 0x02) == 0x02;

	return err;
}
#endif

/* Vertically flip the image */
static int mt9m114_t_vflip(struct v4l2_subdev *sd, int value)
{
	struct i2c_client *c = v4l2_get_subdevdata(sd);
	//struct mt9m114_device *dev = to_mt9m114(sd);
	int err;

	/* set for direct mode */
	err = mt9m114_write_reg(c, MISENSOR_16BIT, 0x098E, 0xC850);
	if (value >= 1) {
		/* enable H flip - ctx A */
		err += misensor_rmw_reg(c, MISENSOR_8BIT, 0xC850, 0x02, 0x01);
		err += misensor_rmw_reg(c, MISENSOR_8BIT, 0xC851, 0x02, 0x01);
		/* ctx B */
		err += misensor_rmw_reg(c, MISENSOR_8BIT, 0xC888, 0x02, 0x01);
		err += misensor_rmw_reg(c, MISENSOR_8BIT, 0xC889, 0x02, 0x01);

		/* disable vert_flip and horz_mirror */
		err += misensor_rmw_reg(c, MISENSOR_16BIT, MISENSOR_READ_MODE,
					MISENSOR_F_M_MASK, MISENSOR_F_M_DIS);
	} else {
		/* disable H flip - ctx A */
		err += misensor_rmw_reg(c, MISENSOR_8BIT, 0xC850, 0x02, 0x00);
		err += misensor_rmw_reg(c, MISENSOR_8BIT, 0xC851, 0x02, 0x00);
		/* ctx B */
		err += misensor_rmw_reg(c, MISENSOR_8BIT, 0xC888, 0x02, 0x00);
		err += misensor_rmw_reg(c, MISENSOR_8BIT, 0xC889, 0x02, 0x00);

		/* enable vert_flip and disable horz_mirror */
		err += misensor_rmw_reg(c, MISENSOR_16BIT, MISENSOR_READ_MODE,
					MISENSOR_F_M_MASK, MISENSOR_F_EN);
	}

	err += mt9m114_write_reg(c, MISENSOR_8BIT, 0x8404, 0x06);
	udelay(10);

	return !!err;
}

static int mt9m114_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct mt9m114_control *octrl = mt9m114_find_control(ctrl->id);
	int ret;

	if (octrl == NULL)
		return -EINVAL;

	ret = octrl->query(sd, &ctrl->value);
	if (ret < 0)
		return ret;

	return 0;
}

static int mt9m114_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct mt9m114_control *octrl = mt9m114_find_control(ctrl->id);
	int ret;

	if (octrl == NULL)
		return -EINVAL;

	ret = octrl->tweak(sd, ctrl->value);
	if (ret < 0)
		return ret;

	return 0;
}

/*
 * Wait till the context has changed. Read the context status register,
 * exit when the target value is reached.
 */
void mt9m114_poll_awhile(struct v4l2_subdev *sd, int targetval)
{

	struct i2c_client *c = v4l2_get_subdevdata(sd);
	int i, val;

	/* POLL to see if the context changes... */
	for (i = 0; i <= 20; i++) {
		mt9m114_read_reg(c, MISENSOR_8BIT, 0x8405, &val);
		if (val == targetval)
			return;
		/* REVISIT: Do we need to wait that much? */
		mdelay(70);
	}
}

static int mt9m114_s_stream(struct v4l2_subdev *sd, int enable)
{
	int ret;
	struct i2c_client *c = v4l2_get_subdevdata(sd);

	return 0;
	printk("%s %s\n", __func__, enable ? "enable" : "disable");
	if (enable) {
		ret = mt9m114_write_reg_array(c, mt9m114_common);
		if (ret < 0)
			return ret;
		ret = mt9m114_wait_state(sd, MT9M114_WAIT_STAT_TIMEOUT);
		if (ret < 0)
			return ret;

		ret = mt9m114_set_streaming(sd);
		/*
		 * here we wait for sensor's 3A algorithm to be
		 * stablized, as to fix still capture bad 3A output picture
		 */
		if (mt9m114_wait_3a(sd))
			v4l2_warn(c, "3A can not finish!");
	}
	else
		ret = mt9m114_set_suspend(sd);

	return ret;
}

static int
mt9m114_enum_framesizes(struct v4l2_subdev *sd, struct v4l2_frmsizeenum *fsize)
{
	unsigned int index = fsize->index;

	printk("%s index = %d\n", __func__, index);

	index = 1;

	fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	fsize->discrete.width = mt9m114_res[index].width;
	fsize->discrete.height = mt9m114_res[index].height;

	/* FIXME: Wrong way to know used mode */
	fsize->reserved[0] = mt9m114_res[index].used;

	return 0;
}

static int mt9m114_enum_frameintervals(struct v4l2_subdev *sd,
				       struct v4l2_frmivalenum *fival)
{
	unsigned int index = fival->index;
	int i;

	return 0;
	if (index >= N_RES)
		return -EINVAL;

	/* find out the first equal or bigger size */
	for (i = 0; i < N_RES; i++) {
		if ((mt9m114_res[i].width >= fival->width) &&
		    (mt9m114_res[i].height >= fival->height))
			break;
	}
	if (i == N_RES)
		i--;

	index = i;

	fival->type = V4L2_FRMIVAL_TYPE_DISCRETE;
	fival->discrete.numerator = 1;
	fival->discrete.denominator = mt9m114_res[index].fps;

	return 0;
}

static int
mt9m114_g_chip_ident(struct v4l2_subdev *sd, struct v4l2_dbg_chip_ident *chip)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return v4l2_chip_ident_i2c_client(client, chip, V4L2_IDENT_MT9M114, 0);
}

#if 0
static int mt9m114_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_fh *fh,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index >= MAX_FMTS)
		return -EINVAL;
	code->code = V4L2_MBUS_FMT_SGRBG10_1X10;

	return 0;
}

static int mt9m114_enum_frame_size(struct v4l2_subdev *sd,
	struct v4l2_subdev_fh *fh,
	struct v4l2_subdev_frame_size_enum *fse)
{

	unsigned int index = fse->index;


	if (index >= N_RES)
		return -EINVAL;

	fse->min_width = mt9m114_res[index].width;
	fse->min_height = mt9m114_res[index].height;
	fse->max_width = mt9m114_res[index].width;
	fse->max_height = mt9m114_res[index].height;

	return 0;
}
#endif

#if 0
static struct v4l2_mbus_framefmt *
__mt9m114_get_pad_format(struct mt9m114_device *sensor,
			 struct v4l2_subdev_fh *fh, unsigned int pad,
			 enum v4l2_subdev_format_whence which)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->sd);

	if (pad != 0) {
		dev_err(&client->dev,  "%s err. pad %x\n", __func__, pad);
		return NULL;
	}

	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_format(fh, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &sensor->format;
	default:
		return NULL;
	}
}

static int
mt9m114_get_pad_format(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh,
		       struct v4l2_subdev_format *fmt)
{
	struct mt9m114_device *snr = to_mt9m114(sd);
	struct v4l2_mbus_framefmt *format =
			__mt9m114_get_pad_format(snr, fh, fmt->pad, fmt->which);

	if (format == NULL)
		return -EINVAL;
	fmt->format = *format;

	return 0;
}

static int
mt9m114_set_pad_format(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh,
		       struct v4l2_subdev_format *fmt)
{
	struct mt9m114_device *snr = to_mt9m114(sd);
	struct v4l2_mbus_framefmt *format =
			__mt9m114_get_pad_format(snr, fh, fmt->pad, fmt->which);

	if (format == NULL)
		return -EINVAL;

	if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE)
		snr->format = fmt->format;

	return 0;
}
#endif


static const struct v4l2_subdev_video_ops mt9m114_video_ops = {
	.try_mbus_fmt = mt9m114_try_mbus_fmt,
	.s_mbus_fmt = mt9m114_set_mbus_fmt,
	.g_mbus_fmt = mt9m114_get_mbus_fmt,
	.s_stream = mt9m114_s_stream,
	.enum_framesizes = mt9m114_enum_framesizes,
	.enum_frameintervals = mt9m114_enum_frameintervals,
};

static const struct v4l2_subdev_core_ops mt9m114_core_ops = {
	.init = mt9m114_init,
	.reset = mt9m114_reset,
	.g_chip_ident = mt9m114_g_chip_ident,
	.queryctrl = mt9m114_queryctrl,
	.g_ctrl = mt9m114_g_ctrl,
	.s_ctrl = mt9m114_s_ctrl,
	.s_power = mt9m114_s_power,
};

/* REVISIT: Do we need pad operations? */
/*
static const struct v4l2_subdev_pad_ops mt9m114_pad_ops = {
	.enum_mbus_code = mt9m114_enum_mbus_code,
	.enum_frame_size = mt9m114_enum_frame_size,
	.get_fmt = mt9m114_get_pad_format,
	.set_fmt = mt9m114_set_pad_format,
};
*/

static const struct v4l2_subdev_ops mt9m114_ops = {
	.core = &mt9m114_core_ops,
	.video = &mt9m114_video_ops,
};


static int mt9m114_remove(struct i2c_client *client)
{
	struct mt9m114_device *dev;
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	dev = container_of(sd, struct mt9m114_device, sd);
	//dev->platform_data->csi_cfg(sd, 0);

	v4l2_device_unregister_subdev(sd);
	kfree(dev);
	regulator_disable(cam1_regulator);
	regulator_disable(cam2_regulator);
	regulator_disable(cam3_regulator);


	return 0;
}

static void mt9m114_cfg_gpio(void)
{
	s3c_gpio_cfgpin(EXYNOS4212_GPJ0(0), S3C_GPIO_SFN(2));
	s3c_gpio_cfgpin(EXYNOS4212_GPJ0(1), S3C_GPIO_SFN(2));
	s3c_gpio_cfgpin(EXYNOS4212_GPJ0(2), S3C_GPIO_SFN(2));
	s3c_gpio_cfgpin(EXYNOS4212_GPJ0(3), S3C_GPIO_SFN(2));
	s3c_gpio_cfgpin(EXYNOS4212_GPJ0(4), S3C_GPIO_SFN(2));
	s3c_gpio_cfgpin(EXYNOS4212_GPJ0(5), S3C_GPIO_SFN(2));
	s3c_gpio_cfgpin(EXYNOS4212_GPJ0(6), S3C_GPIO_SFN(2));
	s3c_gpio_cfgpin(EXYNOS4212_GPJ0(7), S3C_GPIO_SFN(2));
	s3c_gpio_cfgpin(EXYNOS4212_GPJ1(0), S3C_GPIO_SFN(2));
	s3c_gpio_cfgpin(EXYNOS4212_GPJ1(1), S3C_GPIO_SFN(2));
	s3c_gpio_cfgpin(EXYNOS4212_GPJ1(2), S3C_GPIO_SFN(2));
	s3c_gpio_cfgpin(EXYNOS4212_GPJ1(3), S3C_GPIO_SFN(2));

	s3c_gpio_cfgpin(EXYNOS4_GPF3(3), S3C_GPIO_OUTPUT);
	gpio_set_value(EXYNOS4_GPF3(3), 0);
}


static int mt9m114_probe(struct i2c_client *client,
		       const struct i2c_device_id *id)
{
	struct mt9m114_device *dev;
	struct v4l2_subdev *sd;
	int ret;

	cam1_regulator = regulator_get(NULL, "vdd_ldo26");
	cam2_regulator = regulator_get(NULL, "vdd_ldo17");
	cam3_regulator = regulator_get(NULL, "vdd_ldo5");
	regulator_enable(cam1_regulator);
	regulator_enable(cam2_regulator);
	regulator_enable(cam3_regulator);
	msleep(500);
	mt9m114_cfg_gpio();
	printk("################ %s #################\n", __func__);

	printk("%s i2c addr = 0x%x, name = %s \n", __func__, client->addr, client->name);

	/* Setup sensor configuration structure */
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		dev_err(&client->dev, "out of memory\n");
		ret = -ENOMEM;
		goto err;
	}

	sd = &dev->sd;
	v4l2_i2c_subdev_init(&dev->sd, client, &mt9m114_ops);

	/*TODO add format code here*/
	dev->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	//dev->pad.flags = MEDIA_PAD_FLAG_OUTPUT;

	/* set res index to be invalid */
	dev->res = -1;
	printk("################ %s finished #################\n", __func__);

	return 0;
err:
	kfree(dev);
	regulator_disable(cam1_regulator);
	regulator_disable(cam2_regulator);

	return ret;

}

static struct i2c_driver mt9m114_i2c_driver = {
	.driver = {
		.name = "MT9M114", //mt9m114
	},   
	.probe    = mt9m114_probe,
	.remove   = mt9m114_remove,
	.id_table = mt9m114_id,
	//.suspend                = mt9m114_suspend,
	//.resume                 = mt9m114_resume,
};

static int __devinit mt9m114_module_init(void)
{
	printk("%s\n", __func__);
	return i2c_add_driver(&mt9m114_i2c_driver);
}

static void __exit mt9m114_module_exit(void)
{
	i2c_del_driver(&mt9m114_i2c_driver);
}

module_init(mt9m114_module_init);
module_exit(mt9m114_module_exit);


MODULE_AUTHOR("Shuguang Gong <Shuguang.gong@intel.com>");
MODULE_LICENSE("GPL");
