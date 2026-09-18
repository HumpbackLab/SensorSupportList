// SPDX-License-Identifier: GPL-2.0-only
/*
 * OV9281 CVITEK sensor adapter. Register initialization values are adapted
 * from ArduCAM's GPL-2.0 OV9281 driver:
 * https://github.com/ArduCAM/ov9281_driver/blob/main/ov9281.c
 * Lane-specific RAW10 values follow ArduCAM/ArduCAM_USB_Camera_Shield,
 * Config/USB3.0_UC-593 Rev.C/MIPI/OV9281/Channel_A/{1,2}Lane configs.
 * See the TideSense board's OV9281-lanes.md for source links and validation.
 */
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <syslog.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

#include <linux/cvi_comm_video.h>
#include "cvi_sns_ctrl.h"
#include "ov9281_cmos_ex.h"

static CVI_S32 ov9281_linear_60_init(VI_PIPE ViPipe);

const CVI_U32 ov9281_addr_byte = 2;
const CVI_U32 ov9281_data_byte = 1;
static int g_fd[VI_MAX_PIPE_NUM] = {[0 ... (VI_MAX_PIPE_NUM - 1)] = -1};

int ov9281_i2c_init(VI_PIPE ViPipe)
{
	char acDevFile[16] = {0};
	CVI_U8 u8DevNum;

	if (g_fd[ViPipe] >= 0)
		return CVI_SUCCESS;
	int ret;

	u8DevNum = g_aunOv9281_BusInfo[ViPipe].s8I2cDev;
	snprintf(acDevFile, sizeof(acDevFile),  "/dev/i2c-%u", u8DevNum);

	g_fd[ViPipe] = open(acDevFile, O_RDWR, 0600);

	if (g_fd[ViPipe] < 0) {
		CVI_TRACE_SNS(CVI_DBG_ERR, "Open /dev/i2c-%u error!\n", u8DevNum);
		return CVI_FAILURE;
	}

	ret = ioctl(g_fd[ViPipe], I2C_SLAVE_FORCE, ov9281_i2c_addr[ViPipe]);
	if (ret < 0) {
		CVI_TRACE_SNS(CVI_DBG_ERR, "I2C_SLAVE_FORCE error!\n");
		close(g_fd[ViPipe]);
		g_fd[ViPipe] = -1;
		return ret;
	}

	return CVI_SUCCESS;
}

int ov9281_i2c_exit(VI_PIPE ViPipe)
{
	if (g_fd[ViPipe] >= 0) {
		close(g_fd[ViPipe]);
		g_fd[ViPipe] = -1;
		return CVI_SUCCESS;
	}
	return CVI_FAILURE;
}

int ov9281_read_register(VI_PIPE ViPipe, int addr)
{
	int ret, data;
	CVI_U8 buf[8];
	CVI_U8 idx = 0;

	if (g_fd[ViPipe] < 0)
		return CVI_FAILURE;

	if (ov9281_addr_byte == 2)
		buf[idx++] = (addr >> 8) & 0xff;

	// add address byte 0
	buf[idx++] = addr & 0xff;

	ret = write(g_fd[ViPipe], buf, ov9281_addr_byte);
	if (ret != (int)ov9281_addr_byte) {
		CVI_TRACE_SNS(CVI_DBG_ERR, "I2C_WRITE error!\n");
		return -EIO;
	}

	buf[0] = 0;
	buf[1] = 0;
	ret = read(g_fd[ViPipe], buf, ov9281_data_byte);
	if (ret != (int)ov9281_data_byte) {
		CVI_TRACE_SNS(CVI_DBG_ERR, "I2C_READ error!\n");
		return -EIO;
	}

	// pack read back data
	data = 0;
	if (ov9281_data_byte == 2) {
		data = buf[0] << 8;
		data += buf[1];
	} else {
		data = buf[0];
	}

	syslog(LOG_DEBUG, "i2c r 0x%x = 0x%x\n", addr, data);
	return data;

}

