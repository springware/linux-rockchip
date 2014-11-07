/*
 * Copyright (C) 2012 Texas Instruments
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/component.h>
#include <linux/hdmi.h>
#include <linux/module.h>
#include <linux/irq.h>
#include <sound/asoundef.h>

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_encoder_slave.h>
#include <drm/drm_edid.h>
#include <drm/i2c/tda998x.h>

#define DBG(fmt, ...) DRM_DEBUG(fmt"\n", ##__VA_ARGS__)

struct rk1000_tve {
	struct rk1000 *base;
	struct drm_encoder encoder;
	struct drm_connector connector;


/*
	struct i2c_client *cec;
	struct i2c_client *hdmi;
	uint16_t rev;
	uint8_t current_page;
	int dpms;
	bool is_hdmi_sink;
	u8 vip_cntrl_0;
	u8 vip_cntrl_1;
	u8 vip_cntrl_2;
	struct tda998x_encoder_params params;

	wait_queue_head_t wq_edid;
	volatile int wq_edid_wait;
	struct drm_encoder *encoder;*/
};

//#define to_tda998x_priv(x)  ((struct tda998x_priv *)to_encoder_slave(x)->slave_priv)


static void
tda998x_reset(struct tda998x_priv *priv)
{
	/* reset audio and i2c master: */
	reg_write(priv, REG_SOFTRESET, SOFTRESET_AUDIO | SOFTRESET_I2C_MASTER);
	msleep(50);
	reg_write(priv, REG_SOFTRESET, 0);
	msleep(50);

	/* reset transmitter: */
	reg_set(priv, REG_MAIN_CNTRL0, MAIN_CNTRL0_SR);
	reg_clear(priv, REG_MAIN_CNTRL0, MAIN_CNTRL0_SR);

	/* PLL registers common configuration */
	reg_write(priv, REG_PLL_SERIAL_1, 0x00);
	reg_write(priv, REG_PLL_SERIAL_2, PLL_SERIAL_2_SRL_NOSC(1));
	reg_write(priv, REG_PLL_SERIAL_3, 0x00);
	reg_write(priv, REG_SERIALIZER,   0x00);
	reg_write(priv, REG_BUFFER_OUT,   0x00);
	reg_write(priv, REG_PLL_SCG1,     0x00);
	reg_write(priv, REG_AUDIO_DIV,    AUDIO_DIV_SERCLK_8);
	reg_write(priv, REG_SEL_CLK,      SEL_CLK_SEL_CLK1 | SEL_CLK_ENA_SC_CLK);
	reg_write(priv, REG_PLL_SCGN1,    0xfa);
	reg_write(priv, REG_PLL_SCGN2,    0x00);
	reg_write(priv, REG_PLL_SCGR1,    0x5b);
	reg_write(priv, REG_PLL_SCGR2,    0x00);
	reg_write(priv, REG_PLL_SCG2,     0x10);

	/* Write the default value MUX register */
	reg_write(priv, REG_MUX_VP_VIP_OUT, 0x24);
}

/*
 * only 2 interrupts may occur: screen plug/unplug and EDID read
 */
static irqreturn_t tda998x_irq_thread(int irq, void *data)
{
	struct tda998x_priv *priv = data;
	u8 sta, cec, lvl, flag0, flag1, flag2;

	if (!priv)
		return IRQ_HANDLED;
	sta = cec_read(priv, REG_CEC_INTSTATUS);
	cec = cec_read(priv, REG_CEC_RXSHPDINT);
	lvl = cec_read(priv, REG_CEC_RXSHPDLEV);
	flag0 = reg_read(priv, REG_INT_FLAGS_0);
	flag1 = reg_read(priv, REG_INT_FLAGS_1);
	flag2 = reg_read(priv, REG_INT_FLAGS_2);
	DRM_DEBUG_DRIVER(
		"tda irq sta %02x cec %02x lvl %02x f0 %02x f1 %02x f2 %02x\n",
		sta, cec, lvl, flag0, flag1, flag2);
	if ((flag2 & INT_FLAGS_2_EDID_BLK_RD) && priv->wq_edid_wait) {
		priv->wq_edid_wait = 0;
		wake_up(&priv->wq_edid);
	} else if (cec != 0) {			/* HPD change */
		if (priv->encoder && priv->encoder->dev)
			drm_helper_hpd_irq_event(priv->encoder->dev);
	}
	return IRQ_HANDLED;
}

static uint8_t tda998x_cksum(uint8_t *buf, size_t bytes)
{
	int sum = 0;

	while (bytes--)
		sum -= *buf++;
	return sum;
}

#define HB(x) (x)
#define PB(x) (HB(2) + 1 + (x))

static void
tda998x_write_if(struct tda998x_priv *priv, uint8_t bit, uint16_t addr,
		 uint8_t *buf, size_t size)
{
	buf[PB(0)] = tda998x_cksum(buf, size);

	reg_clear(priv, REG_DIP_IF_FLAGS, bit);
	reg_write_range(priv, addr, buf, size);
	reg_set(priv, REG_DIP_IF_FLAGS, bit);
}

