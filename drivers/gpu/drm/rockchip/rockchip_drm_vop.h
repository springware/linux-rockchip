/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author:Mark Yao <mark.yao@rock-chips.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _ROCKCHIP_DRM_VOP_H
#define RK3288__ROCKCHIP_DRM_VOP_H

/* register definition */
#define RK3066_SYS_CTRL0			0x0000
#define RK3066_SYS_CTRL1			0x0004
#define RK3066_DSP_CTRL0			0x0008
#define RK3066_DSP_CTRL1			0x000c
#define RK3066_INT_STATUS			0x0010
#define RK3066_WIN0_YRGB_MST0			0x0028
#define RK3066_WIN0_CBR_MST0			0x002c
#define RK3066_WIN0_VIR				0x0038
#define RK3066_WIN0_ACT_INFO			0x003c
#define RK3066_WIN0_DSP_INFO			0x0040
#define RK3066_WIN0_DSP_ST			0x0044
#define RK3066_WIN1_YRGB_MST			0x0054
#define RK3066_WIN1_CBR_MST			0x0058
#define RK3066_WIN1_VIR				0x005c
#define RK3066_WIN1_ACT_INFO			0x0060
#define RK3066_WIN1_DSP_INFO			0x0064
#define RK3066_WIN1_DSP_ST			0x0068
#define RK3066_WIN2_MST				0x0078
#define RK3066_WIN2_VIR				0x007c
#define RK3066_WIN2_DSP_INFO			0x0080
#define RK3066_WIN2_DSP_ST			0x0084
#define RK3066_DSP_HTOTAL_HS_END		0x009c
#define RK3066_DSP_HACT_ST_END			0x00a0
#define RK3066_DSP_VTOTAL_VS_END		0x00a4
#define RK3066_DSP_VACT_ST_END			0x00a8
#define RK3066_HWC_MST				0x0088
#define RK3066_HWC_DSP_ST			0x008c
#define RK3066_REG_LOAD_EN			0x00c0

#define RK3188_SYS_CTRL				0x0000
#define RK3188_DSP_CTRL0			0x0004
#define RK3188_DSP_CTRL1			0x0008
#define RK3188_INT_STATUS			0x0010
#define RK3188_ALPHA_CTRL			0x0014
#define RK3188_WIN0_YRGB_MST0			0x0020
#define RK3188_WIN0_CBR_MST0			0x0024
#define RK3188_WIN_VIR				0x0030
#define RK3188_WIN0_ACT_INFO			0x0034
#define RK3188_WIN0_DSP_INFO			0x0038
#define RK3188_WIN0_DSP_ST			0x003c
#define RK3188_WIN1_MST				0x004c
#define RK3188_WIN1_DSP_INFO			0x0050
#define RK3188_WIN1_DSP_ST			0x0054
#define RK3188_HWC_MST				0x0058
#define RK3188_HWC_DSP_ST			0x005c
#define RK3188_DSP_HTOTAL_HS_END		0x006c
#define RK3188_DSP_HACT_ST_END			0x0070
#define RK3188_DSP_VTOTAL_VS_END		0x0074
#define RK3188_DSP_VACT_ST_END			0x0078
#define RK3188_REG_CFG_DONE			0x0090

