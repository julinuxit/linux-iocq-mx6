/*
 * Copyright (C) 2011-2015 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/of_device.h>
#include <linux/i2c.h>
#include <linux/of_gpio.h>
#include <linux/pinctrl/consumer.h>
#include <linux/proc_fs.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/fsl_devices.h>
//#include <linux/v4l2-mediabus.h>
#include <linux/mipi_csi2.h>	// new code
#include <media/v4l2-device.h>
//#include <media/v4l2-ctrls.h>
#include <linux/mipi_csi2.h>
#include <media/v4l2-chip-ident.h>
#include "v4l2-int-device.h"
#include "mxc_v4l2_capture.h"

#define OV5647_VOLTAGE_ANALOG               2800000
#define OV5647_VOLTAGE_DIGITAL_CORE         1500000
#define OV5647_VOLTAGE_DIGITAL_IO           1800000

#define MIN_FPS 15
#define MAX_FPS 30
#define DEFAULT_FPS 30

#define OV5647_XCLK_MIN 6000000
#define OV5647_XCLK_MAX 24000000

#define OV5647_CHIP_ID_HIGH_BYTE	0x300A
#define OV5647_CHIP_ID_LOW_BYTE		0x300B

#define MEDIA_BUS_FMT_SBGGR8_1X8        0x3001

enum ov5647_mode {
	ov5647_mode_MIN = 0,
	ov5647_mode_VGA_640_480 = 0,
	ov5647_mode_720P_1280_720 = 1,
	ov5647_mode_1080P_1920_1080 = 2,
	ov5647_mode_QSXGA_2592_1944 = 3,
	ov5647_mode_MAX = 3,
	ov5647_mode_INIT = 0xff, /*only for sensor init*/
};

enum ov5647_frame_rate {
	ov5647_15_fps,
	ov5647_30_fps
};

static int ov5647_framerates[] = {
	[ov5647_15_fps] = 15,
	[ov5647_30_fps] = 30,
};

#if 0
struct ov5647_datafmt {
	u32	code;
	enum v4l2_colorspace		colorspace;
};
#endif

/* image size under 1280 * 960 are SUBSAMPLING
 * image size upper 1280 * 960 are SCALING
 */
enum ov5647_downsize_mode {
	SUBSAMPLING,
	SCALING,
};

struct reg_value {
	u16 u16RegAddr;
	u8 u8Val;
	u8 u8Mask;
	u32 u32Delay_ms;
};

struct ov5647_mode_info {
	enum ov5647_mode mode;
	enum ov5647_downsize_mode dn_mode;
	u32 width;
	u32 height;
	struct reg_value *init_data_ptr;
	u32 init_data_size;
};

struct otp_struct {
	int customer_id;
	int module_integrator_id;
	int lens_id;
	int rg_ratio;
	int bg_ratio;
	int user_data[3];
	int light_rg;
	int light_bg;
};

#if 0
struct ov5647 {
	struct v4l2_subdev		subdev;
	struct i2c_client *i2c_client;
	struct v4l2_pix_format pix;
	const struct ov5647_datafmt	*fmt;
	struct v4l2_captureparm streamcap;
	bool on;

	/* control settings */
	int brightness;
	int hue;
	int contrast;
	int saturation;
	int red;
	int green;
	int blue;
	int ae_mode;

	u32 mclk;
	u8 mclk_source;
	struct clk *sensor_clk;
	int ipu_id;		// new code
	int csi;
	unsigned virtual_channel;   /* Used with mipi */	// new code

	void (*io_init)(void);
};
#endif

/*!
 * Maintains the information on the current state of the sesor.
 */
//static struct ov5647 ov5647_data;
static struct sensor_data ov5647_data;
static int pwn_gpio, rst_gpio;
static int prev_sysclk, prev_HTS;
static int AE_low, AE_high, AE_Target = 52;

/* R/G and B/G of typical camera module is defined here,
 * the typical camera module is selected by CameraAnalyzer. */
static int RG_Ratio_Typical = 0x70;
static int BG_Ratio_Typical = 0x70;

// 1280 x 960
static struct reg_value ov5647_init_setting[] = {

	{0x0100, 0x00, 0, 0},                       {0x3035, 0x11, 0, 0},	// debug mode
	{0x3036, 0x69, 0, 0}, {0x303c, 0x11, 0, 0}, {0x3821, 0x07, 0, 0},	// SC_CMMN_PLL_MULTIPLIER / SC_CMMN_PLLS_CTRL3 / TIMING_TC_REG21
	{0x3820, 0x41, 0, 0}, {0x370c, 0x0f, 0, 0}, {0x3612, 0x59, 0, 0},	// TIMING_TC_REG20 / ?? / ??
	{0x3618, 0x00, 0, 0}, {0x5000, 0x06, 0, 0}, {0x5002, 0x40, 0, 0},	// ?? / ISP CTRL00 / ISP_CTRL02
	{0x5003, 0x08, 0, 0}, {0x5a00, 0x08, 0, 0}, {0x3000, 0xff, 0, 0},	// ISP CTRL03 / DIGC CTRL0 / SC_CMMN_PAD_OEN0
	{0x3001, 0xff, 0, 0}, {0x3002, 0xff, 0, 0}, {0x301d, 0xf0, 0, 0},	// SC_CMMN_PAD_OEN1 / SC_CMMN_PAD_OEN2 / ??
	{0x3a18, 0x00, 0, 0}, {0x3a19, 0xf8, 0, 0}, {0x3c01, 0x80, 0, 0},	// AEC_GAIN_CEILING / AEC_GAIN_CEILING / 50/60HZ DETECTION CTRL01
	{0x3b07, 0x0c, 0, 0}, {0x380c, 0x07, 0, 0}, {0x380d, 0x68, 0, 0},	// STROBE_FREX_MODE_SEL / TIMING_HTS / TIMING_HTS
	{0x380e, 0x03, 0, 0}, {0x380f, 0xd8, 0, 0}, {0x3814, 0x31, 0, 0},	// TIMING_HTS / TIMING_HTS / TIMING_X_INC
	{0x3815, 0x31, 0, 0}, {0x3708, 0x64, 0, 0}, {0x3709, 0x52, 0, 0},	// TIMING_Y_INC / ?? / ??
	{0x3808, 0x05, 0, 0}, {0x3809, 0x00, 0, 0}, {0x380a, 0x03, 0, 0},	// TIMING_X_OUTPUT_SIZE / TIMING_X_OUTPUT_SIZE / TIMING_Y_OUTPUT_SIZE
	{0x380b, 0xc0, 0, 0}, {0x3800, 0x00, 0, 0}, {0x3801, 0x18, 0, 0},	// TIMING_Y_OUTPUT_SIZE
	{0x3802, 0x00, 0, 0}, {0x3803, 0x0e, 0, 0}, {0x3804, 0x0a, 0, 0},
	{0x3805, 0x27, 0, 0}, {0x3806, 0x07, 0, 0}, {0x3807, 0x95, 0, 0},
	{0x3630, 0x2e, 0, 0}, {0x3632, 0xe2, 0, 0}, {0x3633, 0x23, 0, 0},
	{0x3634, 0x44, 0, 0}, {0x3620, 0x64, 0, 0}, {0x3621, 0xe0, 0, 0},
	{0x3600, 0x37, 0, 0}, {0x3704, 0xa0, 0, 0}, {0x3703, 0x5a, 0, 0},
	{0x3715, 0x78, 0, 0}, {0x3717, 0x01, 0, 0}, {0x3731, 0x02, 0, 0},
	{0x370b, 0x60, 0, 0}, {0x3705, 0x1a, 0, 0}, {0x3f05, 0x02, 0, 0},
	{0x3f06, 0x10, 0, 0}, {0x3f01, 0x0a, 0, 0}, {0x3a08, 0x01, 0, 0},
	{0x3a09, 0x27, 0, 0}, {0x3a0a, 0x00, 0, 0}, {0x3a0b, 0xf6, 0, 0},
	{0x3a0d, 0x04, 0, 0}, {0x3a0e, 0x03, 0, 0}, {0x3a0f, 0x58, 0, 0},
	{0x3a10, 0x50, 0, 0}, {0x3a1b, 0x58, 0, 0}, {0x3a1e, 0x50, 0, 0},
	{0x3a11, 0x60, 0, 0}, {0x3a1f, 0x28, 0, 0}, {0x4001, 0x02, 0, 0},
	{0x4004, 0x02, 0, 0}, {0x4000, 0x09, 0, 0}, {0x4050, 0x6e, 0, 0},
	{0x4051, 0x8f, 0, 0}, {0x4837, 0x17, 0, 0}, {0x3503, 0x03, 0, 0},
	{0x3501, 0x44, 0, 0}, {0x3502, 0x80, 0, 0}, {0x350a, 0x00, 0, 0},
	{0x350b, 0x7f, 0, 0}, {0x5001, 0x01, 0, 0}, {0x5002, 0x41, 0, 0},
	{0x5180, 0x08, 0, 0}, {0x5186, 0x04, 0, 0}, {0x5187, 0x00, 0, 0},
	{0x5188, 0x04, 0, 0}, {0x5189, 0x00, 0, 0}, {0x518a, 0x04, 0, 0},
	{0x518b, 0x00, 0, 0}, {0x5000, 0x86, 0, 0}, {0x5800, 0x11, 0, 0},
	{0x5801, 0x0a, 0, 0}, {0x5802, 0x09, 0, 0}, {0x5803, 0x09, 0, 0},
	{0x5804, 0x0a, 0, 0}, {0x5805, 0x0f, 0, 0}, {0x5806, 0x07, 0, 0},
	{0x5807, 0x05, 0, 0}, {0x5808, 0x03, 0, 0}, {0x5809, 0x03, 0, 0},
	{0x580a, 0x05, 0, 0}, {0x580b, 0x07, 0, 0}, {0x580c, 0x05, 0, 0},
	{0x580d, 0x02, 0, 0}, {0x580e, 0x00, 0, 0}, {0x580f, 0x00, 0, 0},
	{0x5810, 0x02, 0, 0}, {0x5811, 0x05, 0, 0}, {0x5812, 0x05, 0, 0},
	{0x5813, 0x02, 0, 0}, {0x5814, 0x00, 0, 0}, {0x5815, 0x00, 0, 0},
	{0x5816, 0x01, 0, 0}, {0x5817, 0x05, 0, 0}, {0x5818, 0x08, 0, 0},
	{0x5819, 0x05, 0, 0}, {0x581a, 0x03, 0, 0}, {0x581b, 0x03, 0, 0},
	{0x581c, 0x04, 0, 0}, {0x581d, 0x07, 0, 0}, {0x581e, 0x10, 0, 0},
	{0x581f, 0x0b, 0, 0}, {0x5820, 0x09, 0, 0}, {0x5821, 0x09, 0, 0},
	{0x5822, 0x09, 0, 0}, {0x5823, 0x0e, 0, 0}, {0x5824, 0x28, 0, 0},
	{0x5825, 0x1a, 0, 0}, {0x5826, 0x1a, 0, 0}, {0x5827, 0x1a, 0, 0},
	{0x5828, 0x46, 0, 0}, {0x5829, 0x2a, 0, 0}, {0x582a, 0x26, 0, 0},
	{0x582b, 0x44, 0, 0}, {0x582c, 0x26, 0, 0}, {0x582d, 0x2a, 0, 0},
	{0x582e, 0x28, 0, 0}, {0x582f, 0x42, 0, 0}, {0x5830, 0x40, 0, 0},
	{0x5831, 0x42, 0, 0}, {0x5832, 0x28, 0, 0}, {0x5833, 0x0a, 0, 0},
	{0x5834, 0x16, 0, 0}, {0x5835, 0x44, 0, 0}, {0x5836, 0x26, 0, 0},
	{0x5837, 0x2a, 0, 0}, {0x5838, 0x28, 0, 0}, {0x5839, 0x0a, 0, 0},
	{0x583a, 0x0a, 0, 0}, {0x583b, 0x0a, 0, 0}, {0x583c, 0x26, 0, 0},
	{0x583d, 0xbe, 0, 0}, {0x0100, 0x01, 0, 0}, {0x3000, 0x00, 0, 0},
	{0x3001, 0x00, 0, 0}, {0x3002, 0x00, 0, 0}, {0x3017, 0xe0, 0, 0},
	{0x301c, 0xfc, 0, 0}, {0x3636, 0x06, 0, 0}, {0x3016, 0x08, 0, 0},
	{0x3827, 0xec, 0, 0}, {0x3018, 0x44, 0, 0}, {0x3035, 0x21, 0, 0},
	{0x3106, 0xf5, 0, 0}, {0x3034, 0x18, 0, 0}, {0x301c, 0xf8, 0, 0},
};