static void
tda998x_write_aif(struct tda998x_priv *priv, struct tda998x_encoder_params *p)
{
	u8 buf[PB(HDMI_AUDIO_INFOFRAME_SIZE) + 1];

	memset(buf, 0, sizeof(buf));
	buf[HB(0)] = HDMI_INFOFRAME_TYPE_AUDIO;
	buf[HB(1)] = 0x01;
	buf[HB(2)] = HDMI_AUDIO_INFOFRAME_SIZE;
	buf[PB(1)] = p->audio_frame[1] & 0x07; /* CC */
	buf[PB(2)] = p->audio_frame[2] & 0x1c; /* SF */
	buf[PB(4)] = p->audio_frame[4];
	buf[PB(5)] = p->audio_frame[5] & 0xf8; /* DM_INH + LSV */

	tda998x_write_if(priv, DIP_IF_FLAGS_IF4, REG_IF4_HB0, buf,
			 sizeof(buf));
}

static void
tda998x_write_avi(struct tda998x_priv *priv, struct drm_display_mode *mode)
{
	u8 buf[PB(HDMI_AVI_INFOFRAME_SIZE) + 1];

	memset(buf, 0, sizeof(buf));
	buf[HB(0)] = HDMI_INFOFRAME_TYPE_AVI;
	buf[HB(1)] = 0x02;
	buf[HB(2)] = HDMI_AVI_INFOFRAME_SIZE;
	buf[PB(1)] = HDMI_SCAN_MODE_UNDERSCAN;
	buf[PB(2)] = HDMI_ACTIVE_ASPECT_PICTURE;
	buf[PB(3)] = HDMI_QUANTIZATION_RANGE_FULL << 2;
	buf[PB(4)] = drm_match_cea_mode(mode);

	tda998x_write_if(priv, DIP_IF_FLAGS_IF2, REG_IF2_HB0, buf,
			 sizeof(buf));
}

static void tda998x_audio_mute(struct tda998x_priv *priv, bool on)
{
	if (on) {
		reg_set(priv, REG_SOFTRESET, SOFTRESET_AUDIO);
		reg_clear(priv, REG_SOFTRESET, SOFTRESET_AUDIO);
		reg_set(priv, REG_AIP_CNTRL_0, AIP_CNTRL_0_RST_FIFO);
	} else {
		reg_clear(priv, REG_AIP_CNTRL_0, AIP_CNTRL_0_RST_FIFO);
	}
}

static void
tda998x_configure_audio(struct tda998x_priv *priv,
		struct drm_display_mode *mode, struct tda998x_encoder_params *p)
{
	uint8_t buf[6], clksel_aip, clksel_fs, cts_n, adiv;
	uint32_t n;

	/* Enable audio ports */
	reg_write(priv, REG_ENA_AP, p->audio_cfg);
	reg_write(priv, REG_ENA_ACLK, p->audio_clk_cfg);

	/* Set audio input source */
	switch (p->audio_format) {
	case AFMT_SPDIF:
		reg_write(priv, REG_MUX_AP, MUX_AP_SELECT_SPDIF);
		clksel_aip = AIP_CLKSEL_AIP_SPDIF;
		clksel_fs = AIP_CLKSEL_FS_FS64SPDIF;
		cts_n = CTS_N_M(3) | CTS_N_K(3);
		break;

	case AFMT_I2S:
		reg_write(priv, REG_MUX_AP, MUX_AP_SELECT_I2S);
		clksel_aip = AIP_CLKSEL_AIP_I2S;
		clksel_fs = AIP_CLKSEL_FS_ACLK;
		cts_n = CTS_N_M(3) | CTS_N_K(3);
		break;

	default:
		BUG();
		return;
	}

	reg_write(priv, REG_AIP_CLKSEL, clksel_aip);
	reg_clear(priv, REG_AIP_CNTRL_0, AIP_CNTRL_0_LAYOUT |
					AIP_CNTRL_0_ACR_MAN);	/* auto CTS */
	reg_write(priv, REG_CTS_N, cts_n);

	/*
	 * Audio input somehow depends on HDMI line rate which is
	 * related to pixclk. Testing showed that modes with pixclk
	 * >100MHz need a larger divider while <40MHz need the default.
	 * There is no detailed info in the datasheet, so we just
	 * assume 100MHz requires larger divider.
	 */
	adiv = AUDIO_DIV_SERCLK_8;
	if (mode->clock > 100000)
		adiv++;			/* AUDIO_DIV_SERCLK_16 */

	/* S/PDIF asks for a larger divider */
	if (p->audio_format == AFMT_SPDIF)
		adiv++;			/* AUDIO_DIV_SERCLK_16 or _32 */

	reg_write(priv, REG_AUDIO_DIV, adiv);

	/*
	 * This is the approximate value of N, which happens to be
	 * the recommended values for non-coherent clocks.
	 */
	n = 128 * p->audio_sample_rate / 1000;

	/* Write the CTS and N values */
	buf[0] = 0x44;
	buf[1] = 0x42;
	buf[2] = 0x01;
	buf[3] = n;
	buf[4] = n >> 8;
	buf[5] = n >> 16;
	reg_write_range(priv, REG_ACR_CTS_0, buf, 6);

	/* Set CTS clock reference */
	reg_write(priv, REG_AIP_CLKSEL, clksel_aip | clksel_fs);

	/* Reset CTS generator */
	reg_set(priv, REG_AIP_CNTRL_0, AIP_CNTRL_0_RST_CTS);
	reg_clear(priv, REG_AIP_CNTRL_0, AIP_CNTRL_0_RST_CTS);

	/* Write the channel status */
	buf[0] = IEC958_AES0_CON_NOT_COPYRIGHT;
	buf[1] = 0x00;
	buf[2] = IEC958_AES3_CON_FS_NOTID;
	buf[3] = IEC958_AES4_CON_ORIGFS_NOTID |
			IEC958_AES4_CON_MAX_WORDLEN_24;
	reg_write_range(priv, REG_CH_STAT_B(0), buf, 4);