#define RK3288_REG_CFG_DONE			0x0000
#define RK3288_VERSION_INFO			0x0004
#define RK3288_SYS_CTRL				0x0008
#define RK3288_SYS_CTRL1			0x000c
#define RK3288_DSP_CTRL0			0x0010
#define RK3288_DSP_CTRL1			0x0014
#define RK3288_DSP_BG				0x0018
#define RK3288_MCU_CTRL				0x001c
#define RK3288_INTR_CTRL0			0x0020
#define RK3288_INTR_CTRL1			0x0024
#define RK3288_WIN0_CTRL0			0x0030
#define RK3288_WIN0_CTRL1			0x0034
#define RK3288_WIN0_COLOR_KEY			0x0038
#define RK3288_WIN0_VIR				0x003c
#define RK3288_WIN0_YRGB_MST			0x0040
#define RK3288_WIN0_CBR_MST			0x0044
#define RK3288_WIN0_ACT_INFO			0x0048
#define RK3288_WIN0_DSP_INFO			0x004c
#define RK3288_WIN0_DSP_ST			0x0050
#define RK3288_WIN0_SCL_FACTOR_YRGB		0x0054
#define RK3288_WIN0_SCL_FACTOR_CBR		0x0058
#define RK3288_WIN0_SCL_OFFSET			0x005c
#define RK3288_WIN0_SRC_ALPHA_CTRL		0x0060
#define RK3288_WIN0_DST_ALPHA_CTRL		0x0064
#define RK3288_WIN0_FADING_CTRL			0x0068
/* win1 register */
#define RK3288_WIN1_CTRL0			0x0070
#define RK3288_WIN1_CTRL1			0x0074
#define RK3288_WIN1_COLOR_KEY			0x0078
#define RK3288_WIN1_VIR				0x007c
#define RK3288_WIN1_YRGB_MST			0x0080
#define RK3288_WIN1_CBR_MST			0x0084
#define RK3288_WIN1_ACT_INFO			0x0088
#define RK3288_WIN1_DSP_INFO			0x008c
#define RK3288_WIN1_DSP_ST			0x0090
#define RK3288_WIN1_SCL_FACTOR_YRGB		0x0094
#define RK3288_WIN1_SCL_FACTOR_CBR		0x0098
#define RK3288_WIN1_SCL_OFFSET			0x009c
#define RK3288_WIN1_SRC_ALPHA_CTRL		0x00a0
#define RK3288_WIN1_DST_ALPHA_CTRL		0x00a4
#define RK3288_WIN1_FADING_CTRL			0x00a8
/* win2 register */
#define RK3288_WIN2_CTRL0			0x00b0
#define RK3288_WIN2_CTRL1			0x00b4
#define RK3288_WIN2_VIR0_1			0x00b8
#define RK3288_WIN2_VIR2_3			0x00bc
#define RK3288_WIN2_MST0			0x00c0
#define RK3288_WIN2_DSP_INFO0			0x00c4
#define RK3288_WIN2_DSP_ST0			0x00c8
#define RK3288_WIN2_COLOR_KEY			0x00cc
#define RK3288_WIN2_MST1			0x00d0
#define RK3288_WIN2_DSP_INFO1			0x00d4
#define RK3288_WIN2_DSP_ST1			0x00d8
#define RK3288_WIN2_SRC_ALPHA_CTRL		0x00dc
#define RK3288_WIN2_MST2			0x00e0
#define RK3288_WIN2_DSP_INFO2			0x00e4
#define RK3288_WIN2_DSP_ST2			0x00e8
#define RK3288_WIN2_DST_ALPHA_CTRL		0x00ec
#define RK3288_WIN2_MST3			0x00f0
#define RK3288_WIN2_DSP_INFO3			0x00f4
#define RK3288_WIN2_DSP_ST3			0x00f8
#define RK3288_WIN2_FADING_CTRL			0x00fc
/* win3 register */
#define RK3288_WIN3_CTRL0			0x0100
#define RK3288_WIN3_CTRL1			0x0104
#define RK3288_WIN3_VIR0_1			0x0108
#define RK3288_WIN3_VIR2_3			0x010c
#define RK3288_WIN3_MST0			0x0110
#define RK3288_WIN3_DSP_INFO0			0x0114
#define RK3288_WIN3_DSP_ST0			0x0118
#define RK3288_WIN3_COLOR_KEY			0x011c
#define RK3288_WIN3_MST1			0x0120
#define RK3288_WIN3_DSP_INFO1			0x0124
#define RK3288_WIN3_DSP_ST1			0x0128
#define RK3288_WIN3_SRC_ALPHA_CTRL		0x012c
#define RK3288_WIN3_MST2			0x0130
#define RK3288_WIN3_DSP_INFO2			0x0134
#define RK3288_WIN3_DSP_ST2			0x0138
#define RK3288_WIN3_DST_ALPHA_CTRL		0x013c
#define RK3288_WIN3_MST3			0x0140
#define RK3288_WIN3_DSP_INFO3			0x0144
#define RK3288_WIN3_DSP_ST3			0x0148
#define RK3288_WIN3_FADING_CTRL			0x014c
/* hwc register */
#define RK3288_HWC_CTRL0			0x0150
#define RK3288_HWC_CTRL1			0x0154
#define RK3288_HWC_MST				0x0158
#define RK3288_HWC_DSP_ST			0x015c
#define RK3288_HWC_SRC_ALPHA_CTRL		0x0160
#define RK3288_HWC_DST_ALPHA_CTRL		0x0164
#define RK3288_HWC_FADING_CTRL			0x0168
/* post process register */
#define RK3288_POST_DSP_HACT_INFO		0x0170
#define RK3288_POST_DSP_VACT_INFO		0x0174
#define RK3288_POST_SCL_FACTOR_YRGB		0x0178
#define RK3288_POST_SCL_CTRL			0x0180
#define RK3288_POST_DSP_VACT_INFO_F1		0x0184
#define RK3288_DSP_HTOTAL_HS_END		0x0188
#define RK3288_DSP_HACT_ST_END			0x018c
#define RK3288_DSP_VTOTAL_VS_END		0x0190
#define RK3288_DSP_VACT_ST_END			0x0194
#define RK3288_DSP_VS_ST_END_F1			0x0198
#define RK3288_DSP_VACT_ST_END_F1		0x019c
/* register definition end */