int ov9281_write_register(VI_PIPE ViPipe, int addr, int data)
{
	CVI_U8 idx = 0;
	int ret;
	CVI_U8 buf[8];

	if (g_fd[ViPipe] < 0)
		return CVI_FAILURE;

	if (ov9281_addr_byte == 2) {
		buf[idx] = (addr >> 8) & 0xff;
		idx++;
		buf[idx] = addr & 0xff;
		idx++;
	}

	if (ov9281_data_byte == 1) {
		buf[idx] = data & 0xff;
		idx++;
	}

	ret = write(g_fd[ViPipe], buf, ov9281_addr_byte + ov9281_data_byte);
	if (ret != (int)(ov9281_addr_byte + ov9281_data_byte)) {
		CVI_TRACE_SNS(CVI_DBG_ERR, "I2C_WRITE error!\n");
		return CVI_FAILURE;
	}
	syslog(LOG_DEBUG, "i2c w 0x%x 0x%x\n", addr, data);
	return CVI_SUCCESS;
}

static void delay_ms(int ms)
{
	usleep(ms * 1000);
}

void ov9281_standby(VI_PIPE ViPipe)
{
	ov9281_write_register(ViPipe, 0x0100, 0x00); /* standby */
}

void ov9281_restart(VI_PIPE ViPipe)
{
	ov9281_write_register(ViPipe, 0x0100, 0x00);
	delay_ms(20);
	ov9281_write_register(ViPipe, 0x0100, 0x01); /* restart */
}

void ov9281_default_reg_init(VI_PIPE ViPipe)
{
	CVI_U32 i;

	for (i = 0; i < g_pastOv9281[ViPipe]->astSyncInfo[0].snsCfg.u32RegNum; i++) {
		if (g_pastOv9281[ViPipe]->astSyncInfo[0].snsCfg.astI2cData[i].bUpdate == CVI_TRUE) {
			ov9281_write_register(ViPipe,
				g_pastOv9281[ViPipe]->astSyncInfo[0].snsCfg.astI2cData[i].u32RegAddr,
				g_pastOv9281[ViPipe]->astSyncInfo[0].snsCfg.astI2cData[i].u32Data);
		}
	}
}

void ov9281_mirror_flip(VI_PIPE ViPipe, ISP_SNS_MIRRORFLIP_TYPE_E eSnsMirrorFlip)
{
	CVI_U8 flip, mirror;

	flip = ov9281_read_register(ViPipe, 0x3820);
	mirror = ov9281_read_register(ViPipe, 0x3821);

	flip &= ~(0x1 << 2);
	mirror &= ~(0x1 << 2);

	switch (eSnsMirrorFlip) {
	case ISP_SNS_NORMAL:
		break;
	case ISP_SNS_MIRROR:
		mirror |= 0x1 << 2;
		break;
	case ISP_SNS_FLIP:
		flip |= 0x1 << 2;
		break;
	case ISP_SNS_MIRROR_FLIP:
		flip |= 0x1 << 2;
		mirror |= 0x1 << 2;
		break;
	default:
		return;
	}

	ov9281_write_register(ViPipe, 0x3820, flip);
	ov9281_write_register(ViPipe, 0x3821, mirror);
}

#define OV9281_CHIP_ID_ADDR_H		0x300A
#define OV9281_CHIP_ID_ADDR_L		0x300B
#define OV9281_CHIP_ID			0x9281

int ov9281_probe(VI_PIPE ViPipe)
{
	int nVal, nVal2;

	if (ov9281_i2c_init(ViPipe) != CVI_SUCCESS)
		return CVI_FAILURE;

	delay_ms(5);

	nVal  = ov9281_read_register(ViPipe, OV9281_CHIP_ID_ADDR_H);
	nVal2 = ov9281_read_register(ViPipe, OV9281_CHIP_ID_ADDR_L);
	if (nVal < 0 || nVal2 < 0) {
		CVI_TRACE_SNS(CVI_DBG_ERR, "read sensor id error.\n");
		return CVI_FAILURE;
	}

	if ((((nVal & 0xFF) << 8) | ((nVal2 & 0xFF) << 0)) != OV9281_CHIP_ID) {
		CVI_TRACE_SNS(CVI_DBG_ERR, "Sensor ID Mismatch! Use the wrong sensor??\n");
		return CVI_FAILURE;
	}

	return CVI_SUCCESS;
}