	tda998x_audio_mute(priv, true);
	msleep(20);
	tda998x_audio_mute(priv, false);

	/* Write the audio information packet */
	tda998x_write_aif(priv, p);
}

/* DRM encoder functions */

static void tda998x_encoder_set_config(struct tda998x_priv *priv,
				       const struct tda998x_encoder_params *p)
{
	priv->vip_cntrl_0 = VIP_CNTRL_0_SWAP_A(p->swap_a) |
			    (p->mirr_a ? VIP_CNTRL_0_MIRR_A : 0) |
			    VIP_CNTRL_0_SWAP_B(p->swap_b) |
			    (p->mirr_b ? VIP_CNTRL_0_MIRR_B : 0);
	priv->vip_cntrl_1 = VIP_CNTRL_1_SWAP_C(p->swap_c) |
			    (p->mirr_c ? VIP_CNTRL_1_MIRR_C : 0) |
			    VIP_CNTRL_1_SWAP_D(p->swap_d) |
			    (p->mirr_d ? VIP_CNTRL_1_MIRR_D : 0);
	priv->vip_cntrl_2 = VIP_CNTRL_2_SWAP_E(p->swap_e) |
			    (p->mirr_e ? VIP_CNTRL_2_MIRR_E : 0) |
			    VIP_CNTRL_2_SWAP_F(p->swap_f) |
			    (p->mirr_f ? VIP_CNTRL_2_MIRR_F : 0);

	priv->params = *p;
}

static void tda998x_encoder_dpms(struct tda998x_priv *priv, int mode)
{
	/* we only care about on or off: */
	if (mode != DRM_MODE_DPMS_ON)
		mode = DRM_MODE_DPMS_OFF;

	if (mode == priv->dpms)
		return;

	switch (mode) {
	case DRM_MODE_DPMS_ON:
		/* enable video ports, audio will be enabled later */
		reg_write(priv, REG_ENA_VP_0, 0xff);
		reg_write(priv, REG_ENA_VP_1, 0xff);
		reg_write(priv, REG_ENA_VP_2, 0xff);
		/* set muxing after enabling ports: */
		reg_write(priv, REG_VIP_CNTRL_0, priv->vip_cntrl_0);
		reg_write(priv, REG_VIP_CNTRL_1, priv->vip_cntrl_1);
		reg_write(priv, REG_VIP_CNTRL_2, priv->vip_cntrl_2);
		break;
	case DRM_MODE_DPMS_OFF:
		/* disable video ports */
		reg_write(priv, REG_ENA_VP_0, 0x00);
		reg_write(priv, REG_ENA_VP_1, 0x00);
		reg_write(priv, REG_ENA_VP_2, 0x00);
		break;
	}

	priv->dpms = mode;
}

static void
tda998x_encoder_save(struct drm_encoder *encoder)
{
	DBG("");
}

static void
tda998x_encoder_restore(struct drm_encoder *encoder)
{
	DBG("");
}

static bool
tda998x_encoder_mode_fixup(struct drm_encoder *encoder,
			  const struct drm_display_mode *mode,
			  struct drm_display_mode *adjusted_mode)
{
	return true;
}

static int tda998x_encoder_mode_valid(struct tda998x_priv *priv,
				      struct drm_display_mode *mode)
{
	if (mode->clock > 150000)
		return MODE_CLOCK_HIGH;
	if (mode->htotal >= BIT(13))
		return MODE_BAD_HVALUE;
	if (mode->vtotal >= BIT(11))
		return MODE_BAD_VVALUE;
	return MODE_OK;
}

static void
tda998x_encoder_mode_set(struct tda998x_priv *priv,
			 struct drm_display_mode *mode,
			 struct drm_display_mode *adjusted_mode)
{
	uint16_t ref_pix, ref_line, n_pix, n_line;
	uint16_t hs_pix_s, hs_pix_e;
	uint16_t vs1_pix_s, vs1_pix_e, vs1_line_s, vs1_line_e;
	uint16_t vs2_pix_s, vs2_pix_e, vs2_line_s, vs2_line_e;
	uint16_t vwin1_line_s, vwin1_line_e;
	uint16_t vwin2_line_s, vwin2_line_e;
	uint16_t de_pix_s, de_pix_e;
	uint8_t reg, div, rep;

	/*
	 * Internally TDA998x is using ITU-R BT.656 style sync but
	 * we get VESA style sync. TDA998x is using a reference pixel
	 * relative to ITU to sync to the input frame and for output
	 * sync generation. Currently, we are using reference detection
	 * from HS/VS, i.e. REFPIX/REFLINE denote frame start sync point
	 * which is position of rising VS with coincident rising HS.
	 *
	 * Now there is some issues to take care of:
	 * - HDMI data islands require sync-before-active
	 * - TDA998x register values must be > 0 to be enabled
	 * - REFLINE needs an additional offset of +1
	 * - REFPIX needs an addtional offset of +1 for UYUV and +3 for RGB
	 *
	 * So we add +1 to all horizontal and vertical register values,
	 * plus an additional +3 for REFPIX as we are using RGB input only.
	 */
	n_pix        = mode->htotal;
	n_line       = mode->vtotal;

	hs_pix_e     = mode->hsync_end - mode->hdisplay;
	hs_pix_s     = mode->hsync_start - mode->hdisplay;
	de_pix_e     = mode->htotal;
	de_pix_s     = mode->htotal - mode->hdisplay;
	ref_pix      = 3 + hs_pix_s;