static struct reg_value ov5647_setting_60fps_VGA_640_480[] = {
	{0x0100, 0x00, 0, 0},                        {0x3035, 0x11, 0, 0},
	{0x3036, 0x46, 0, 0}, {0x303c, 0x11, 0, 0}, {0x3821, 0x07, 0, 0},
	{0x3820, 0x41, 0, 0}, {0x370c, 0x0f, 0, 0}, {0x3612, 0x59, 0, 0},
	{0x3618, 0x00, 0, 0}, {0x5000, 0x06, 0, 0}, {0x5002, 0x40, 0, 0},
	{0x5003, 0x08, 0, 0}, {0x5a00, 0x08, 0, 0}, {0x3000, 0xff, 0, 0},
	{0x3001, 0xff, 0, 0}, {0x3002, 0xff, 0, 0}, {0x301d, 0xf0, 0, 0},
	{0x3a18, 0x00, 0, 0}, {0x3a19, 0xf8, 0, 0}, {0x3c01, 0x80, 0, 0},
	{0x3b07, 0x0c, 0, 0}, {0x380c, 0x07, 0, 0}, {0x380d, 0x3c, 0, 0},
	{0x380e, 0x01, 0, 0}, {0x380f, 0xf8, 0, 0}, {0x3814, 0x71, 0, 0},
	{0x3815, 0x71, 0, 0}, {0x3708, 0x64, 0, 0}, {0x3709, 0x52, 0, 0},
	{0x3808, 0x02, 0, 0}, {0x3809, 0x80, 0, 0}, {0x380a, 0x01, 0, 0},
	{0x380b, 0xe0, 0, 0}, {0x3800, 0x00, 0, 0}, {0x3801, 0x10, 0, 0},
	{0x3802, 0x00, 0, 0}, {0x3803, 0x00, 0, 0}, {0x3804, 0x0a, 0, 0},
	{0x3805, 0x2f, 0, 0}, {0x3806, 0x07, 0, 0}, {0x3807, 0x9f, 0, 0},
	{0x3630, 0x2e, 0, 0}, {0x3632, 0xe2, 0, 0}, {0x3633, 0x23, 0, 0},
	{0x3634, 0x44, 0, 0}, {0x3620, 0x64, 0, 0}, {0x3621, 0xe0, 0, 0},
	{0x3600, 0x37, 0, 0}, {0x3704, 0xa0, 0, 0}, {0x3703, 0x5a, 0, 0},
	{0x3715, 0x78, 0, 0}, {0x3717, 0x01, 0, 0}, {0x3731, 0x02, 0, 0},
	{0x370b, 0x60, 0, 0}, {0x3705, 0x1a, 0, 0}, {0x3f05, 0x02, 0, 0},
	{0x3f06, 0x10, 0, 0}, {0x3f01, 0x0a, 0, 0}, {0x3a08, 0x01, 0, 0},
	{0x3a09, 0x2e, 0, 0}, {0x3a0a, 0x00, 0, 0}, {0x3a0b, 0xfb, 0, 0},
	{0x3a0d, 0x02, 0, 0}, {0x3a0e, 0x01, 0, 0}, {0x3a0f, 0x58, 0, 0},
	{0x3a10, 0x50, 0, 0}, {0x3a1b, 0x58, 0, 0}, {0x3a1e, 0x50, 0, 0},
	{0x3a11, 0x60, 0, 0}, {0x3a1f, 0x28, 0, 0}, {0x4001, 0x02, 0, 0},
	{0x4004, 0x02, 0, 0}, {0x4000, 0x09, 0, 0}, {0x4050, 0x6e, 0, 0},
	{0x4051, 0x8f, 0, 0}, {0x0100, 0x01, 0, 0}, {0x3000, 0x00, 0, 0},
	{0x3001, 0x00, 0, 0}, {0x3002, 0x00, 0, 0}, {0x3017, 0xe0, 0, 0},
	{0x301c, 0xfc, 0, 0}, {0x3636, 0x06, 0, 0}, {0x3016, 0x08, 0, 0},
	{0x3827, 0xec, 0, 0}, {0x3018, 0x44, 0, 0}, {0x3035, 0x21, 0, 0},
	{0x3106, 0xf5, 0, 0}, {0x3034, 0x18, 0, 0}, {0x301c, 0xf8, 0, 0},
};

static struct reg_value ov5647_setting_30fps_720P_1280_720[] = {
	{0x0100, 0x00, 0, 0}, {0x3820, 0x41, 0, 0}, {0x3821, 0x07, 0, 0},
	{0x3612, 0x59, 0, 0}, {0x3618, 0x00, 0, 0}, {0x380c, 0x09, 0, 0},
	{0x380d, 0xe8, 0, 0}, {0x380e, 0x04, 0, 0}, {0x380f, 0x50, 0, 0},
	{0x3814, 0x31, 0, 0}, {0x3815, 0x31, 0, 0}, {0x3709, 0x52, 0, 0},
	{0x3808, 0x05, 0, 0}, {0x3809, 0x00, 0, 0}, {0x380a, 0x02, 0, 0},
	{0x380b, 0xd0, 0, 0}, {0x3801, 0x18, 0, 0}, {0x3802, 0x00, 0, 0},
	{0x3803, 0xf8, 0, 0}, {0x3804, 0x0a, 0, 0}, {0x3805, 0x27, 0, 0},
	{0x3806, 0x06, 0, 0}, {0x3807, 0xa7, 0, 0}, {0x3a09, 0xbe, 0, 0},
	{0x3a0a, 0x01, 0, 0}, {0x3a0b, 0x74, 0, 0}, {0x3a0d, 0x02, 0, 0},
	{0x3a0e, 0x01, 0, 0}, {0x4004, 0x02, 0, 0}, {0x4005, 0x18, 0, 0},
	{0x0100, 0x01, 0, 0},
};

static struct reg_value ov5647_setting_30fps_1080P_1920_1080[] = {
	{0x0100, 0x00, 0, 0}, {0x3820, 0x00, 0, 0}, {0x3821, 0x06, 0, 0},
	{0x3612, 0x5b, 0, 0}, {0x3618, 0x04, 0, 0}, {0x380c, 0x09, 0, 0},
	{0x380d, 0xe8, 0, 0}, {0x380e, 0x04, 0, 0}, {0x380f, 0x50, 0, 0},
	{0x3814, 0x11, 0, 0}, {0x3815, 0x11, 0, 0}, {0x3709, 0x12, 0, 0},
	{0x3808, 0x07, 0, 0}, {0x3809, 0x80, 0, 0}, {0x380a, 0x04, 0, 0},
	{0x380b, 0x38, 0, 0}, {0x3801, 0x5c, 0, 0}, {0x3802, 0x01, 0, 0},
	{0x3803, 0xb2, 0, 0}, {0x3804, 0x08, 0, 0}, {0x3805, 0xe3, 0, 0},
	{0x3806, 0x05, 0, 0}, {0x3807, 0xf1, 0, 0}, {0x3a09, 0x4b, 0, 0},
	{0x3a0a, 0x01, 0, 0}, {0x3a0b, 0x13, 0, 0}, {0x3a0d, 0x04, 0, 0},
	{0x3a0e, 0x03, 0, 0}, {0x4004, 0x04, 0, 0}, {0x4005, 0x18, 0, 0},
	{0x0100, 0x01, 0, 0},
};

static struct reg_value ov5647_setting_15fps_QSXGA_2592_1944[] = {
	{0x0100, 0x00, 0, 0}, {0x3820, 0x00, 0, 0}, {0x3821, 0x06, 0, 0},
	{0x3612, 0x5b, 0, 0}, {0x3618, 0x04, 0, 0}, {0x380c, 0x0b, 0, 0},
	{0x380d, 0x10, 0, 0}, {0x380e, 0x07, 0, 0}, {0x380f, 0xb8, 0, 0},
	{0x3814, 0x11, 0, 0}, {0x3815, 0x11, 0, 0}, {0x3709, 0x12, 0, 0},
	{0x3808, 0x0a, 0, 0}, {0x3809, 0x20, 0, 0}, {0x380a, 0x07, 0, 0},
	{0x380b, 0x98, 0, 0}, {0x3801, 0x0c, 0, 0}, {0x3802, 0x00, 0, 0},
	{0x3803, 0x04, 0, 0}, {0x3804, 0x0a, 0, 0}, {0x3805, 0x33, 0, 0},
	{0x3806, 0x07, 0, 0}, {0x3807, 0xa3, 0, 0}, {0x3a09, 0x28, 0, 0},
	{0x3a0a, 0x00, 0, 0}, {0x3a0b, 0xf6, 0, 0}, {0x3a0d, 0x08, 0, 0},
	{0x3a0e, 0x06, 0, 0}, {0x4004, 0x04, 0, 0}, {0x4005, 0x1a, 0, 0},
	{0x0100, 0x01, 0, 0},
};

static struct ov5647_mode_info ov5647_mode_info_data[2][ov5647_mode_MAX + 1] = {
	{
		{ov5647_mode_VGA_640_480, -1, 0, 0, NULL, 0},
		{ov5647_mode_720P_1280_720, -1, 0, 0, NULL, 0},
		{ov5647_mode_1080P_1920_1080, -1, 0, 0, NULL, 0},
		{ov5647_mode_QSXGA_2592_1944, SCALING, 2592, 1944,
		ov5647_setting_15fps_QSXGA_2592_1944,
		ARRAY_SIZE(ov5647_setting_15fps_QSXGA_2592_1944)},
	},
	{
		/* Actually VGA working in 60fps mode */
		{ov5647_mode_VGA_640_480, SUBSAMPLING, 640,  480,
		ov5647_setting_60fps_VGA_640_480,
		ARRAY_SIZE(ov5647_setting_60fps_VGA_640_480)},
		{ov5647_mode_720P_1280_720, SUBSAMPLING, 1280, 720,
		ov5647_setting_30fps_720P_1280_720,
		ARRAY_SIZE(ov5647_setting_30fps_720P_1280_720)},
		{ov5647_mode_1080P_1920_1080, SCALING, 1920, 1080,
		ov5647_setting_30fps_1080P_1920_1080,
		ARRAY_SIZE(ov5647_setting_30fps_1080P_1920_1080)},
		{ov5647_mode_QSXGA_2592_1944, -1, 0, 0, NULL, 0},
	},
};

static struct regulator *io_regulator;
static struct regulator *core_regulator;
static struct regulator *analog_regulator;
static struct regulator *gpo_regulator;

static int ov5647_probe(struct i2c_client *adapter,
				const struct i2c_device_id *device_id);
static int ov5647_remove(struct i2c_client *client);

static s32 ov5647_read_reg(u16 reg, u8 *val);
static s32 ov5647_write_reg(u16 reg, u8 val);

static const struct i2c_device_id ov5647_id[] = {
	{"ov5647_mipi", 0},
	{},
};

MODULE_DEVICE_TABLE(i2c, ov5647_id);

static struct i2c_driver ov5647_i2c_driver = {
	.driver = {
		  .owner = THIS_MODULE,
		  .name  = "ov5647_mipi",
		  },
	.probe  = ov5647_probe,
	.remove = ov5647_remove,
	.id_table = ov5647_id,
};

#if 0
static const struct ov5647_datafmt ov5647_colour_fmts[] = {
	{MEDIA_BUS_FMT_SBGGR8_1X8, V4L2_COLORSPACE_JPEG},
};