void ov9281_init(VI_PIPE ViPipe)
{
	if (ov9281_i2c_init(ViPipe) != CVI_SUCCESS || g_pastOv9281[ViPipe] == CVI_NULL)
		return;

	if (ov9281_linear_60_init(ViPipe) == CVI_SUCCESS)
		g_pastOv9281[ViPipe]->bInit = CVI_TRUE;
}

void ov9281_exit(VI_PIPE ViPipe)
{
	/* Stop CSI before closing I2C so a later mode change can reconfigure RX. */
	ov9281_standby(ViPipe);
	ov9281_i2c_exit(ViPipe);
	if (g_pastOv9281[ViPipe] != CVI_NULL)
		g_pastOv9281[ViPipe]->bInit = CVI_FALSE;
}

/* Register values follow the 1280x800 RAW10 mode in the cited driver. */
struct ov9281_regval {
	CVI_U16 addr;
	CVI_U8 data;
};

#define OV9281_REG_END 0xffff

static const struct ov9281_regval ov9281_common_regs[] = {
	{0x0302, 0x32}, {0x030e, 0x02}, {0x3001, 0x00},
	{0x3004, 0x00}, {0x3005, 0x00}, {0x3006, 0x04},
	{0x3011, 0x0a}, {0x3013, 0x18}, {0x3022, 0x01},
	{0x3023, 0x00}, {0x302c, 0x00}, {0x302f, 0x00},
	{0x3030, 0x04}, {0x303a, 0x00},
	{0x303f, 0x01}, {0x3500, 0x00}, {0x3501, 0x2a},
	{0x3502, 0x90}, {0x3503, 0x08}, {0x3505, 0x8c},
	{0x3507, 0x03}, {0x3508, 0x00}, {0x3509, 0x10},
	{0x3610, 0x80}, {0x3611, 0xa0}, {0x3620, 0x6f},
	{0x3632, 0x56}, {0x3633, 0x78}, {0x3666, 0x00},
	{0x366f, 0x5a}, {0x3680, 0x84}, {0x3712, 0x80},
	{0x372d, 0x22}, {0x3731, 0x80}, {0x3732, 0x30},
	{0x377d, 0x22}, {0x3788, 0x02}, {0x3789, 0xa4},
	{0x378a, 0x00}, {0x378b, 0x4a}, {0x3799, 0x20},
	{0x3881, 0x42}, {0x38b1, 0x00}, {0x3920, 0xff},
	{0x4010, 0x40}, {0x4043, 0x40}, {0x4307, 0x30},
	{0x4317, 0x00}, {0x4501, 0x00}, {0x450a, 0x08},
	{0x4601, 0x04}, {0x470f, 0x00}, {0x4f07, 0x00},
	{0x4800, 0x00}, {0x5000, 0x9f}, {0x5001, 0x00},
	{0x5e00, 0x00}, {0x5d00, 0x07}, {0x5d01, 0x00},
	{OV9281_REG_END, 0x00},
};

static const struct ov9281_regval ov9281_1280x800_regs[] = {
	{0x3778, 0x00}, {0x3800, 0x00}, {0x3801, 0x00},
	{0x3802, 0x00}, {0x3803, 0x00}, {0x3804, 0x05},
	{0x3805, 0x0f}, {0x3806, 0x03}, {0x3807, 0x2f},
	{0x3808, 0x05}, {0x3809, 0x00}, {0x380a, 0x03},
	{0x380b, 0x20}, {0x380c, 0x05}, {0x380d, 0xb0},
	{0x380e, 0x03}, {0x380f, 0x8e}, {0x3810, 0x00},
	{0x3811, 0x08}, {0x3812, 0x00}, {0x3813, 0x08},
	{0x3814, 0x11}, {0x3815, 0x11}, {0x3820, 0x40},
	{0x3821, 0x00}, {0x4003, 0x40}, {0x4008, 0x04},
	{0x4009, 0x0b}, {0x400c, 0x00}, {0x400d, 0x07},
	{0x4507, 0x00}, {0x4509, 0x00}, {OV9281_REG_END, 0x00},
};