	/*
	 * Attached LCD controllers may generate broken sync. Allow
	 * those to adjust the position of the rising VS edge by adding
	 * HSKEW to ref_pix.
	 */
	if (adjusted_mode->flags & DRM_MODE_FLAG_HSKEW)
		ref_pix += adjusted_mode->hskew;

	if ((mode->flags & DRM_MODE_FLAG_INTERLACE) == 0) {
		ref_line     = 1 + mode->vsync_start - mode->vdisplay;
		vwin1_line_s = mode->vtotal - mode->vdisplay - 1;
		vwin1_line_e = vwin1_line_s + mode->vdisplay;
		vs1_pix_s    = vs1_pix_e = hs_pix_s;
		vs1_line_s   = mode->vsync_start - mode->vdisplay;
		vs1_line_e   = vs1_line_s +
			       mode->vsync_end - mode->vsync_start;
		vwin2_line_s = vwin2_line_e = 0;
		vs2_pix_s    = vs2_pix_e  = 0;
		vs2_line_s   = vs2_line_e = 0;
	} else {
		ref_line     = 1 + (mode->vsync_start - mode->vdisplay)/2;
		vwin1_line_s = (mode->vtotal - mode->vdisplay)/2;
		vwin1_line_e = vwin1_line_s + mode->vdisplay/2;
		vs1_pix_s    = vs1_pix_e = hs_pix_s;
		vs1_line_s   = (mode->vsync_start - mode->vdisplay)/2;
		vs1_line_e   = vs1_line_s +
			       (mode->vsync_end - mode->vsync_start)/2;
		vwin2_line_s = vwin1_line_s + mode->vtotal/2;
		vwin2_line_e = vwin2_line_s + mode->vdisplay/2;
		vs2_pix_s    = vs2_pix_e = hs_pix_s + mode->htotal/2;
		vs2_line_s   = vs1_line_s + mode->vtotal/2 ;
		vs2_line_e   = vs2_line_s +
			       (mode->vsync_end - mode->vsync_start)/2;
	}

	div = 148500 / mode->clock;
	if (div != 0) {
		div--;
		if (div > 3)
			div = 3;
	}

	/* mute the audio FIFO: */
	reg_set(priv, REG_AIP_CNTRL_0, AIP_CNTRL_0_RST_FIFO);

	/* set HDMI HDCP mode off: */
	reg_write(priv, REG_TBG_CNTRL_1, TBG_CNTRL_1_DWIN_DIS);
	reg_clear(priv, REG_TX33, TX33_HDMI);
	reg_write(priv, REG_ENC_CNTRL, ENC_CNTRL_CTL_CODE(0));

	/* no pre-filter or interpolator: */
	reg_write(priv, REG_HVF_CNTRL_0, HVF_CNTRL_0_PREFIL(0) |
			HVF_CNTRL_0_INTPOL(0));
	reg_write(priv, REG_VIP_CNTRL_5, VIP_CNTRL_5_SP_CNT(0));
	reg_write(priv, REG_VIP_CNTRL_4, VIP_CNTRL_4_BLANKIT(0) |
			VIP_CNTRL_4_BLC(0));

	reg_clear(priv, REG_PLL_SERIAL_1, PLL_SERIAL_1_SRL_MAN_IZ);
	reg_clear(priv, REG_PLL_SERIAL_3, PLL_SERIAL_3_SRL_CCIR |
					  PLL_SERIAL_3_SRL_DE);
	reg_write(priv, REG_SERIALIZER, 0);
	reg_write(priv, REG_HVF_CNTRL_1, HVF_CNTRL_1_VQR(0));

	/* TODO enable pixel repeat for pixel rates less than 25Msamp/s */
	rep = 0;
	reg_write(priv, REG_RPT_CNTRL, 0);
	reg_write(priv, REG_SEL_CLK, SEL_CLK_SEL_VRF_CLK(0) |
			SEL_CLK_SEL_CLK1 | SEL_CLK_ENA_SC_CLK);

	reg_write(priv, REG_PLL_SERIAL_2, PLL_SERIAL_2_SRL_NOSC(div) |
			PLL_SERIAL_2_SRL_PR(rep));

	/* set color matrix bypass flag: */
	reg_write(priv, REG_MAT_CONTRL, MAT_CONTRL_MAT_BP |
				MAT_CONTRL_MAT_SC(1));

	/* set BIAS tmds value: */
	reg_write(priv, REG_ANA_GENERAL, 0x09);

	/*
	 * Sync on rising HSYNC/VSYNC
	 */
	reg = VIP_CNTRL_3_SYNC_HS;

	/*
	 * TDA19988 requires high-active sync at input stage,
	 * so invert low-active sync provided by master encoder here
	 */
	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		reg |= VIP_CNTRL_3_H_TGL;
	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		reg |= VIP_CNTRL_3_V_TGL;
	reg_write(priv, REG_VIP_CNTRL_3, reg);