static struct ov5647 *to_ov5647(const struct i2c_client *client)
{
	pr_info("[%s:%s:%d] -------------------------\n", __FILE__, __func__, __LINE__);
	return container_of(i2c_get_clientdata(client), struct ov5647, subdev);
}

/* Find a data format by a pixel code in an array */
static const struct ov5647_datafmt
			*ov5647_find_datafmt(u32 code)
{
	int i;

	pr_info("[%s:%s:%d] -------------------------\n", __FILE__, __func__, __LINE__);
	for (i = 0; i < ARRAY_SIZE(ov5647_colour_fmts); i++)
		if (ov5647_colour_fmts[i].code == code)
			return ov5647_colour_fmts + i;

	return NULL;
}
#endif

static inline void ov5647_power_down(int enable)
{
	pr_info("[%s:%s:%d] -------------------------\n", __FILE__, __func__, __LINE__);
	//if (pwn_gpio < 0)
		//return;

	/* 19x19 pwdn pin invert by mipi daughter card */
	if (!enable)
		gpio_set_value_cansleep(pwn_gpio, 1);
	else
		gpio_set_value_cansleep(pwn_gpio, 0);

	msleep(2);
}

static void ov5647_reset(void)
{
	pr_info("[%s:%s:%d] -------------------------\n", __FILE__, __func__, __LINE__);
	//if (rst_gpio < 0 || pwn_gpio < 0)
//		return;

	/* camera reset */
	gpio_set_value_cansleep(rst_gpio, 1);

	/* camera power dowmn */
	//gpio_set_value_cansleep(pwn_gpio, 1);
	gpio_set_value_cansleep(pwn_gpio, 0);
	msleep(5);

	//gpio_set_value_cansleep(pwn_gpio, 0);
	gpio_set_value_cansleep(pwn_gpio, 1);
	msleep(5);

	gpio_set_value_cansleep(rst_gpio, 0);
	msleep(1);

	gpio_set_value_cansleep(rst_gpio, 1);
	msleep(5);

	//gpio_set_value_cansleep(pwn_gpio, 1);
	gpio_set_value_cansleep(pwn_gpio, 0);
}

static int ov5647_regulator_enable(struct device *dev)
{
	int ret = 0;

	pr_info("[%s:%s:%d] -------------------------\n", __FILE__, __func__, __LINE__);
	io_regulator = devm_regulator_get(dev, "DOVDD");
	if (!IS_ERR(io_regulator)) {
		regulator_set_voltage(io_regulator,
				      OV5647_VOLTAGE_DIGITAL_IO,
				      OV5647_VOLTAGE_DIGITAL_IO);
		ret = regulator_enable(io_regulator);
		if (ret) {
			pr_err("%s:io set voltage error\n", __func__);
			return ret;
		} else {
			dev_dbg(dev,
				"%s:io set voltage ok\n", __func__);
		}
	} else {
		pr_err("%s: cannot get io voltage error\n", __func__);
		io_regulator = NULL;
	}

	core_regulator = devm_regulator_get(dev, "DVDD");
	if (!IS_ERR(core_regulator)) {
		regulator_set_voltage(core_regulator,
				      OV5647_VOLTAGE_DIGITAL_CORE,
				      OV5647_VOLTAGE_DIGITAL_CORE);
		ret = regulator_enable(core_regulator);
		if (ret) {
			pr_err("%s:core set voltage error\n", __func__);
			return ret;
		} else {
			dev_dbg(dev,
				"%s:core set voltage ok\n", __func__);
		}
	} else {
		core_regulator = NULL;
		pr_err("%s: cannot get core voltage error\n", __func__);
	}

	analog_regulator = devm_regulator_get(dev, "AVDD");
	if (!IS_ERR(analog_regulator)) {
		regulator_set_voltage(analog_regulator,
				      OV5647_VOLTAGE_ANALOG,
				      OV5647_VOLTAGE_ANALOG);
		ret = regulator_enable(analog_regulator);
		if (ret) {
			pr_err("%s:analog set voltage error\n",
				__func__);
			return ret;
		} else {
			dev_dbg(dev,
				"%s:analog set voltage ok\n", __func__);
		}
	} else {
		analog_regulator = NULL;
		pr_err("%s: cannot get analog voltage error\n", __func__);
	}

	return ret;
}

static s32 ov5647_write_reg(u16 reg, u8 val)
{
	u8 au8Buf[3] = {0};

	au8Buf[0] = reg >> 8;
	au8Buf[1] = reg & 0xff;
	au8Buf[2] = val;

	if (i2c_master_send(ov5647_data.i2c_client, au8Buf, 3) < 0) {
		pr_err("%s:write reg error:reg=%x,val=%x\n",
			__func__, reg, val);
		return -1;
	}

	return 0;
}

static s32 ov5647_read_reg(u16 reg, u8 *val)
{
	u8 au8RegBuf[2] = {0};
	u8 u8RdVal = 0;

	au8RegBuf[0] = reg >> 8;
	au8RegBuf[1] = reg & 0xff;

	if (2 != i2c_master_send(ov5647_data.i2c_client, au8RegBuf, 2)) {
		pr_err("%s:write reg error:reg=%x\n",
				__func__, reg);
		return -1;
	}

	if (1 != i2c_master_recv(ov5647_data.i2c_client, &u8RdVal, 1)) {
		pr_err("%s:read reg error:reg=%x,val=%x\n",
				__func__, reg, u8RdVal);
		return -1;
	}

	*val = u8RdVal;

	return u8RdVal;
}

/* index: index of otp group. (0, 1, 2)
 * return:
 * 0, group index is empty
 * 1, group index has invalid data
 * 2, group index has valid data */
static int ov5647_check_otp(int index)
{
	int i;
	int address;
	u8 temp;

	pr_info("[%s:%s:%d] -------------------------\n", __FILE__, __func__, __LINE__);
	/* read otp into buffer */
	ov5647_write_reg(0x3d21, 0x01);
	msleep(20);
	address = 0x3d05 + index * 9;
	temp = ov5647_read_reg(address, &temp);

	/* disable otp read */
	ov5647_write_reg(0x3d21, 0x00);

	/* clear otp buffer */
	for (i = 0; i < 32; i++)
		ov5647_write_reg(0x3d00 + i, 0x00);

	if (!temp)
		return 0;
	else if ((!(temp & 0x80)) && (temp & 0x7f))
		return 2;
	else
		return 1;
}

/* index: index of otp group. (0, 1, 2)
 * return: 0 */
static int ov5647_read_otp(int index, struct otp_struct *otp_ptr)
{
	int i;
	int address;
	u8 temp;

	pr_info("[%s:%s:%d] -------------------------\n", __FILE__, __func__, __LINE__);
	/* read otp into buffer */
	ov5647_write_reg(0x3d21, 0x01);
	msleep(2);
	address = 0x3d05 + index * 9;
	temp = ov5647_read_reg(address, &temp);
	(*otp_ptr).customer_id = temp & 0x7f;

	(*otp_ptr).module_integrator_id = ov5647_read_reg(address, &temp);
	(*otp_ptr).lens_id = ov5647_read_reg(address + 1, &temp);
	(*otp_ptr).rg_ratio = ov5647_read_reg(address + 2, &temp);
	(*otp_ptr).bg_ratio = ov5647_read_reg(address + 3, &temp);
	(*otp_ptr).user_data[0] = ov5647_read_reg(address + 4, &temp);
	(*otp_ptr).user_data[1] = ov5647_read_reg(address + 5, &temp);
	(*otp_ptr).user_data[2] = ov5647_read_reg(address + 6, &temp);
	(*otp_ptr).light_rg = ov5647_read_reg(address + 7, &temp);
	(*otp_ptr).light_bg = ov5647_read_reg(address + 8, &temp);

	/* disable otp read */
	ov5647_write_reg(0x3d21, 0x00);

	/* clear otp buffer */
	for (i = 0; i < 32; i++)
		ov5647_write_reg(0x3d00 + i, 0x00);

	return 0;
}

/* R_gain, sensor red gain of AWB, 0x400 =1
 * G_gain, sensor green gain of AWB, 0x400 =1
 * B_gain, sensor blue gain of AWB, 0x400 =1
 * return 0 */
static int ov5647_update_awb_gain(int R_gain, int G_gain, int B_gain)
{
	pr_info("[%s:%s:%d] -------------------------\n", __FILE__, __func__, __LINE__);
	if (R_gain > 0x400) {
		ov5647_write_reg(0x5186, R_gain >> 8);
		ov5647_write_reg(0x5187, R_gain & 0x00ff);
	}
	if (G_gain > 0x400) {
		ov5647_write_reg(0x5188, G_gain >> 8);
		ov5647_write_reg(0x5189, G_gain & 0x00ff);
	}
	if (B_gain > 0x400) {
		ov5647_write_reg(0x518a, B_gain >> 8);
		ov5647_write_reg(0x518b, B_gain & 0x00ff);
	}

	return 0;
}

/* call this function after OV5647 initialization
 * return value:
 * 0 update success
 * 1 no OTP */
static int ov5647_update_otp(void)
{
	struct otp_struct current_otp;
	int i;
	int otp_index;
	int temp;
	int R_gain, G_gain, B_gain, G_gain_R, G_gain_B;
	int rg, bg;

	pr_info("[%s:%s:%d] -------------------------\n", __FILE__, __func__, __LINE__);
	/* R/G and B/G of current camera module is read out from sensor OTP
	 * check first OTP with valid data */
	for (i = 0; i < 3; i++) {
		temp = ov5647_check_otp(i);
		if (temp == 2) {
			otp_index = i;
			break;
		}
	}
	if (i == 3) {
		/* no valid wb OTP data */
		printk(KERN_WARNING "No valid wb otp data\n");
		return 1;
	}

	ov5647_read_otp(otp_index, &current_otp);
	if (current_otp.light_rg == 0)
		rg = current_otp.rg_ratio;
	else
		rg = current_otp.rg_ratio * (current_otp.light_rg + 128) / 256;

	if (current_otp.light_bg == 0)
		bg = current_otp.bg_ratio;
	else
		bg = current_otp.bg_ratio * (current_otp.light_bg + 128) / 256;

	/* calculate G gain
	 * 0x400 = 1x gain */
	if (bg < BG_Ratio_Typical) {
		if (rg < RG_Ratio_Typical) {
			/* current_otp.bg_ratio < BG_Ratio_typical &&
			 * current_otp.rg_ratio < RG_Ratio_typical */
			G_gain = 0x400;
			B_gain = 0x400 * BG_Ratio_Typical / bg;
			R_gain = 0x400 * RG_Ratio_Typical / rg;
		} else {
			/* current_otp.bg_ratio < BG_Ratio_typical &&
			 * current_otp.rg_ratio >= RG_Ratio_typical */
			R_gain = 0x400;
			G_gain = 0x400 * rg / RG_Ratio_Typical;
			B_gain = G_gain * BG_Ratio_Typical / bg;
		}
	} else {
		if (rg < RG_Ratio_Typical) {
			/* current_otp.bg_ratio >= BG_Ratio_typical &&
			 * current_otp.rg_ratio < RG_Ratio_typical */
			B_gain = 0x400;
			G_gain = 0x400 * bg / BG_Ratio_Typical;
			R_gain = G_gain * RG_Ratio_Typical / rg;
		} else {
			/* current_otp.bg_ratio >= BG_Ratio_typical &&
			 * current_otp.rg_ratio >= RG_Ratio_typical */
			G_gain_B = 0x400 * bg / BG_Ratio_Typical;
			G_gain_R = 0x400 * rg / RG_Ratio_Typical;
			if (G_gain_B > G_gain_R) {
				B_gain = 0x400;
				G_gain = G_gain_B;
				R_gain = G_gain * RG_Ratio_Typical / rg;
			} else {
				R_gain = 0x400;
				G_gain = G_gain_R;
				B_gain = G_gain * BG_Ratio_Typical / bg;
			}
		}
	}
	ov5647_update_awb_gain(R_gain, G_gain, B_gain);
	return 0;
}

static void ov5647_stream_on(void)
{
	pr_info("[%s:%s:%d] -------------------------\n", __FILE__, __func__, __LINE__);
	ov5647_write_reg(0x4202, 0x00);
}