/* Native centered crop: (320,160), no binning; retain 1456/910 timing.
 * OV9281 datasheet sections 4.2 and 7.8; eight border pixels per side. */
static const struct ov9281_regval ov9281_640x480_regs[] = {
	{0x3778, 0x00}, {0x3800, 0x01}, {0x3801, 0x40},
	{0x3802, 0x00}, {0x3803, 0xa0}, {0x3804, 0x03},
	{0x3805, 0xcf}, {0x3806, 0x02}, {0x3807, 0x8f},
	{0x3808, 0x02}, {0x3809, 0x80}, {0x380a, 0x01},
	{0x380b, 0xe0}, {0x380c, 0x05}, {0x380d, 0xb0},
	{0x380e, 0x03}, {0x380f, 0x8e}, {0x3810, 0x00},
	{0x3811, 0x08}, {0x3812, 0x00}, {0x3813, 0x08},
	{0x3814, 0x11}, {0x3815, 0x11}, {0x3820, 0x40},
	{0x3821, 0x00}, {0x4003, 0x40}, {0x4008, 0x04},
	{0x4009, 0x0b}, {0x400c, 0x00}, {0x400d, 0x07},
	{0x4507, 0x00}, {0x4509, 0x00}, {OV9281_REG_END, 0x00},
};

/* Full-field sub-sampling from the cited ArduCAM driver, with the same
 * 60 fps HTS/VTS as the other modes for both one- and two-lane boards. */
static const struct ov9281_regval ov9281_640x400_regs[] = {
	{0x3778, 0x10},
	{0x3800, 0x00},
	{0x3801, 0x00},
	{0x3802, 0x00},
	{0x3803, 0x00},
	{0x3804, 0x05},
	{0x3805, 0x0f},
	{0x3806, 0x03},
	{0x3807, 0x2f},
	{0x3808, 0x02},
	{0x3809, 0x80},
	{0x380a, 0x01},
	{0x380b, 0x90},
	{0x380c, 0x05},
	{0x380d, 0xb0},
	{0x380e, 0x03},
	{0x380f, 0x8e},
	{0x3810, 0x00},
	{0x3811, 0x04},
	{0x3812, 0x00},
	{0x3813, 0x04},
	{0x3814, 0x31},
	{0x3815, 0x22},
	{0x3820, 0x60},
	{0x3821, 0x01},
	{0x4008, 0x02},
	{0x4009, 0x05},
	{0x400c, 0x00},
	{0x400d, 0x03},
	{0x4507, 0x03},
	{0x4509, 0x80},
	{OV9281_REG_END, 0x00},
};

static const struct ov9281_regval ov9281_raw10_regs[] = {
	{0x030d, 0x50}, {OV9281_REG_END, 0x00},
};

static int ov9281_write_array(VI_PIPE ViPipe, const struct ov9281_regval *regs)
{
	CVI_U32 i;

	for (i = 0; regs[i].addr != OV9281_REG_END; i++) {
		if (ov9281_write_register(ViPipe, regs[i].addr, regs[i].data) != CVI_SUCCESS)
			return CVI_FAILURE;
	}
	return CVI_SUCCESS;
}