	reg_write(priv, REG_VIDFORMAT, 0x00);
	reg_write16(priv, REG_REFPIX_MSB, ref_pix);
	reg_write16(priv, REG_REFLINE_MSB, ref_line);
	reg_write16(priv, REG_NPIX_MSB, n_pix);
	reg_write16(priv, REG_NLINE_MSB, n_line);
	reg_write16(priv, REG_VS_LINE_STRT_1_MSB, vs1_line_s);
	reg_write16(priv, REG_VS_PIX_STRT_1_MSB, vs1_pix_s);
	reg_write16(priv, REG_VS_LINE_END_1_MSB, vs1_line_e);
	reg_write16(priv, REG_VS_PIX_END_1_MSB, vs1_pix_e);
	reg_write16(priv, REG_VS_LINE_STRT_2_MSB, vs2_line_s);
	reg_write16(priv, REG_VS_PIX_STRT_2_MSB, vs2_pix_s);
	reg_write16(priv, REG_VS_LINE_END_2_MSB, vs2_line_e);
	reg_write16(priv, REG_VS_PIX_END_2_MSB, vs2_pix_e);
	reg_write16(priv, REG_HS_PIX_START_MSB, hs_pix_s);
	reg_write16(priv, REG_HS_PIX_STOP_MSB, hs_pix_e);
	reg_write16(priv, REG_VWIN_START_1_MSB, vwin1_line_s);
	reg_write16(priv, REG_VWIN_END_1_MSB, vwin1_line_e);
	reg_write16(priv, REG_VWIN_START_2_MSB, vwin2_line_s);
	reg_write16(priv, REG_VWIN_END_2_MSB, vwin2_line_e);
	reg_write16(priv, REG_DE_START_MSB, de_pix_s);
	reg_write16(priv, REG_DE_STOP_MSB, de_pix_e);

	if (priv->rev == TDA19988) {
		/* let incoming pixels fill the active space (if any) */
		reg_write(priv, REG_ENABLE_SPACE, 0x00);
	}

	/*
	 * Always generate sync polarity relative to input sync and
	 * revert input stage toggled sync at output stage
	 */
	reg = TBG_CNTRL_1_DWIN_DIS | TBG_CNTRL_1_TGL_EN;
	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		reg |= TBG_CNTRL_1_H_TGL;
	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		reg |= TBG_CNTRL_1_V_TGL;
	reg_write(priv, REG_TBG_CNTRL_1, reg);

	/* must be last register set: */
	reg_write(priv, REG_TBG_CNTRL_0, 0);

	/* Only setup the info frames if the sink is HDMI */
	if (priv->is_hdmi_sink) {
		/* We need to turn HDMI HDCP stuff on to get audio through */
		reg &= ~TBG_CNTRL_1_DWIN_DIS;
		reg_write(priv, REG_TBG_CNTRL_1, reg);
		reg_write(priv, REG_ENC_CNTRL, ENC_CNTRL_CTL_CODE(1));
		reg_set(priv, REG_TX33, TX33_HDMI);

		tda998x_write_avi(priv, adjusted_mode);

		if (priv->params.audio_cfg)
			tda998x_configure_audio(priv, adjusted_mode,
						&priv->params);
	}
}

static int read_edid_block(struct tda998x_priv *priv, uint8_t *buf, int blk)
{
	uint8_t offset, segptr;
	int ret, i;

	offset = (blk & 1) ? 128 : 0;
	segptr = blk / 2;

	reg_write(priv, REG_DDC_ADDR, 0xa0);
	reg_write(priv, REG_DDC_OFFS, offset);
	reg_write(priv, REG_DDC_SEGM_ADDR, 0x60);
	reg_write(priv, REG_DDC_SEGM, segptr);

	/* enable reading EDID: */
	priv->wq_edid_wait = 1;
	reg_write(priv, REG_EDID_CTRL, 0x1);

	/* flag must be cleared by sw: */
	reg_write(priv, REG_EDID_CTRL, 0x0);

	/* wait for block read to complete: */
	if (priv->hdmi->irq) {
		i = wait_event_timeout(priv->wq_edid,
					!priv->wq_edid_wait,
					msecs_to_jiffies(100));
		if (i < 0) {
			dev_err(&priv->hdmi->dev, "read edid wait err %d\n", i);
			return i;
		}
	} else {
		for (i = 100; i > 0; i--) {
			msleep(1);
			ret = reg_read(priv, REG_INT_FLAGS_2);
			if (ret < 0)
				return ret;
			if (ret & INT_FLAGS_2_EDID_BLK_RD)
				break;
		}
	}

	if (i == 0) {
		dev_err(&priv->hdmi->dev, "read edid timeout\n");
		return -ETIMEDOUT;
	}

	ret = reg_read_range(priv, REG_EDID_DATA_0, buf, EDID_LENGTH);
	if (ret != EDID_LENGTH) {
		dev_err(&priv->hdmi->dev, "failed to read edid block %d: %d\n",
			blk, ret);
		return ret;
	}

	return 0;
}