static void ov5647_stream_off(void)
{
	pr_info("[%s:%s:%d] -------------------------\n", __FILE__, __func__, __LINE__);
	ov5647_write_reg(0x4202, 0x0f);
	/* both clock and data lane in LP00 */
	ov5647_write_reg(0x0100, 0x00);
}

static int ov5647_get_sysclk(void)
{
	/* calculate sysclk */
	int xvclk = ov5647_data.mclk / 10000;
	int sysclk, temp1, temp2;
	int pre_div02x, div_cnt7b, sdiv0, pll_rdiv, bit_div2x, sclk_div, VCO;
	int pre_div02x_map[] = {2, 2, 4, 6, 8, 3, 12, 5, 16, 2, 2, 2, 2, 2, 2, 2};
	int sdiv0_map[] = {16, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
	int pll_rdiv_map[] = {1, 2};
	int bit_div2x_map[] = {2, 2, 2, 2, 2, 2, 2, 2, 4, 2, 5, 2, 2, 2, 2, 2};
	int sclk_div_map[] = {1, 2, 4, 1};
	u8 temp;

	pr_info("[%s:%s:%d] -------------------------\n", __FILE__, __func__, __LINE__);

	temp1 = ov5647_read_reg(0x3037, &temp);
	temp2 = temp1 & 0x0f;
	pre_div02x = pre_div02x_map[temp2];
	temp2 = (temp1 >> 4) & 0x01;
	pll_rdiv = pll_rdiv_map[temp2];
	temp1 = ov5647_read_reg(0x3036, &temp);

	div_cnt7b = temp1;

	VCO = xvclk * 2 / pre_div02x * div_cnt7b;
	temp1 = ov5647_read_reg(0x3035, &temp);
	temp2 = temp1 >> 4;
	sdiv0 = sdiv0_map[temp2];
	temp1 = ov5647_read_reg(0x3034, &temp);
	temp2 = temp1 & 0x0f;
	bit_div2x = bit_div2x_map[temp2];
	temp1 = ov5647_read_reg(0x3106, &temp);
	temp2 = (temp1 >> 2) & 0x03;
	sclk_div = sclk_div_map[temp2];
	sysclk = VCO * 2 / sdiv0 / pll_rdiv / bit_div2x / sclk_div;
	return sysclk;
}

static void ov5647_set_night_mode(void)
{
	 /* read HTS from register settings */
	u8 mode;

	pr_info("[%s:%s:%d] -------------------------\n", __FILE__, __func__, __LINE__);

	ov5647_read_reg(0x3a00, &mode);
	mode &= 0xfb;
	ov5647_write_reg(0x3a00, mode);
}

static int ov5647_get_HTS(void)
{
	 /* read HTS from register settings */
	int HTS;
	u8 temp;

	pr_info("[%s:%s:%d] -------------------------\n", __FILE__, __func__, __LINE__);

	HTS = ov5647_read_reg(0x380c, &temp);
	HTS = (HTS << 8) + ov5647_read_reg(0x380d, &temp);

	return HTS;
}

static int ov5647_soft_reset(void)
{
	pr_info("[%s:%s:%d] -------------------------\n", __FILE__, __func__, __LINE__);
	/* soft reset ov5647 */

	ov5647_write_reg(0x0103, 0x1);
	msleep(5);

	return 0;
}

static int ov5647_get_VTS(void)
{
	 /* read VTS from register settings */
	int VTS;
	u8 temp;

	pr_info("[%s:%s:%d] -------------------------\n", __FILE__, __func__, __LINE__);

	/* total vertical size[15:8] high byte */
	VTS = ov5647_read_reg(0x380e, &temp);

	VTS = (VTS << 8) + ov5647_read_reg(0x380f, &temp);

	return VTS;
}

static int ov5647_set_VTS(int VTS)
{
	 /* write VTS to registers */
	 int temp;

	pr_info("[%s:%s:%d] -------------------------\n", __FILE__, __func__, __LINE__);

	 temp = VTS & 0xff;
	 ov5647_write_reg(0x380f, temp);

	 temp = VTS >> 8;
	 ov5647_write_reg(0x380e, temp);

	 return 0;
}

static int ov5647_get_shutter(void)
{
	 /* read shutter, in number of line period */
	int shutter;
	u8 temp;

	pr_info("[%s:%s:%d] -------------------------\n", __FILE__, __func__, __LINE__);

	shutter = (ov5647_read_reg(0x03500, &temp) & 0x0f);
	shutter = (shutter << 8) + ov5647_read_reg(0x3501, &temp);
	shutter = (shutter << 4) + (ov5647_read_reg(0x3502, &temp)>>4);

	 return shutter;
}

static int ov5647_set_shutter(int shutter)
{
	 /* write shutter, in number of line period */
	 int temp;

	pr_info("[%s:%s:%d] -------------------------\n", __FILE__, __func__, __LINE__);

	 shutter = shutter & 0xffff;

	 temp = shutter & 0x0f;
	 temp = temp << 4;
	 ov5647_write_reg(0x3502, temp);

	 temp = shutter & 0xfff;
	 temp = temp >> 4;
	 ov5647_write_reg(0x3501, temp);

	 temp = shutter >> 12;
	 ov5647_write_reg(0x3500, temp);

	 return 0;
}

static int ov5647_get_gain16(void)
{
	 /* read gain, 16 = 1x */
	int gain16;
	u8 temp;

	pr_info("[%s:%s:%d] -------------------------\n", __FILE__, __func__, __LINE__);

	gain16 = ov5647_read_reg(0x350a, &temp) & 0x03;
	gain16 = (gain16 << 8) + ov5647_read_reg(0x350b, &temp);

	return gain16;
}

static int ov5647_set_gain16(int gain16)
{
	/* write gain, 16 = 1x */
	u8 temp;

	pr_info("[%s:%s:%d] -------------------------\n", __FILE__, __func__, __LINE__);

	gain16 = gain16 & 0x3ff;

	temp = gain16 & 0xff;
	ov5647_write_reg(0x350b, temp);

	temp = gain16 >> 8;
	ov5647_write_reg(0x350a, temp);

	return 0;
}

static int ov5647_get_light_freq(void)
{
	/* get banding filter value */
	int temp, temp1, light_freq = 0;
	u8 tmp;

	pr_info("[%s:%s:%d] -------------------------\n", __FILE__, __func__, __LINE__);

	temp = ov5647_read_reg(0x3c01, &tmp);

	if (temp & 0x80) {
		/* manual */
		temp1 = ov5647_read_reg(0x3c00, &tmp);
		if (temp1 & 0x04) {
			/* 50Hz */
			light_freq = 50;
		} else {
			/* 60Hz */
			light_freq = 60;
		}
	} else {
		/* auto */
		temp1 = ov5647_read_reg(0x3c0c, &tmp);
		if (temp1 & 0x01) {
			/* 50Hz */
			light_freq = 50;
		} else {
			/* 60Hz */
		}
	}
	return light_freq;
}

static void ov5647_set_bandingfilter(void)
{
	int prev_VTS;
	int band_step60, max_band60, band_step50, max_band50;

	pr_info("[%s:%s:%d] -------------------------\n", __FILE__, __func__, __LINE__);

	/* read preview PCLK */
	prev_sysclk = ov5647_get_sysclk();
	/* read preview HTS */
	prev_HTS = ov5647_get_HTS();

	/* read preview VTS */
	prev_VTS = ov5647_get_VTS();

	/* calculate banding filter */
	/* 60Hz */
	band_step60 = prev_sysclk * 100/prev_HTS * 100/120;
	ov5647_write_reg(0x3a0a, (band_step60 >> 8));
	ov5647_write_reg(0x3a0b, (band_step60 & 0xff));

	max_band60 = (int)((prev_VTS-4)/band_step60);
	ov5647_write_reg(0x3a0d, max_band60);

	/* 50Hz */
	band_step50 = prev_sysclk * 100/prev_HTS;
	ov5647_write_reg(0x3a08, (band_step50 >> 8));
	ov5647_write_reg(0x3a09, (band_step50 & 0xff));

	max_band50 = (int)((prev_VTS-4)/band_step50);
	ov5647_write_reg(0x3a0e, max_band50);
}

static int ov5647_set_AE_target(int target)
{
	/* stable in high */
	int fast_high, fast_low;
	
	pr_info("[%s:%s:%d] -------------------------\n", __FILE__, __func__, __LINE__);

	AE_low = target * 23 / 25;	/* 0.92 */
	AE_high = target * 27 / 25;	/* 1.08 */

	fast_high = AE_high<<1;
	if (fast_high > 255)
		fast_high = 255;

	fast_low = AE_low >> 1;

	ov5647_write_reg(0x3a0f, AE_high);
	ov5647_write_reg(0x3a10, AE_low);
	ov5647_write_reg(0x3a1b, AE_high);
	ov5647_write_reg(0x3a1e, AE_low);
	ov5647_write_reg(0x3a11, fast_high);
	ov5647_write_reg(0x3a1f, fast_low);

	return 0;
}

static void ov5647_turn_on_AE_AG(int enable)
{
	u8 ae_ag_ctrl;

	pr_info("[%s:%s:%d] -------------------------\n", __FILE__, __func__, __LINE__);

	ov5647_read_reg(0x3503, &ae_ag_ctrl);
	if (enable) {
		/* turn on auto AE/AG */
		ae_ag_ctrl = ae_ag_ctrl & ~(0x03);
	} else {
		/* turn off AE/AG */
		ae_ag_ctrl = ae_ag_ctrl | 0x03;
	}
	ov5647_write_reg(0x3503, ae_ag_ctrl);
}

static void ov5647_set_virtual_channel(int channel)
{
	u8 channel_id;

	pr_info("[%s:%s:%d] -------------------------\n", __FILE__, __func__, __LINE__);

	ov5647_read_reg(0x4814, &channel_id);
	channel_id &= ~(3 << 6);
	ov5647_write_reg(0x4814, channel_id | (channel << 6));
}

/* download ov5647 settings to sensor through i2c */
static int ov5647_download_firmware(struct reg_value *pModeSetting, s32 ArySize)
{
	register u32 Delay_ms = 0;
	register u16 RegAddr = 0;
	register u8 Mask = 0;
	register u8 Val = 0;
	u8 RegVal = 0;
	int i, retval = 0;

	pr_info("[%s:%s:%d] -------------------------\n", __FILE__, __func__, __LINE__);

	for (i = 0; i < ArySize; ++i, ++pModeSetting) {
		Delay_ms = pModeSetting->u32Delay_ms;
		RegAddr = pModeSetting->u16RegAddr;
		Val = pModeSetting->u8Val;
		Mask = pModeSetting->u8Mask;

		if (Mask) {
			retval = ov5647_read_reg(RegAddr, &RegVal);
			if (retval < 0)
				goto err;

			RegVal &= ~(u8)Mask;
			Val &= Mask;
			Val |= RegVal;
		}

		retval = ov5647_write_reg(RegAddr, Val);
		if (retval < 0)
			goto err;

		if (Delay_ms)
			msleep(Delay_ms);
	}
err:
	return retval;
}

/* sensor changes between scaling and subsampling
 * go through exposure calcualtion
 */
static int ov5647_change_mode_exposure_calc(enum ov5647_frame_rate frame_rate,
				enum ov5647_mode mode)
{
	struct reg_value *pModeSetting = NULL;
	s32 ArySize = 0;
	int pre_shutter, pre_gain16;
	int cap_shutter, cap_gain16;
	int pre_sysclk, pre_HTS;
	int cap_sysclk, cap_HTS, cap_VTS;
	long cap_gain16_shutter;
	int retval = 0;

	pr_info("[%s:%s:%d] -------------------------\n", __FILE__, __func__, __LINE__);

	/* check if the input mode and frame rate is valid */
	pModeSetting =
		ov5647_mode_info_data[frame_rate][mode].init_data_ptr;
	ArySize =
		ov5647_mode_info_data[frame_rate][mode].init_data_size;

	ov5647_data.pix.width =
		ov5647_mode_info_data[frame_rate][mode].width;
	ov5647_data.pix.height =
		ov5647_mode_info_data[frame_rate][mode].height;

	if (ov5647_data.pix.width == 0 || ov5647_data.pix.height == 0 ||
		pModeSetting == NULL || ArySize == 0)
		return -EINVAL;

	ov5647_stream_off();

	/* turn off night mode for capture */
	ov5647_set_night_mode();

	pre_shutter = ov5647_get_shutter();
	pre_gain16 = ov5647_get_gain16();
	pre_HTS = ov5647_get_HTS();
	pre_sysclk = ov5647_get_sysclk();

	/* Write capture setting */
	retval = ov5647_download_firmware(pModeSetting, ArySize);
	if (retval < 0)
		goto err;

	/* read capture VTS */
	cap_VTS = ov5647_get_VTS();
	cap_HTS = ov5647_get_HTS();
	cap_sysclk = ov5647_get_sysclk();

	/* calculate capture shutter/gain16 */
	cap_shutter = pre_shutter * cap_sysclk/pre_sysclk * pre_HTS / cap_HTS;

	if (cap_shutter < 16) {
		cap_gain16_shutter = pre_shutter * pre_gain16 *
				cap_sysclk / pre_sysclk * pre_HTS / cap_HTS;
		cap_shutter = ((int)(cap_gain16_shutter / 16));
		if (cap_shutter < 1)
			cap_shutter = 1;
		cap_gain16 = ((int)(cap_gain16_shutter / cap_shutter));
		if (cap_gain16 < 16)
			cap_gain16 = 16;
	} else
		cap_gain16 = pre_gain16;

	/* gain to shutter */
	while ((cap_gain16 > 32) &&
			(cap_shutter < ((int)((cap_VTS - 4) / 2)))) {
		cap_gain16 = cap_gain16 / 2;
		cap_shutter = cap_shutter * 2;
	}
	/* write capture gain */
	ov5647_set_gain16(cap_gain16);

	/* write capture shutter */
	if (cap_shutter > (cap_VTS - 4)) {
		cap_VTS = cap_shutter + 4;
		ov5647_set_VTS(cap_VTS);
	}
	ov5647_set_shutter(cap_shutter);

err:
	return retval;
}

/* if sensor changes inside scaling or subsampling
 * change mode directly
 * */
static int ov5647_change_mode_direct(enum ov5647_frame_rate frame_rate,
				enum ov5647_mode mode)
{
	struct reg_value *pModeSetting = NULL;
	s32 ArySize = 0;
	int retval = 0;
	
	pr_info("[%s:%s:%d] -------------------------\n", __FILE__, __func__, __LINE__);

	/* check if the input mode and frame rate is valid */
	pModeSetting =
		ov5647_mode_info_data[frame_rate][mode].init_data_ptr;
	ArySize =
		ov5647_mode_info_data[frame_rate][mode].init_data_size;

	ov5647_data.pix.width =
		ov5647_mode_info_data[frame_rate][mode].width;
	ov5647_data.pix.height =
		ov5647_mode_info_data[frame_rate][mode].height;

	if (ov5647_data.pix.width == 0 || ov5647_data.pix.height == 0 ||
		pModeSetting == NULL || ArySize == 0)
		return -EINVAL;

	/* turn off AE/AG */
	ov5647_turn_on_AE_AG(0);

	ov5647_stream_off();

	/* Write capture setting */
	retval = ov5647_download_firmware(pModeSetting, ArySize);
	if (retval < 0)
		goto err;

	ov5647_stream_on();	// new code

	ov5647_turn_on_AE_AG(1);

err:
	return retval;
}

static int ov5647_init_mode(enum ov5647_frame_rate frame_rate,
			    enum ov5647_mode mode, enum ov5647_mode orig_mode)
{
	struct reg_value *pModeSetting = NULL;
	s32 ArySize = 0;
	int retval = 0;
	void *mipi_csi2_info;	// new code
	u32 mipi_reg;	// new code
	u32 msec_wait4stable = 0;
	enum ov5647_downsize_mode dn_mode, orig_dn_mode;

	pr_info("[%s:%s:%d] -------------------------\n", __FILE__, __func__, __LINE__);

	if ((mode > ov5647_mode_MAX || mode < ov5647_mode_MIN)
		&& (mode != ov5647_mode_INIT)) {
		pr_err("Wrong ov5647 mode detected!\n");
		return -1;
	}

	// Start new code
	mipi_csi2_info = mipi_csi2_get_info();

	/* initial mipi dphy */
	if (!mipi_csi2_info) {
		printk(KERN_ERR "%s() in %s: Fail to get mipi_csi2_info!\n",
		       __func__, __FILE__);
		return -1;
	}

	if (!mipi_csi2_get_status(mipi_csi2_info))
		mipi_csi2_enable(mipi_csi2_info);

	if (!mipi_csi2_get_status(mipi_csi2_info)) {
		pr_err("Can not enable mipi csi2 driver!\n");
		return -1;
	}

	mipi_csi2_set_lanes(mipi_csi2_info, 2);

	/*Only reset MIPI CSI2 HW at sensor initialize*/
	if (mode == ov5647_mode_INIT) {
		mipi_csi2_reset(mipi_csi2_info);
		pr_debug("%s: mode == ov5647_mode_INIT\n", __func__);
	}

/*	if (ov5647_data.pix.pixelformat == V4L2_PIX_FMT_UYVY)
		mipi_csi2_set_datatype(mipi_csi2_info, MIPI_DT_YUV422);
	else if (ov5647_data.pix.pixelformat == V4L2_PIX_FMT_RGB565)
		mipi_csi2_set_datatype(mipi_csi2_info, MIPI_DT_RGB565);*/
	if (ov5647_data.pix.pixelformat == V4L2_PIX_FMT_SBGGR8){
		pr_info("setting mipi datatype to MIPI_DT_RAW8");
		mipi_csi2_set_datatype(mipi_csi2_info, MIPI_DT_RAW8);
	}
	else if (ov5647_data.pix.pixelformat == V4L2_PIX_FMT_SBGGR10){
		pr_info("setting mipi datatype to MIPI_DT_RAW10");
		mipi_csi2_set_datatype(mipi_csi2_info, MIPI_DT_RAW10);
	}
	else if (ov5647_data.pix.pixelformat == V4L2_PIX_FMT_GREY){
		pr_info("setting mipi datatype to MIPI_DT_RAW10");
		mipi_csi2_set_datatype(mipi_csi2_info, MIPI_DT_RAW10);
	}
	else
		pr_err("currently this sensor format can not be supported!\n");
	// End new code

	dn_mode = ov5647_mode_info_data[frame_rate][mode].dn_mode;
	orig_dn_mode = ov5647_mode_info_data[frame_rate][orig_mode].dn_mode;
	if (mode == ov5647_mode_INIT) {
		ov5647_soft_reset();
		pModeSetting = ov5647_init_setting;
		ArySize = ARRAY_SIZE(ov5647_init_setting);
		retval = ov5647_download_firmware(pModeSetting, ArySize);
		if (retval < 0)
			goto err;
		pModeSetting = ov5647_setting_60fps_VGA_640_480;
		ArySize = ARRAY_SIZE(ov5647_setting_60fps_VGA_640_480);
		retval = ov5647_download_firmware(pModeSetting, ArySize);

		ov5647_data.pix.width = 640;
		ov5647_data.pix.height = 480;
	} else if ((dn_mode == SUBSAMPLING && orig_dn_mode == SCALING) ||
			(dn_mode == SCALING && orig_dn_mode == SUBSAMPLING)) {
		/* change between subsampling and scaling
		 * go through exposure calucation */
		retval = ov5647_change_mode_exposure_calc(frame_rate, mode);
	} else {
		/* change inside subsampling or scaling
		 * download firmware directly */
		retval = ov5647_change_mode_direct(frame_rate, mode);
	}

	if (retval < 0)
		goto err;

	ov5647_set_AE_target(AE_Target);
	ov5647_get_light_freq();
	ov5647_set_bandingfilter();
	//ov5647_set_virtual_channel(ov5647_data.csi);
	ov5647_set_virtual_channel(ov5647_data.virtual_channel);

	/* add delay to wait for sensor stable */
	if (mode == ov5647_mode_QSXGA_2592_1944) {
		/* dump the first two frames: 1/7.5*2
		 * the frame rate of QSXGA is 7.5fps */
		msec_wait4stable = 267;
	} else if (frame_rate == ov5647_15_fps) {
		/* dump the first nine frames: 1/15*9 */
		msec_wait4stable = 600;
	} else if (frame_rate == ov5647_30_fps) {
		/* dump the first nine frames: 1/30*9 */
		msec_wait4stable = 300;
	}
	msleep(msec_wait4stable);

	// Start new code
	if (mipi_csi2_info) {
		unsigned int i = 0;

		/* wait for mipi sensor ready */
		while (1) {
			mipi_reg = mipi_csi2_dphy_status(mipi_csi2_info);
			if (mipi_reg != 0x200)
				break;
			if (i++ >= 20) {
				pr_err("mipi csi2 can not receive sensor clk! %x\n", mipi_reg);
				return -1;
			}
			msleep(10);
		}

		i = 0;
		/* wait for mipi stable */
		while (1) {
			mipi_reg = mipi_csi2_get_error1(mipi_csi2_info);
			if (!mipi_reg)
				break;
			if (i++ >= 20) {
				pr_err("mipi csi2 can not receive data correctly!\n");
				return -1;
			}
			msleep(10);
		}

	}
	// End new code

err:
	return retval;
}

/*!
 * ov5647_s_power - V4L2 sensor interface handler for VIDIOC_S_POWER ioctl
 * @s: pointer to standard V4L2 device structure
 * @on: indicates power mode (on or off)
 *
 * Turns the power on or off, depending on the value of on and returns the
 * appropriate error code.
 */
//static int ov5647_s_power(struct v4l2_subdev *sd, int on)
static int ioctl_s_power(struct v4l2_int_device *s, int on)
{
	//struct i2c_client *client = v4l2_get_subdevdata(sd);
	//struct ov5647 *sensor = to_ov5647(client);
	struct sensor_data *sensor = s->priv;

	pr_info("[%s:%s:%d] -------------------------\n", __FILE__, __func__, __LINE__);

	if (on && !sensor->on) {
		if (io_regulator)
			if (regulator_enable(io_regulator) != 0)
				return -EIO;
		if (core_regulator)
			if (regulator_enable(core_regulator) != 0)
				return -EIO;
		if (gpo_regulator)
			if (regulator_enable(gpo_regulator) != 0)
				return -EIO;
		if (analog_regulator)
			if (regulator_enable(analog_regulator) != 0)
				return -EIO;
	} else if (!on && sensor->on) {
		if (analog_regulator)
			regulator_disable(analog_regulator);
		if (core_regulator)
			regulator_disable(core_regulator);
		if (io_regulator)
			regulator_disable(io_regulator);
		if (gpo_regulator)
			regulator_disable(gpo_regulator);
	}

	sensor->on = on;

	return 0;
}

/*!
 * ov5647_g_parm - V4L2 sensor interface handler for VIDIOC_G_PARM ioctl
 * @s: pointer to standard V4L2 sub device structure
 * @a: pointer to standard V4L2 VIDIOC_G_PARM ioctl structure
 *
 * Returns the sensor's video CAPTURE parameters.
 */
//static int ov5647_g_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *a)
static int ov5647_g_parm(struct v4l2_int_device *s, struct v4l2_streamparm *a)
{
	//struct i2c_client *client = v4l2_get_subdevdata(sd);
	//struct ov5647 *sensor = to_ov5647(client);
	struct sensor_data *sensor = s->priv;

	struct v4l2_captureparm *cparm = &a->parm.capture;
	int ret = 0;

	pr_info("[%s:%s:%d] -------------------------\n", __FILE__, __func__, __LINE__);

	switch (a->type) {
	/* This is the only case currently handled. */
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		memset(a, 0, sizeof(*a));
		a->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		cparm->capability = sensor->streamcap.capability;
		cparm->timeperframe = sensor->streamcap.timeperframe;
		cparm->capturemode = sensor->streamcap.capturemode;
		ret = 0;
		break;

	/* These are all the possible cases. */
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
	case V4L2_BUF_TYPE_VBI_CAPTURE:
	case V4L2_BUF_TYPE_VBI_OUTPUT:
	case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
	case V4L2_BUF_TYPE_SLICED_VBI_OUTPUT:
		ret = -EINVAL;
		break;

	default:
		pr_debug("   type is unknown - %d\n", a->type);
		ret = -EINVAL;
		break;
	}

	return ret;
}

/*!
 * ov5460_s_parm - V4L2 sensor interface handler for VIDIOC_S_PARM ioctl
 * @s: pointer to standard V4L2 sub device structure
 * @a: pointer to standard V4L2 VIDIOC_S_PARM ioctl structure
 *
 * Configures the sensor to use the input parameters, if possible.  If
 * not possible, reverts to the old parameters and returns the
 * appropriate error code.
 */
//static int ov5647_s_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *a)
static int ov5647_s_parm(struct v4l2_int_device *s, struct v4l2_streamparm *a)
{
	//struct i2c_client *client = v4l2_get_subdevdata(sd);
	//struct ov5647 *sensor = to_ov5647(client);
	struct sensor_data *sensor = s->priv;

	struct v4l2_fract *timeperframe = &a->parm.capture.timeperframe;
	u32 tgt_fps;	/* target frames per secound */
	enum ov5647_frame_rate frame_rate;
	enum ov5647_mode orig_mode;
	int ret = 0;
	
	pr_info("[%s:%s:%d] -------------------------\n", __FILE__, __func__, __LINE__);

	/* Make sure power on */
	ov5647_power_down(0);

	switch (a->type) {
	/* This is the only case currently handled. */
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		/* Check that the new frame rate is allowed. */
		if ((timeperframe->numerator == 0) ||
		    (timeperframe->denominator == 0)) {
			timeperframe->denominator = DEFAULT_FPS;
			timeperframe->numerator = 1;
		}

		tgt_fps = timeperframe->denominator /
			  timeperframe->numerator;

		if (tgt_fps > MAX_FPS) {
			timeperframe->denominator = MAX_FPS;
			timeperframe->numerator = 1;
		} else if (tgt_fps < MIN_FPS) {
			timeperframe->denominator = MIN_FPS;
			timeperframe->numerator = 1;
		}

		/* Actual frame rate we use */
		tgt_fps = timeperframe->denominator /
			  timeperframe->numerator;

		if (tgt_fps == 15)
			frame_rate = ov5647_15_fps;
		else if (tgt_fps == 30)
			frame_rate = ov5647_30_fps;
		else {
			pr_err(" The camera frame rate is not supported!\n");
			return -EINVAL;
		}

		orig_mode = sensor->streamcap.capturemode;
		ret = ov5647_init_mode(frame_rate,
				(u32)a->parm.capture.capturemode, orig_mode);
		if (ret < 0)
			return ret;

		sensor->streamcap.timeperframe = *timeperframe;
		sensor->streamcap.capturemode =
				(u32)a->parm.capture.capturemode;

		break;

	/* These are all the possible cases. */
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
	case V4L2_BUF_TYPE_VBI_CAPTURE:
	case V4L2_BUF_TYPE_VBI_OUTPUT:
	case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
	case V4L2_BUF_TYPE_SLICED_VBI_OUTPUT:
		pr_debug("type is not " \
			"V4L2_BUF_TYPE_VIDEO_CAPTURE but %d\n",
			a->type);
		ret = -EINVAL;
		break;

	default:
		pr_debug("type is unknown - %d\n", a->type);
		ret = -EINVAL;
		break;
	}

	return ret;
}
#if 0
/* list of image formats supported by OV5647 sensor */
static const struct v4l2_fmtdesc ov5647_formats[] = {
	{
		.description = "RAW8(BGGR)",
		.pixelformat = V4L2_PIX_FMT_SBGGR8,
	}, {
		.description = "GREY",
		.pixelformat = V4L2_PIX_FMT_GREY,
	},
};

#define OV5647_NUM_CAPTURE_FORMATS	ARRAY_SIZE(ov5647_formats)

//static int ov5647_try_fmt(struct v4l2_subdev *sd,
//			  struct v4l2_mbus_framefmt *mf)
static int ov5647_try_fmt(struct v4l2_int_device *s,
			  struct v4l2_format *f)
{
	struct v4l2_pix_format *pix = &f->fmt.pix;	// new code
	int ifmt;	// new code

	//const struct ov5647_datafmt *fmt = ov5647_find_datafmt(f->code);

	pr_info("[%s:%s:%d] -------------------------\n", __FILE__, __func__, __LINE__);

	//if (!fmt) {
	//	mf->code	= ov5647_colour_fmts[0].code;
	//	mf->colorspace	= ov5647_colour_fmts[0].colorspace;
	//}
	//mf->field	= V4L2_FIELD_NONE;
	
	for (ifmt = 0; ifmt < OV5647_NUM_CAPTURE_FORMATS; ifmt++)
				if (pix->pixelformat == ov5647_formats[ifmt].pixelformat)
								break;

	if (ifmt == OV5647_NUM_CAPTURE_FORMATS)
			ifmt = 0;	/* Default = RAW8 */

	pix->pixelformat = ov5647_formats[ifmt].pixelformat;	// new code
	pix->field = V4L2_FIELD_NONE;	// new code

	return 0;
}

static int ov5647_s_fmt(struct v4l2_subdev *sd,
			struct v4l2_mbus_framefmt *mf)
{
	//struct i2c_client *client = v4l2_get_subdevdata(sd);
	//struct ov5647 *sensor = to_ov5647(client);
	struct sensor_data *sensor = s->priv;	// new code

	pr_info("[%s:%s:%d] -------------------------\n", __FILE__, __func__, __LINE__);

	/* MIPI CSI could have changed the format, double-check */
	if (!ov5647_find_datafmt(mf->code))
		return -EINVAL;

	ov5647_try_fmt(sd, mf);
	//sensor->fmt = ov5647_find_datafmt(mf->code);

	return 0;
}

static int ov5647_g_fmt(struct v4l2_subdev *sd,
			struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov5647 *sensor = to_ov5647(client);

	const struct ov5647_datafmt *fmt = sensor->fmt;

	pr_info("[%s:%s:%d] -------------------------\n", __FILE__, __func__, __LINE__);

	mf->code	= fmt->code;
	mf->colorspace	= fmt->colorspace;
	mf->field	= V4L2_FIELD_NONE;

	return 0;
}
#endif

/*!
 * ioctl_g_fmt_cap - V4L2 sensor interface handler for ioctl_g_fmt_cap
 * @s: pointer to standard V4L2 device structure
 * @f: pointer to standard V4L2 v4l2_format structure
 *
 * Returns the sensor's current pixel format in the v4l2_format
 * parameter.
 */
static int ioctl_g_fmt_cap(struct v4l2_int_device *s, struct v4l2_format *f)
{
	struct sensor_data *sensor = s->priv;

	switch (f->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		f->fmt.pix = sensor->pix;
		pr_debug("%s: %dx%d\n", __func__, sensor->pix.width, sensor->pix.height);
		break;

	case V4L2_BUF_TYPE_SENSOR:
		pr_debug("%s: left=%d, top=%d, %dx%d\n", __func__,
			sensor->spix.left, sensor->spix.top,
			sensor->spix.swidth, sensor->spix.sheight);
		f->fmt.spix = sensor->spix;
		break;

	case V4L2_BUF_TYPE_PRIVATE:
		break;

	default:
		f->fmt.pix = sensor->pix;
		break;
	}

	return 0;
}

#if 0
static int ov5647_enum_fmt(struct v4l2_subdev *sd, unsigned int index,
			   u32 *code)
{
	pr_info("[%s:%s:%d] -------------------------\n", __FILE__, __func__, __LINE__);

	if (index >= ARRAY_SIZE(ov5647_colour_fmts))
		return -EINVAL;

	*code = ov5647_colour_fmts[index].code;
	return 0;
}
#endif

/*!
 * ioctl_enum_fmt_cap - V4L2 sensor interface handler for VIDIOC_ENUM_FMT
 * @s: pointer to standard V4L2 device structure
 * @fmt: pointer to standard V4L2 fmt description structure
 *
 * Return 0.
 */
static int ioctl_enum_fmt_cap(struct v4l2_int_device *s,
			      struct v4l2_fmtdesc *fmt)
{
	if (fmt->index > ov5647_mode_MAX)
		return -EINVAL;

	fmt->pixelformat = ov5647_data.pix.pixelformat;

	return 0;
}

/*!
 * ov5647_enum_framesizes - V4L2 sensor interface handler for
 *			   VIDIOC_ENUM_FRAMESIZES ioctl
 * @s: pointer to standard V4L2 device structure
 * @fsize: standard V4L2 VIDIOC_ENUM_FRAMESIZES ioctl structure
 *
 * Return 0 if successful, otherwise -EINVAL.
 */
//static int ov5647_enum_framesizes(struct v4l2_subdev *sd,
//			       struct v4l2_subdev_pad_config *cfg,
//			       struct v4l2_subdev_frame_size_enum *fse)
static int ioctl_enum_framesizes(struct v4l2_int_device *s,
				 struct v4l2_frmsizeenum *fsize)
{
	pr_info("[%s:%s:%d] -------------------------\n", __FILE__, __func__, __LINE__);

	if (fsize->index > ov5647_mode_MAX)
		return -EINVAL;

	fsize->pixel_format = ov5647_data.pix.pixelformat;	// new code
	//fse->max_width =
	fsize->discrete.width =
			max(ov5647_mode_info_data[0][fsize->index].width,
			    ov5647_mode_info_data[1][fsize->index].width);
	//fse->min_width = fse->max_width;
	//fse->max_height =
	fsize->discrete.height =
			max(ov5647_mode_info_data[0][fsize->index].height,
			    ov5647_mode_info_data[1][fsize->index].height);
	//fse->min_height = fse->max_height;
	return 0;
}

/*!
 * ov5647_enum_frameintervals - V4L2 sensor interface handler for
 *			       VIDIOC_ENUM_FRAMEINTERVALS ioctl
 * @s: pointer to standard V4L2 device structure
 * @fival: standard V4L2 VIDIOC_ENUM_FRAMEINTERVALS ioctl structure
 *
 * Return 0 if successful, otherwise -EINVAL.
 */
//static int ov5647_enum_frameintervals(struct v4l2_subdev *sd,
//		struct v4l2_subdev_pad_config *cfg,
//		struct v4l2_subdev_frame_interval_enum *fie)
static int ioctl_enum_frameintervals(struct v4l2_int_device *s,
					 struct v4l2_frmivalenum *fival)
{
	int i, j, count;

	pr_info("[%s:%s:%d] -------------------------\n", __FILE__, __func__, __LINE__);

	//if (fie->index < 0 || fie->index > ov5647_mode_MAX)
	//	return -EINVAL;

	//if (fie->width == 0 || fie->height == 0 ||
	//    fie->code == 0) {
	//	pr_warning("Please assign pixel format, width and height.\n");
	//	return -EINVAL;
	//}

	//fie->interval.numerator = 1;
	fival->type = V4L2_FRMIVAL_TYPE_DISCRETE;
	fival->discrete.numerator = 1;

	count = 0;
	for (i = 0; i < ARRAY_SIZE(ov5647_mode_info_data); i++) {
		for (j = 0; j < (ov5647_mode_MAX + 1); j++) {
			//if (fie->width == ov5647_mode_info_data[i][j].width
			// && fie->height == ov5647_mode_info_data[i][j].height
			// && ov5647_mode_info_data[i][j].init_data_ptr != NULL) {
			//	count++;
			//}
			if (fival->pixel_format == ov5647_data.pix.pixelformat
			 && fival->width == ov5647_mode_info_data[i][j].width
			 && fival->height == ov5647_mode_info_data[i][j].height
			 && ov5647_mode_info_data[i][j].init_data_ptr != NULL
			 && fival->index == count++) {
			//if (fie->index == (count - 1)) {
				//fie->interval.denominator =
				fival->discrete.denominator =
						ov5647_framerates[i];
				return 0;
			}
		}
	}

	return -EINVAL;
}

/*!
 * dev_init - V4L2 sensor init
 * @s: pointer to standard V4L2 device structure
 *
 */
//static int init_device(void)
static int ioctl_dev_init(struct v4l2_int_device *s)
{
	u32 tgt_xclk;	/* target xclk */
	u32 tgt_fps;	/* target frames per secound */
	enum ov5647_frame_rate frame_rate;
	int ret;
	void *mipi_csi2_info;	// new code
	
	pr_info("[%s:%s:%d] -------------------------\n", __FILE__, __func__, __LINE__);

	ov5647_data.on = true;

	/* mclk */
	tgt_xclk = ov5647_data.mclk;
	tgt_xclk = min(tgt_xclk, (u32)OV5647_XCLK_MAX);
	tgt_xclk = max(tgt_xclk, (u32)OV5647_XCLK_MIN);
	ov5647_data.mclk = tgt_xclk;

	pr_debug("   Setting mclk to %d MHz\n", tgt_xclk / 1000000);

	/* Default camera frame rate is set in probe */
	tgt_fps = ov5647_data.streamcap.timeperframe.denominator /
		  ov5647_data.streamcap.timeperframe.numerator;

	if (tgt_fps == 15)
		frame_rate = ov5647_15_fps;
	else if (tgt_fps == 30)
		frame_rate = ov5647_30_fps;
	else
		return -EINVAL; /* Only support 15fps or 30fps now. */

	// Start new code 
	mipi_csi2_info = mipi_csi2_get_info();

	/* enable mipi csi2 */
	if (mipi_csi2_info)
		mipi_csi2_enable(mipi_csi2_info);
	else {
		printk(KERN_ERR "%s() in %s: Fail to get mipi_csi2_info!\n",
		       __func__, __FILE__);
		return -EPERM;
	}
	// End New code

	ret = ov5647_init_mode(frame_rate, ov5647_mode_INIT, ov5647_mode_INIT);

	ov5647_update_otp();
	return ret;
}

#if 0
static int ov5647_s_stream(struct v4l2_subdev *sd, int enable)
{
	pr_info("[%s:%s:%d] -------------------------\n", __FILE__, __func__, __LINE__);

	if (enable)
		ov5647_stream_on();
	else
		ov5647_stream_off();
	return 0;
}

static struct v4l2_subdev_video_ops ov5647_subdev_video_ops = {
	.g_parm = ov5647_g_parm,
	.s_parm = ov5647_s_parm,
	.s_stream = ov5647_s_stream,

	.s_mbus_fmt	= ov5647_s_fmt,
	.g_mbus_fmt	= ov5647_g_fmt,
	.try_mbus_fmt	= ov5647_try_fmt,
	.enum_mbus_fmt	= ov5647_enum_fmt,
};

static const struct v4l2_subdev_pad_ops ov5647_subdev_pad_ops = {
	.enum_frame_size       = ov5647_enum_framesizes,
	.enum_frame_interval   = ov5647_enum_frameintervals,
};

static struct v4l2_subdev_core_ops ov5647_subdev_core_ops = {
	.s_power	= ov5647_s_power,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register	= ov5647_get_register,
	.s_register	= ov5647_set_register,
#endif
};

static struct v4l2_subdev_ops ov5647_subdev_ops = {
	.core	= &ov5647_subdev_core_ops,
	.video	= &ov5647_subdev_video_ops,
	.pad	= &ov5647_subdev_pad_ops,
};
#endif
// start new code
/*!
 * ioctl_dev_exit - V4L2 sensor interface handler for vidioc_int_dev_exit_num
 * @s: pointer to standard V4L2 device structure
 *
 * Delinitialise the device when slave detaches to the master.
 */
static int ioctl_dev_exit(struct v4l2_int_device *s)
{
	void *mipi_csi2_info;
	pr_info("[%s:%s:%d] -------------------------\n", __FILE__, __func__, __LINE__);

	mipi_csi2_info = mipi_csi2_get_info();

	/* disable mipi csi2 */
	if (mipi_csi2_info)
		if (mipi_csi2_get_status(mipi_csi2_info))
			mipi_csi2_disable(mipi_csi2_info);

	return 0;
}

/*!
 * ioctl_init - V4L2 sensor interface handler for VIDIOC_INT_INIT
 * @s: pointer to standard V4L2 device structure
 */
static int ioctl_init(struct v4l2_int_device *s)
{
	pr_info("[%s:%s:%d] -------------------------\n", __FILE__, __func__, __LINE__);

	return 0;
}

static int ioctl_g_ifparm(struct v4l2_int_device *s, struct v4l2_ifparm *p)
{
	if (s == NULL) {
		pr_err("   ERROR!! no slave device set!\n");
		return -1;
	}

	memset(p, 0, sizeof(*p));
	p->u.bt656.clock_curr = ov5647_data.mclk;
	pr_debug("   clock_curr=mclk=%d\n", ov5647_data.mclk);
	p->if_type = V4L2_IF_TYPE_BT656;
	p->u.bt656.mode = V4L2_IF_TYPE_BT656_MODE_NOBT_8BIT;
	p->u.bt656.clock_min = OV5647_XCLK_MIN;
	p->u.bt656.clock_max = OV5647_XCLK_MAX;
	p->u.bt656.bt_sync_correct = 1;  /* Indicate external vsync */

	return 0;
}

/*!
 * ioctl_g_parm - V4L2 sensor interface handler for VIDIOC_G_PARM ioctl
 * @s: pointer to standard V4L2 device structure
 * @a: pointer to standard V4L2 VIDIOC_G_PARM ioctl structure
 *
 * Returns the sensor's video CAPTURE parameters.
 */
static int ioctl_g_parm(struct v4l2_int_device *s, struct v4l2_streamparm *a)
{
	struct sensor_data *sensor = s->priv;
	struct v4l2_captureparm *cparm = &a->parm.capture;
	int ret = 0;

	switch (a->type) {
	/* This is the only case currently handled. */
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		memset(a, 0, sizeof(*a));
		a->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		cparm->capability = sensor->streamcap.capability;
		cparm->timeperframe = sensor->streamcap.timeperframe;
		cparm->capturemode = sensor->streamcap.capturemode;
		ret = 0;
		break;

	/* These are all the possible cases. */
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
	case V4L2_BUF_TYPE_VBI_CAPTURE:
	case V4L2_BUF_TYPE_VBI_OUTPUT:
	case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
	case V4L2_BUF_TYPE_SLICED_VBI_OUTPUT:
		ret = -EINVAL;
		break;

	default:
		pr_debug("   type is unknown - %d\n", a->type);
		ret = -EINVAL;
		break;
	}

	return ret;
}

/*!
 * ioctl_s_parm - V4L2 sensor interface handler for VIDIOC_S_PARM ioctl
 * @s: pointer to standard V4L2 device structure
 * @a: pointer to standard V4L2 VIDIOC_S_PARM ioctl structure
 *
 * Configures the sensor to use the input parameters, if possible.  If
 * not possible, reverts to the old parameters and returns the
 * appropriate error code.
 */
static int ioctl_s_parm(struct v4l2_int_device *s, struct v4l2_streamparm *a)
{
	struct sensor_data *sensor = s->priv;
	struct v4l2_fract *timeperframe = &a->parm.capture.timeperframe;
	u32 tgt_fps;	/* target frames per secound */
	enum ov5647_frame_rate frame_rate;
	enum ov5647_mode orig_mode;
	int ret = 0;

	/* Make sure power on */
	ov5647_power_down(0);

	switch (a->type) {
	/* This is the only case currently handled. */
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		/* Check that the new frame rate is allowed. */
		if ((timeperframe->numerator == 0) ||
		    (timeperframe->denominator == 0)) {
			timeperframe->denominator = DEFAULT_FPS;
			timeperframe->numerator = 1;
		}

		tgt_fps = timeperframe->denominator /
			  timeperframe->numerator;

		if (tgt_fps > MAX_FPS) {
			timeperframe->denominator = MAX_FPS;
			timeperframe->numerator = 1;
		} else if (tgt_fps < MIN_FPS) {
			timeperframe->denominator = MIN_FPS;
			timeperframe->numerator = 1;
		}

		/* Actual frame rate we use */
		tgt_fps = timeperframe->denominator /
			  timeperframe->numerator;

		if (tgt_fps == 15)
			frame_rate = ov5647_15_fps;
		else if (tgt_fps == 30)
			frame_rate = ov5647_30_fps;
		else {
			pr_err(" The camera frame rate is not supported!\n");
			return -EINVAL;
		}

		orig_mode = sensor->streamcap.capturemode;
		ret = ov5647_init_mode(frame_rate,
				(u32)a->parm.capture.capturemode, orig_mode);
		if (ret < 0)
			return ret;

		sensor->streamcap.timeperframe = *timeperframe;
		sensor->streamcap.capturemode =
				(u32)a->parm.capture.capturemode;

		break;

	/* These are all the possible cases. */
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
	case V4L2_BUF_TYPE_VBI_CAPTURE:
	case V4L2_BUF_TYPE_VBI_OUTPUT:
	case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
	case V4L2_BUF_TYPE_SLICED_VBI_OUTPUT:
		pr_debug("   type is not " \
			"V4L2_BUF_TYPE_VIDEO_CAPTURE but %d\n",
			a->type);
		ret = -EINVAL;
		break;

	default:
		pr_debug("   type is unknown - %d\n", a->type);
		ret = -EINVAL;
		break;
	}

	return ret;
}

/*!
 * ioctl_g_ctrl - V4L2 sensor interface handler for VIDIOC_G_CTRL ioctl
 * @s: pointer to standard V4L2 device structure
 * @vc: standard V4L2 VIDIOC_G_CTRL ioctl structure
 *
 * If the requested control is supported, returns the control's current
 * value from the video_control[] array.  Otherwise, returns -EINVAL
 * if the control is not supported.
 */
static int ioctl_g_ctrl(struct v4l2_int_device *s, struct v4l2_control *vc)
{
	int ret = 0;

	switch (vc->id) {
	case V4L2_CID_BRIGHTNESS:
		vc->value = ov5647_data.brightness;
		break;
	case V4L2_CID_HUE:
		vc->value = ov5647_data.hue;
		break;
	case V4L2_CID_CONTRAST:
		vc->value = ov5647_data.contrast;
		break;
	case V4L2_CID_SATURATION:
		vc->value = ov5647_data.saturation;
		break;
	case V4L2_CID_RED_BALANCE:
		vc->value = ov5647_data.red;
		break;
	case V4L2_CID_BLUE_BALANCE:
		vc->value = ov5647_data.blue;
		break;
	case V4L2_CID_EXPOSURE:
		vc->value = ov5647_data.ae_mode;
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

/*!
 * ioctl_s_ctrl - V4L2 sensor interface handler for VIDIOC_S_CTRL ioctl
 * @s: pointer to standard V4L2 device structure
 * @vc: standard V4L2 VIDIOC_S_CTRL ioctl structure
 *
 * If the requested control is supported, sets the control's current
 * value in HW (and updates the video_control[] array).  Otherwise,
 * returns -EINVAL if the control is not supported.
 */
static int ioctl_s_ctrl(struct v4l2_int_device *s, struct v4l2_control *vc)
{
	int retval = 0;

	pr_debug("In ov5640:ioctl_s_ctrl %d\n",
		 vc->id);

	switch (vc->id) {
	case V4L2_CID_BRIGHTNESS:
		break;
	case V4L2_CID_AUTO_FOCUS_START:
		//retval = trigger_auto_focus();
		break;
	case V4L2_CID_CONTRAST:
		break;
	case V4L2_CID_SATURATION:
		break;
	case V4L2_CID_HUE:
		break;
	case V4L2_CID_AUTO_WHITE_BALANCE:
		break;
	case V4L2_CID_DO_WHITE_BALANCE:
		break;
	case V4L2_CID_RED_BALANCE:
		break;
	case V4L2_CID_BLUE_BALANCE:
		break;
	case V4L2_CID_GAMMA:
		break;
	case V4L2_CID_EXPOSURE:
		break;
	case V4L2_CID_AUTOGAIN:
		break;
	case V4L2_CID_GAIN:
		break;
	case V4L2_CID_HFLIP:
		break;
	case V4L2_CID_VFLIP:
		break;
	default:
		retval = -EPERM;
		break;
	}

	return retval;
}

/*!
 * ioctl_g_chip_ident - V4L2 sensor interface handler for
 *			VIDIOC_DBG_G_CHIP_IDENT ioctl
 * @s: pointer to standard V4L2 device structure
 * @id: pointer to int
 *
 * Return 0.
 */
static int ioctl_g_chip_ident(struct v4l2_int_device *s, int *id)
{
	((struct v4l2_dbg_chip_ident *)id)->match.type =
					V4L2_CHIP_MATCH_I2C_DRIVER;
	strcpy(((struct v4l2_dbg_chip_ident *)id)->match.name,
		"ov5647_mipi_camera");

	return 0;
}

/*!
 * This structure defines all the ioctls for this module and links them to the
 * enumeration.
 */
static struct v4l2_int_ioctl_desc ov5647_ioctl_desc[] = {
	{vidioc_int_dev_init_num, (v4l2_int_ioctl_func *) ioctl_dev_init},
	{vidioc_int_dev_exit_num, ioctl_dev_exit},
	{vidioc_int_s_power_num, (v4l2_int_ioctl_func *) ioctl_s_power},
/*	{vidioc_int_g_ifparm_num, (v4l2_int_ioctl_func *) ioctl_g_ifparm}, */
/*	{vidioc_int_g_needs_reset_num,
				(v4l2_int_ioctl_func *)ioctl_g_needs_reset}, */
/*	{vidioc_int_reset_num, (v4l2_int_ioctl_func *)ioctl_reset}, */
	{vidioc_int_init_num, (v4l2_int_ioctl_func *) ioctl_init},
	{vidioc_int_enum_fmt_cap_num,
				(v4l2_int_ioctl_func *) ioctl_enum_fmt_cap},
/*	{vidioc_int_try_fmt_cap_num,
				(v4l2_int_ioctl_func *)ioctl_try_fmt_cap}, */
	{vidioc_int_g_fmt_cap_num, (v4l2_int_ioctl_func *) ioctl_g_fmt_cap},
/*	{vidioc_int_s_fmt_cap_num, (v4l2_int_ioctl_func *) ioctl_s_fmt_cap}, */
	{vidioc_int_g_parm_num, (v4l2_int_ioctl_func *) ioctl_g_parm},
	{vidioc_int_s_parm_num, (v4l2_int_ioctl_func *) ioctl_s_parm},
/*	{vidioc_int_queryctrl_num, (v4l2_int_ioctl_func *)ioctl_queryctrl}, */
	{vidioc_int_g_ctrl_num, (v4l2_int_ioctl_func *) ioctl_g_ctrl}, 
	{vidioc_int_s_ctrl_num, (v4l2_int_ioctl_func *) ioctl_s_ctrl},
	{vidioc_int_enum_framesizes_num,
				(v4l2_int_ioctl_func *) ioctl_enum_framesizes},
	{vidioc_int_enum_frameintervals_num,
			(v4l2_int_ioctl_func *) ioctl_enum_frameintervals},
	{vidioc_int_g_chip_ident_num,
				(v4l2_int_ioctl_func *) ioctl_g_chip_ident}, 
/*	{vidioc_int_send_command_num,
				(v4l2_int_ioctl_func *) ioctl_send_command},*/
};

static struct v4l2_int_slave ov5647_slave = {
	.ioctls = ov5647_ioctl_desc,
	.num_ioctls = ARRAY_SIZE(ov5647_ioctl_desc),
};

static struct v4l2_int_device ov5647_int_device = {
	.module = THIS_MODULE,
	.name = "ov5647_mipi",
	.type = v4l2_int_type_slave,
	.u = {
		.slave = &ov5647_slave,
	},
};

static ssize_t show_reg(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	u8 val;
	s32 rval = ov5647_read_reg(ov5647_data.last_reg, &val);
	pr_info("[%s:%s:%d] -------------------------\n", __FILE__, __func__, __LINE__);

	return sprintf(buf, "ov5647[0x%04x]=0x%02x\n",ov5647_data.last_reg, rval);
}

static ssize_t set_reg(struct device *dev,
			struct device_attribute *attr,
		       const char *buf, size_t count)
{
	int regnum, value;
	int num_parsed = sscanf(buf, "%04x=%02x", &regnum, &value);
	pr_info("[%s:%s:%d] -------------------------\n", __FILE__, __func__, __LINE__);
	if (1 <= num_parsed) {
		if (0xffff < (unsigned)regnum){
			pr_err("%s:invalid regnum %x\n", __func__, regnum);
			return 0;
		}
		ov5647_data.last_reg = regnum;
	}
	if (2 == num_parsed) {
		if (0xff < (unsigned)value) {
			pr_err("%s:invalid value %x\n", __func__, value);
			return 0;
		}
		ov5647_write_reg(ov5647_data.last_reg, value);
	}
	return count;
}

static DEVICE_ATTR(ov5647_reg, S_IRUGO|S_IWUGO, show_reg, set_reg);
// end new code

/*!
 * ov5647 I2C probe function
 *
 * @param adapter            struct i2c_adapter *
 * @return  Error code indicating success or failure
 */
static int ov5647_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct pinctrl *pinctrl;
	struct device *dev = &client->dev;
	int retval;
	u8 chip_id_high, chip_id_low;

	pr_info("[%s:%s:%d] -------------------------\n", __FILE__, __func__, __LINE__);

	/* ov5647 pinctrl */
	pinctrl = devm_pinctrl_get_select_default(dev);
	if (IS_ERR(pinctrl)) {
		dev_warn(dev, "no  pin available\n");
	}


	/* request power down pin */
	pwn_gpio = of_get_named_gpio(dev->of_node, "pwn-gpios", 0);
	if (!gpio_is_valid(pwn_gpio)) {
		dev_warn(dev, "no sensor pwdn pin available\n");
		pwn_gpio = -1;
	} else {
		retval = devm_gpio_request_one(dev, pwn_gpio, GPIOF_OUT_INIT_HIGH,
						"ov5647_mipi_pwdn");
		if (retval < 0) {
			dev_warn(dev, "Failed to set power pin\n");
			return retval;
		}
	}

	/* request reset pin */
	rst_gpio = of_get_named_gpio(dev->of_node, "rst-gpios", 0);
	if (!gpio_is_valid(rst_gpio)) {
		dev_warn(dev, "no sensor reset pin available\n");
		rst_gpio = -1;
	} else {
		retval = devm_gpio_request_one(dev, rst_gpio, GPIOF_OUT_INIT_HIGH,
						"ov5647_mipi_reset");
		if (retval < 0) {
			dev_warn(dev, "Failed to set reset pin\n");
			return retval;
		}
	}

	/* Set initial values for the sensor struct. */
	memset(&ov5647_data, 0, sizeof(ov5647_data));

	ov5647_data.mipi_camera = 1;
	ov5647_data.sensor_clk = devm_clk_get(dev, "csi_mclk");
	if (IS_ERR(ov5647_data.sensor_clk)) {
		/* assuming clock enabled by default */
		ov5647_data.sensor_clk = NULL;
		dev_err(dev, "clock-frequency missing or invalid\n");
		return PTR_ERR(ov5647_data.sensor_clk);
	}

	retval = of_property_read_u32(dev->of_node, "mclk",
					&(ov5647_data.mclk));
	if (retval) {
		dev_err(dev, "mclk missing or invalid\n");
		return retval;
	}

	retval = of_property_read_u32(dev->of_node, "mclk_source",
					(u32 *) &(ov5647_data.mclk_source));
	if (retval) {
		dev_err(dev, "mclk_source missing or invalid\n");
		return retval;
	}

	retval = of_property_read_u32(dev->of_node, "ipu_id",
					&(ov5647_data.ipu_id));
	if (retval) {
		dev_err(dev, "ipu_id missing or invalid\n");
		return retval;
	}

	retval = of_property_read_u32(dev->of_node, "csi_id",
					&(ov5647_data.csi));
	if (retval) {
		dev_err(dev, "csi id missing or invalid\n");
		return retval;
	}

	clk_prepare_enable(ov5647_data.sensor_clk);

	ov5647_data.io_init = ov5647_reset;
	ov5647_data.i2c_client = client;
	ov5647_data.pix.pixelformat = V4L2_PIX_FMT_SBGGR8;
	ov5647_data.pix.width = 640;
	ov5647_data.pix.height = 480;
	ov5647_data.streamcap.capability = V4L2_MODE_HIGHQUALITY |
					   V4L2_CAP_TIMEPERFRAME;
	ov5647_data.streamcap.capturemode = 0;
	ov5647_data.streamcap.timeperframe.denominator = DEFAULT_FPS;
	ov5647_data.streamcap.timeperframe.numerator = 1;

	ov5647_regulator_enable(&client->dev);

	ov5647_reset();

	ov5647_power_down(0);

	retval = ov5647_read_reg(OV5647_CHIP_ID_HIGH_BYTE, &chip_id_high);
	if (retval < 0 || chip_id_high != 0x56) {
		pr_warning("camera ov5647_mipi is not found\n");
		clk_disable_unprepare(ov5647_data.sensor_clk);
		return -ENODEV;
	}
	retval = ov5647_read_reg(OV5647_CHIP_ID_LOW_BYTE, &chip_id_low);
	if (retval < 0 || chip_id_low != 0x47) {
		pr_warning("camera ov5647_mipi is not found\n");
		clk_disable_unprepare(ov5647_data.sensor_clk);
		return -ENODEV;
	}

	// start new code
	ov5647_data.virtual_channel = ov5647_data.csi | (ov5647_data.ipu_id << 1);	// new code

	ov5647_stream_off();

	//ov5647_power_down(1);

	ov5647_int_device.priv = &ov5647_data;
	retval = v4l2_int_device_register(&ov5647_int_device);

	if (device_create_file(dev, &dev_attr_ov5647_reg))
		dev_err(dev, "%s: error creating ov5647_reg entry\n", __func__);
	// end new code

	pr_info("camera ov5647_mipi is found\n");
	
	return retval;
}

/*!
 * ov5647 I2C detach function
 *
 * @param client            struct i2c_client *
 * @return  Error code indicating success or failure
 */
static int ov5647_remove(struct i2c_client *client)
{
	//struct v4l2_subdev *sd = i2c_get_clientdata(client);

	pr_info("[%s:%s:%d] -------------------------\n", __FILE__, __func__, __LINE__);

	v4l2_int_device_unregister(&ov5647_int_device);	// new code
	
	//v4l2_async_unregister_subdev(sd);

	clk_disable_unprepare(ov5647_data.sensor_clk);

	ov5647_power_down(1);

	if (gpo_regulator)
		regulator_disable(gpo_regulator);

	if (analog_regulator)
		regulator_disable(analog_regulator);

	if (core_regulator)
		regulator_disable(core_regulator);

	if (io_regulator)
		regulator_disable(io_regulator);

	return 0;
}

/*!
 * ov5640 init function
 * Called by insmod ov5640_camera.ko.
 *
 * @return  Error code indicating success or failure
 */
static __init int ov5647_init(void)
{
	u8 err;

	err = i2c_add_driver(&ov5647_i2c_driver);
	if (err != 0)
		pr_err("%s:driver registration failed, error=%d\n",
			__func__, err);

	return err;
}

/*!
 * OV5640 cleanup function
 * Called on rmmod ov5647_camera.ko
 *
 * @return  Error code indicating success or failure
 */
static void __exit ov5647_clean(void)
{
	i2c_del_driver(&ov5647_i2c_driver);
}

module_init(ov5647_init);
module_exit(ov5647_clean);
//module_i2c_driver(ov5647_i2c_driver);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("OV5647 MIPI Camera Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");
MODULE_ALIAS("CSI");
