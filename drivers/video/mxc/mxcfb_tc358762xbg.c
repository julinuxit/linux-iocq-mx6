/*
 * Copyright (C) 2011-2013 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/console.h>
#include <linux/io.h>
#include <linux/bitops.h>
#include <linux/spinlock.h>
#include <linux/mipi_dsi.h>
#include <linux/mxcfb.h>
#include <linux/backlight.h>
#include <video/mipi_display.h>

#include "mipi_dsi.h"

#define MIPI_DSI_MAX_RET_PACK_SIZE				(0x4)

#define CHECK_RETCODE(ret)					\
do {								\
	if (ret < 0) {						\
		dev_err(&mipi_dsi->pdev->dev,			\
			"%s ERR: ret:%d, line:%d.\n",		\
			__func__, ret, __LINE__);		\
		return ret;					\
	}							\
} while (0)

static int mipid_init_backlight(struct mipi_dsi_info *mipi_dsi);

static struct fb_videomode rpi7_lcd_modedb[] = {
	{
	 "RPI7-WVGA", 64, 480, 800, 37880,
	 8, 8,
	 6, 6,
	 8, 6,
	 FB_SYNC_OE_LOW_ACT,
	 FB_VMODE_NONINTERLACED,
	 0,
	},
};

static struct mipi_lcd_config lcd_config = {
	.virtual_ch		= 0x0,
	.data_lane_num  = 2,
	.max_phy_clk    = 800,
	.dpi_fmt		= MIPI_RGB888,
};
void mipid_tc358762_get_lcd_videomode(struct fb_videomode **mode, int *size,
		struct mipi_lcd_config **data)
{
	*mode = &rpi7_lcd_modedb[0];
	*size = ARRAY_SIZE(rpi7_lcd_modedb);
	*data = &lcd_config;
}

int mipid_tc358762_lcd_setup(struct mipi_dsi_info *mipi_dsi)
{
	u32 buf[DSI_CMD_BUF_MAXSIZE];
	int err;

	dev_dbg(&mipi_dsi->pdev->dev, "MIPI DSI LCD setup.\n");

	return 0;
}

static int mipi_bl_check_fb(struct backlight_device *bl, struct fb_info *fbi)
{
	return 0;
}