static uint8_t *do_get_edid(struct tda998x_priv *priv)
{
	int j, valid_extensions = 0;
	uint8_t *block, *new;
	bool print_bad_edid = drm_debug & DRM_UT_KMS;

	if ((block = kmalloc(EDID_LENGTH, GFP_KERNEL)) == NULL)
		return NULL;

	if (priv->rev == TDA19988)
		reg_clear(priv, REG_TX4, TX4_PD_RAM);

	/* base block fetch */
	if (read_edid_block(priv, block, 0))
		goto fail;

	if (!drm_edid_block_valid(block, 0, print_bad_edid))
		goto fail;

	/* if there's no extensions, we're done */
	if (block[0x7e] == 0)
		goto done;

	new = krealloc(block, (block[0x7e] + 1) * EDID_LENGTH, GFP_KERNEL);
	if (!new)
		goto fail;
	block = new;

	for (j = 1; j <= block[0x7e]; j++) {
		uint8_t *ext_block = block + (valid_extensions + 1) * EDID_LENGTH;
		if (read_edid_block(priv, ext_block, j))
			goto fail;

		if (!drm_edid_block_valid(ext_block, j, print_bad_edid))
			goto fail;

		valid_extensions++;
	}

	if (valid_extensions != block[0x7e]) {
		block[EDID_LENGTH-1] += block[0x7e] - valid_extensions;
		block[0x7e] = valid_extensions;
		new = krealloc(block, (valid_extensions + 1) * EDID_LENGTH, GFP_KERNEL);
		if (!new)
			goto fail;
		block = new;
	}

done:
	if (priv->rev == TDA19988)
		reg_set(priv, REG_TX4, TX4_PD_RAM);

	return block;

fail:
	if (priv->rev == TDA19988)
		reg_set(priv, REG_TX4, TX4_PD_RAM);
	dev_warn(&priv->hdmi->dev, "failed to read EDID\n");
	kfree(block);
	return NULL;
}

static int
tda998x_encoder_get_modes(struct tda998x_priv *priv,
			  struct drm_connector *connector)
{
	struct edid *edid = (struct edid *)do_get_edid(priv);
	int n = 0;

	if (edid) {
		drm_mode_connector_update_edid_property(connector, edid);
		n = drm_add_edid_modes(connector, edid);
		priv->is_hdmi_sink = drm_detect_hdmi_monitor(edid);
		kfree(edid);
	}

	return n;
}

static void tda998x_encoder_set_polling(struct tda998x_priv *priv,
					struct drm_connector *connector)
{
	if (priv->hdmi->irq)
		connector->polled = DRM_CONNECTOR_POLL_HPD;
	else
		connector->polled = DRM_CONNECTOR_POLL_CONNECT |
			DRM_CONNECTOR_POLL_DISCONNECT;
}

static int
tda998x_encoder_set_property(struct drm_encoder *encoder,
			    struct drm_connector *connector,
			    struct drm_property *property,
			    uint64_t val)
{
	DBG("");
	return 0;
}

static void tda998x_destroy(struct tda998x_priv *priv)
{
	/* disable all IRQs and free the IRQ handler */
	cec_write(priv, REG_CEC_RXSHPDINTENA, 0);
	reg_clear(priv, REG_INT_FLAGS_2, INT_FLAGS_2_EDID_BLK_RD);
	if (priv->hdmi->irq)
		free_irq(priv->hdmi->irq, priv);

	i2c_unregister_device(priv->cec);
}

/* Slave encoder support */

static void
tda998x_encoder_slave_set_config(struct drm_encoder *encoder, void *params)
{
	tda998x_encoder_set_config(to_tda998x_priv(encoder), params);
}

static void tda998x_encoder_slave_destroy(struct drm_encoder *encoder)
{
	struct tda998x_priv *priv = to_tda998x_priv(encoder);

	tda998x_destroy(priv);
	drm_i2c_encoder_destroy(encoder);
	kfree(priv);
}

static void tda998x_encoder_slave_dpms(struct drm_encoder *encoder, int mode)
{
	tda998x_encoder_dpms(to_tda998x_priv(encoder), mode);
}

static int tda998x_encoder_slave_mode_valid(struct drm_encoder *encoder,
					    struct drm_display_mode *mode)
{
	return tda998x_encoder_mode_valid(to_tda998x_priv(encoder), mode);
}

static void
tda998x_encoder_slave_mode_set(struct drm_encoder *encoder,
			       struct drm_display_mode *mode,
			       struct drm_display_mode *adjusted_mode)
{
	tda998x_encoder_mode_set(to_tda998x_priv(encoder), mode, adjusted_mode);
}

static enum drm_connector_status
tda998x_encoder_slave_detect(struct drm_encoder *encoder,
			     struct drm_connector *connector)
{
	return connector_status_connected;
}

static int tda998x_encoder_slave_get_modes(struct drm_encoder *encoder,
					   struct drm_connector *connector)
{
	return tda998x_encoder_get_modes(to_tda998x_priv(encoder), connector);
}

static int
tda998x_encoder_slave_create_resources(struct drm_encoder *encoder,
				       struct drm_connector *connector)
{
	tda998x_encoder_set_polling(to_tda998x_priv(encoder), connector);
	return 0;
}

static struct drm_encoder_slave_funcs tda998x_encoder_slave_funcs = {
	.set_config = tda998x_encoder_slave_set_config,
	.destroy = tda998x_encoder_slave_destroy,
	.dpms = tda998x_encoder_slave_dpms,
	.save = tda998x_encoder_save,
	.restore = tda998x_encoder_restore,
	.mode_fixup = tda998x_encoder_mode_fixup,
	.mode_valid = tda998x_encoder_slave_mode_valid,
	.mode_set = tda998x_encoder_slave_mode_set,
	.detect = tda998x_encoder_slave_detect,
	.get_modes = tda998x_encoder_slave_get_modes,
	.create_resources = tda998x_encoder_slave_create_resources,
	.set_property = tda998x_encoder_set_property,
};

/* I2C driver functions */