static CVI_S32 ov9281_linear_60_init(VI_PIPE ViPipe)
{
	int lanes = ov9281_get_lane_num(ViPipe);
	int raw10_ctrl;
	CVI_U8 mode = g_pastOv9281[ViPipe]->u8ImgMode;
	const struct ov9281_regval *mode_regs;

	if (mode == OV9281_MODE_640X400P60)
		mode_regs = ov9281_640x400_regs;
	else if (mode == OV9281_MODE_640X480P60)
		mode_regs = ov9281_640x480_regs;
	else if (mode == OV9281_MODE_1280X800P60)
		mode_regs = ov9281_1280x800_regs;
	else
		return CVI_FAILURE;

	if (lanes != 1 && lanes != 2) {
		CVI_TRACE_SNS(CVI_DBG_ERR, "OV9281 unsupported lane count %d on pipe %d\n", lanes, ViPipe);
		return CVI_FAILURE;
	}
	/* ArduCAM USB Camera Shield OV9281 Channel_A RAW10 configs:
	 * 0x3662 = 0x01 for 1 lane, 0x05 for 2 lanes; 0x3039 stays 0x32.
	 * Changing 0x3039 alone produces invalid CSI packets on this sensor.
	 */
	raw10_ctrl = (lanes == 1) ? 0x01 : 0x05;
	if (ov9281_write_register(ViPipe, 0x0103, 0x01) != CVI_SUCCESS)
		return CVI_FAILURE;
	delay_ms(5);
	if (ov9281_write_register(ViPipe, 0x0100, 0x00) != CVI_SUCCESS ||
	    ov9281_write_array(ViPipe, ov9281_common_regs) != CVI_SUCCESS ||
	    ov9281_write_array(ViPipe, mode_regs) != CVI_SUCCESS ||
	    ov9281_write_array(ViPipe, ov9281_raw10_regs) != CVI_SUCCESS) {
		CVI_TRACE_SNS(CVI_DBG_ERR, "OV9281 register initialization failed on pipe %d\n", ViPipe);
		return CVI_FAILURE;
	}
	/* Match the transmitter to this pipe's receiver before starting the stream. */
	if (ov9281_write_register(ViPipe, 0x3039, 0x32) != CVI_SUCCESS ||
	    ov9281_write_register(ViPipe, 0x3662, raw10_ctrl) != CVI_SUCCESS ||
	    ov9281_read_register(ViPipe, 0x3039) != 0x32 ||
	    ov9281_read_register(ViPipe, 0x3662) != raw10_ctrl) {
		CVI_TRACE_SNS(CVI_DBG_ERR, "OV9281 MIPI lane configuration failed on pipe %d\n", ViPipe);
		return CVI_FAILURE;
	}
	/* 0x3823 enables external VSYNC input when hw_sync is set by CVITEK. */
	if (ov9281_write_register(ViPipe, 0x3823,
		g_au16Ov9281_HwSync[ViPipe] ? 0x30 : 0x00) != CVI_SUCCESS)
		return CVI_FAILURE;
	if (g_au16Ov9281_HwSync[ViPipe] &&
	    (ov9281_write_register(ViPipe, 0x3824, 0x00) != CVI_SUCCESS ||
	     ov9281_write_register(ViPipe, 0x3825, 0x00) != CVI_SUCCESS ||
	     ov9281_write_register(ViPipe, 0x3826, 0x00) != CVI_SUCCESS ||
	     ov9281_write_register(ViPipe, 0x3827, 0x00) != CVI_SUCCESS))
		return CVI_FAILURE;

	/* Apply ISP-managed exposure, gain, and 60 fps VTS before streaming. */
	ov9281_default_reg_init(ViPipe);
	if (ov9281_write_register(ViPipe, 0x0100, 0x01) != CVI_SUCCESS)
		return CVI_FAILURE;
	delay_ms(100);
	printf("ViPipe:%d, OV9281 addr=0x%02x %d-lane %s RAW10 60fps init OK\n",
		ViPipe, ov9281_i2c_addr[ViPipe], lanes,
		mode == OV9281_MODE_640X400P60 ? "640x400" :
		mode == OV9281_MODE_640X480P60 ? "640x480" : "1280x800");
	return CVI_SUCCESS;
}