/* interrupt define */
#define RK3288_DSP_HOLD_VALID_INTR		(1 << 0)
#define FS_INTR					(1 << 1)
#define LINE_FLAG_INTR				(1 << 2)
#define BUS_ERROR_INTR				(1 << 3)

#define RK3288_DSP_HOLD_VALID_INTR_EN(x)	((x) << 4)
#define FS_INTR_EN(x)				((x) << 5)
#define LINE_FLAG_INTR_EN(x)			((x) << 6)
#define BUS_ERROR_INTR_EN(x)			((x) << 7)
#define RK3288_DSP_HOLD_VALID_INTR_MASK		(1 << 4)
#define FS_INTR_MASK				(1 << 5)
#define LINE_FLAG_INTR_MASK			(1 << 6)
#define BUS_ERROR_INTR_MASK			(1 << 7)

#define RK3288_DSP_HOLD_VALID_INTR_CLR		(1 << 8)
#define FS_INTR_CLR				(1 << 9)
#define LINE_FLAG_INTR_CLR			(1 << 10)
#define BUS_ERROR_INTR_CLR			(1 << 11)
#define RK3066_DSP_LINE_NUM(x)			(((x) & 0xfff) << 12)
#define RK3066_DSP_LINE_NUM_MASK		(0xfff << 12)
#define RK3288_DSP_LINE_NUM(x)			(((x) & 0x1fff) << 12)
#define RK3288_DSP_LINE_NUM_MASK		(0x1fff << 12)

/* src alpha ctrl define */
#define RK3288_SRC_FADING_VALUE(x)		(((x) & 0xff) << 24)
#define RK3288_SRC_GLOBAL_ALPHA(x)		(((x) & 0xff) << 16)
#define RK3288_SRC_FACTOR_M0(x)			(((x) & 0x7) << 6)
#define RK3288_SRC_ALPHA_CAL_M0(x)		(((x) & 0x1) << 5)
#define RK3288_SRC_BLEND_M0(x)			(((x) & 0x3) << 3)
#define RK3288_SRC_ALPHA_M0(x)			(((x) & 0x1) << 2)
#define RK3288_SRC_COLOR_M0(x)			(((x) & 0x1) << 1)
#define RK3288_SRC_ALPHA_EN(x)			(((x) & 0x1) << 0)
/* dst alpha ctrl define */
#define RK3288_DST_FACTOR_M0(x)			(((x) & 0x7) << 6)

/*
 * display output interface supported by rockchip lcdc
 */
#define RK3288_ROCKCHIP_OUT_MODE_P888	0
#define RK3288_ROCKCHIP_OUT_MODE_P666	1
#define RK3288_ROCKCHIP_OUT_MODE_P565	2
/* for use special outface */
#define RK3288_ROCKCHIP_OUT_MODE_AAAA	15

enum alpha_mode {
	ALPHA_STRAIGHT,
	ALPHA_INVERSE,
};

enum global_blend_mode {
	ALPHA_GLOBAL,
	ALPHA_PER_PIX,
	ALPHA_PER_PIX_GLOBAL,
};

enum alpha_cal_mode {
	ALPHA_SATURATION,
	ALPHA_NO_SATURATION,
};

enum color_mode {
	ALPHA_SRC_PRE_MUL,
	ALPHA_SRC_NO_PRE_MUL,
};

enum factor_mode {
	ALPHA_ZERO,
	ALPHA_ONE,
	ALPHA_SRC,
	ALPHA_SRC_INVERSE,
	ALPHA_SRC_GLOBAL,
};

#endif /* _ROCKCHIP_DRM_VOP_H */