static int tda998x_create(struct i2c_client *client, struct tda998x_priv *priv)
{
	struct device_node *np = client->dev.of_node;
	u32 video;
	int rev_lo, rev_hi, ret;

	priv->vip_cntrl_0 = VIP_CNTRL_0_SWAP_A(2) | VIP_CNTRL_0_SWAP_B(3);
	priv->vip_cntrl_1 = VIP_CNTRL_1_SWAP_C(0) | VIP_CNTRL_1_SWAP_D(1);
	priv->vip_cntrl_2 = VIP_CNTRL_2_SWAP_E(4) | VIP_CNTRL_2_SWAP_F(5);

	priv->current_page = 0xff;
	priv->hdmi = client;
	priv->cec = i2c_new_dummy(client->adapter, 0x34);
	if (!priv->cec)
		return -ENODEV;

	priv->dpms = DRM_MODE_DPMS_OFF;

	/* wake up the device: */
	cec_write(priv, REG_CEC_ENAMODS,
			CEC_ENAMODS_EN_RXSENS | CEC_ENAMODS_EN_HDMI);

	tda998x_reset(priv);

	/* read version: */
	rev_lo = reg_read(priv, REG_VERSION_LSB);
	rev_hi = reg_read(priv, REG_VERSION_MSB);
	if (rev_lo < 0 || rev_hi < 0) {
		ret = rev_lo < 0 ? rev_lo : rev_hi;
		goto fail;
	}

	priv->rev = rev_lo | rev_hi << 8;

	/* mask off feature bits: */
	priv->rev &= ~0x30; /* not-hdcp and not-scalar bit */

	switch (priv->rev) {
	case TDA9989N2:
		dev_info(&client->dev, "found TDA9989 n2");
		break;
	case TDA19989:
		dev_info(&client->dev, "found TDA19989");
		break;
	case TDA19989N2:
		dev_info(&client->dev, "found TDA19989 n2");
		break;
	case TDA19988:
		dev_info(&client->dev, "found TDA19988");
		break;
	default:
		dev_err(&client->dev, "found unsupported device: %04x\n",
			priv->rev);
		goto fail;
	}

	/* after reset, enable DDC: */
	reg_write(priv, REG_DDC_DISABLE, 0x00);

	/* set clock on DDC channel: */
	reg_write(priv, REG_TX3, 39);

	/* if necessary, disable multi-master: */
	if (priv->rev == TDA19989)
		reg_set(priv, REG_I2C_MASTER, I2C_MASTER_DIS_MM);

	cec_write(priv, REG_CEC_FRO_IM_CLK_CTRL,
			CEC_FRO_IM_CLK_CTRL_GHOST_DIS | CEC_FRO_IM_CLK_CTRL_IMCLK_SEL);

	/* initialize the optional IRQ */
	if (client->irq) {
		int irqf_trigger;

		/* init read EDID waitqueue */
		init_waitqueue_head(&priv->wq_edid);

		/* clear pending interrupts */
		reg_read(priv, REG_INT_FLAGS_0);
		reg_read(priv, REG_INT_FLAGS_1);
		reg_read(priv, REG_INT_FLAGS_2);

		irqf_trigger =
			irqd_get_trigger_type(irq_get_irq_data(client->irq));
		ret = request_threaded_irq(client->irq, NULL,
					   tda998x_irq_thread,
					   irqf_trigger | IRQF_ONESHOT,
					   "tda998x", priv);
		if (ret) {
			dev_err(&client->dev,
				"failed to request IRQ#%u: %d\n",
				client->irq, ret);
			goto fail;
		}

		/* enable HPD irq */
		cec_write(priv, REG_CEC_RXSHPDINTENA, CEC_RXSHPDLEV_HPD);
	}

	/* enable EDID read irq: */
	reg_set(priv, REG_INT_FLAGS_2, INT_FLAGS_2_EDID_BLK_RD);

	if (!np)
		return 0;		/* non-DT */

	/* get the optional video properties */
	ret = of_property_read_u32(np, "video-ports", &video);
	if (ret == 0) {
		priv->vip_cntrl_0 = video >> 16;
		priv->vip_cntrl_1 = video >> 8;
		priv->vip_cntrl_2 = video;
	}

	return 0;

fail:
	/* if encoder_init fails, the encoder slave is never registered,
	 * so cleanup here:
	 */
	if (priv->cec)
		i2c_unregister_device(priv->cec);
	return -ENXIO;
}

static int tda998x_encoder_init(struct i2c_client *client,
				struct drm_device *dev,
				struct drm_encoder_slave *encoder_slave)
{
	struct tda998x_priv *priv;
	int ret;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->encoder = &encoder_slave->base;

	ret = tda998x_create(client, priv);
	if (ret) {
		kfree(priv);
		return ret;
	}

	encoder_slave->slave_priv = priv;
	encoder_slave->slave_funcs = &tda998x_encoder_slave_funcs;

	return 0;
}

struct tda998x_priv2 {
	struct drm_encoder encoder;
	struct drm_connector connector;
};

#define conn_to_rk1000_tve(x) \
	container_of(x, struct rk1000_tve, connector);

#define enc_to_tda998x_priv2(x) \
	container_of(x, struct rk1000_tve, encoder);

static void tda998x_encoder2_dpms(struct drm_encoder *encoder, int mode)
{
	struct tda998x_priv2 *priv = enc_to_tda998x_priv2(encoder);

	tda998x_encoder_dpms(&priv->base, mode);
}

static void tda998x_encoder_prepare(struct drm_encoder *encoder)
{
	tda998x_encoder2_dpms(encoder, DRM_MODE_DPMS_OFF);
}

static void tda998x_encoder_commit(struct drm_encoder *encoder)
{
	tda998x_encoder2_dpms(encoder, DRM_MODE_DPMS_ON);
}

static void tda998x_encoder2_mode_set(struct drm_encoder *encoder,
				      struct drm_display_mode *mode,
				      struct drm_display_mode *adjusted_mode)
{
	struct tda998x_priv2 *priv = enc_to_tda998x_priv2(encoder);

	tda998x_encoder_mode_set(&priv->base, mode, adjusted_mode);
}

static const struct drm_encoder_helper_funcs tda998x_encoder_helper_funcs = {
	.dpms = tda998x_encoder2_dpms,
	.save = tda998x_encoder_save,
	.restore = tda998x_encoder_restore,
	.mode_fixup = tda998x_encoder_mode_fixup,
	.prepare = tda998x_encoder_prepare,
	.commit = tda998x_encoder_commit,
	.mode_set = tda998x_encoder2_mode_set,
};

static void tda998x_encoder_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
}

static const struct drm_encoder_funcs tda998x_encoder_funcs = {
	.destroy = tda998x_encoder_destroy,
};

static int tda998x_connector_get_modes(struct drm_connector *connector)
{
	struct tda998x_priv2 *priv = conn_to_rk1000_tve(connector);

	return tda998x_encoder_get_modes(&priv->base, connector);
}

static int tda998x_connector_mode_valid(struct drm_connector *connector,
					struct drm_display_mode *mode)
{
	struct tda998x_priv2 *priv = conn_to_rk1000_tve(connector);

	return tda998x_encoder_mode_valid(&priv->base, mode);
}

////////////////////////////////////

static struct drm_encoder *
rk1000_tve_connector_best_encoder(struct drm_connector *connector)
{
	struct rk1000_tve *tve = conn_to_rk1000_tve(connector);

	return &tve->encoder;
}

static
const struct drm_connector_helper_funcs rk1000_tve_connector_helper_funcs = {
	.get_modes = rk1000_tve_connector_get_modes,
	.mode_valid = rk1000_tve_connector_mode_valid,
	.best_encoder = rk1000_tve_connector_best_encoder,
};

static enum drm_connector_status
rk1000_tve_connector_detect(struct drm_connector *connector, bool force)
{
	return connector_status_connected;
}

static void rk1000_tve_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static const struct drm_connector_funcs rk1000_tve_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.detect = rk1000_tve_connector_detect,
	.destroy = rk1000_tve_connector_destroy,
};


static int rk1000_tve_register(struct drm_device *drm, struct rk1000_tve *tve)
{
	int ret;

/*	ret = imx_drm_encoder_parse_of(drm, &tve->encoder,
				       tve->dev->of_node);
	if (ret)
		return ret;*/

	drm_encoder_helper_add(&tve->encoder, &rk1000_tve_encoder_helper_funcs);
	ret = drm_encoder_init(drm, &tve->encoder, &rk1000_tve_encoder_funcs,
			 DRM_MODE_ENCODER_TVDAC);
	if (ret)
		return ret;

	drm_connector_helper_add(&tve->connector,
				 &rk1000_tve_connector_helper_funcs);
	ret = drm_connector_init(drm, &tve->connector,
				 &rk1000_tve_connector_funcs,
				 DRM_MODE_CONNECTOR_VGA);
	if (ret)
		goto err_connector_init;

	tve->connector.encoder = &tve->encoder;
	ret = drm_mode_connector_attach_encoder(&tve->connector, &tve->encoder);
	if (ret)
		goto err_attach;

	ret = drm_connector_register(&tve->connector);
	if (ret)
		goto err_attach;

	return 0;

err_attach:
	drm_connector_cleanup(&tve->connector);
err_connector_init:
	drm_encoder_cleanup(&tve->encoder);
	return ret;
}

static int rk1000_tve_bind(struct device *dev, struct device *master, void *data)
{
	struct rk1000 *rk1000 = dev_get_drvdata(dev);
	struct rk1000_tve *tve;
	struct drm_device *drm = data;
	int ret;

	tve = kzalloc(sizeof(rk1000_tve), GFP_KERNEL);
	if (!tve)
		return -ENOMEM;

	tve->rk1000 = rk1000;
	rk1000->tve = tve;

	tve->connector.interlace_allowed = 1;
	tve->encoder.possible_crtcs = 1 << 0;

	ret = rk1000_tve_register(drm, tve);
	if (ret) {
		kfree(rk1000->tve);
		return ret;
	}

	return 0;
}

static void rk1000_tve_unbind(struct device *dev, struct device *master,
			   void *data)
{
	struct rk1000 *rk1000 = dev_get_drvdata(dev);
	struct rk1000_tve *tve = rk1000->tve;

	tve->connector.funcs->destroy(&tve->connector);
	tve->encoder.funcs->destroy(&tve->encoder);

	kfree(rk1000->tve);
}

static const struct component_ops rk1000_tve_ops = {
	.bind = rk1000_tve_bind,
	.unbind = rk1000_tve_unbind,
};

static int rk1000_tve_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev.parent, &rk1000_tve_ops);
}

static int rk1000_tve_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev.parent, &rk1000_tve_ops);
	return 0;
}

static struct platform_driver rk1000_tve_driver = {
	.probe = rk1000_tve_probe,
	.remove = rk1000_tve_remove,
	.driver = {
		.name = "rk1000-tve",
	},
};

module_platform_driver(rk1000_tve_driver);

MODULE_AUTHOR("Heiko Stuebner <heiko@sntech.de>");
MODULE_DESCRIPTION("Rockchip RK1000 TV Encoder");
MODULE_LICENSE("GPL");
