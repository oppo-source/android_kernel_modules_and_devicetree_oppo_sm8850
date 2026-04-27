// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/debugfs.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/qti-regmap-debugfs.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc-dapm.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>

#define CLK_RATE_FIXED 73728000
#define SUPPLIES_NUM   2
#define SLAVE_ADDR     0x00c
#define NUM_REGS       0x03

#define WSA885X_INTR_STATUS0   0x8584   // Base address of the status register
#define WSA885X_INTR_MASK0     0x8581   // Base address of the mask register
#define WSA885X_INTR_CLEAR0    0x8587   // Base address of the acknowledge register
#define WSA885X_INTR_LEVEL0    0x858A   // Base address of the acknowledge register
#define WSA885X_POWER_FSM_CTL0 0x8423
#define WSA885X_PA0_FSM_CTL0   0x842A
#define WSA885X_PA1_FSM_CTL0   0x8434
#define DIG_CTRL1_PIN_CT       0x8510
#define DIG_CTRL1_SPMI_PAD_GPIO2_CTL   0x8518
#define DIG_CTRL1_INTR_MODE    0x8580

#define SMP_AMP_CTRL_STEREO_STEREO_SMP_AMP_CTRL_I2S    0x0000
#define SMP_AMP_CTRL_STEREO_CMT_GRP_MASK       0x0004
#define SMP_AMP_CTRL_STEREO_IT21_CLUSERINDEX   0x0140
#define SMP_AMP_CTRL_STEREO_CS21_CLOCK_VALID   0x0208
#define SMP_AMP_CTRL_STEREO_CS21_SAMPLERATEINDEX       0x0240
#define SMP_AMP_CTRL_STEREO_PPU21_POSTURENUMBER        0x0340
#define SMP_AMP_CTRL_STEREO_FU21_MUTE_CH2X0    0x4405
#define SMP_AMP_CTRL_STEREO_FU21_MUTE_CH2X1    0x4406
#define SMP_AMP_CTRL_STEREO_FU21_CH_VOL_CH2X0_LSB      0x4409
#define SMP_AMP_CTRL_STEREO_FU21_CH_VOL_CH2X0_MSB      0x6409
#define SMP_AMP_CTRL_STEREO_FU21_CH_VOL_CH2X1_LSB      0x440a
#define SMP_AMP_CTRL_STEREO_FU21_CH_VOL_CH2X1_MSB      0x640a
#define SMP_AMP_CTRL_STEREO_PDE23_REQ_PS       0x0a04
#define SMP_AMP_CTRL_STEREO_PDE23_ACT_PS       0x0a40
#define SMP_AMP_CTRL_STEREO_OT23_USAGE         0x0b10
#define SMP_AMP_CTRL_STEREO_CS24_SAMPLERATEINDEX       0x0e40

#define ANA_TOP_PON_CKSK_CTL_0 0x800d
#define ANA_TOP_BG_TVP_UVLO1_PROG      0x8024
#define ANA_TOP_BG_TVP_UVLO2_PROG      0x8025
#define ANA_PLL_DIV_CTL_0      0x8090
#define ANA_PLL_DIV_CTL_1      0x8091
#define ANA_TOP_PLL_VCO_CTL    0x8092
#define ANA_TOP_PLL_LOOPFILT_0         0x8093
#define ANA_TOP_PLL_OVRD_CTL   0x8098
#define ANA_TOP_BG_TVP_OVRD_CTL        0x8034
#define ANA_TOP_BOOST_STB_CTRL2        0x805b
#define ANA_TOP_BOOST_STB_CTRL3        0x805c
#define ANA_TOP_BOOST_BYP_CTRL2        0x805e
#define ANA_TOP_BOOST_BYP_CTRL3        0x805f
#define ANA_TOP_BOOST_MISC     0x8063
#define ANA_TOP_BOOST_PWRSTAGE_CTRL2   0x8065
#define ANA_TOP_BOOST_PWRSTAGE_CTRL4   0x8067
#define ANA_TOP_PLL_STATUS_0   0x809a
#define ANA_TOP_PLL_STATUS_1   0x809b
#define ANA_TOP_IVSENSE_ADC_MODE_CTL2  0x80ca
#define ANA_TOP_IVSENSE_ADC_MODE_CTL3  0x80cb
#define ANA_TOP_IVSENSE_ADC_REF_CTL    0x80cc
#define ANA_TOP_IVSENSE_ADC_CDAC_CAL_CTL2      0x80d0
#define ANA_TOP_SPK_TOP_PWRSTG_CH1_CTRL3       0x8108
#define ANA_TOP_SPK_TOP_PWRSTG_CH1_TUNE3       0x810b
#define ANA_TOP_SPK_TOP_PWRSTG_CH2_CTRL3       0x810e
#define ANA_TOP_SPK_TOP_PWRSTG_CH2_TUNE3       0x8111
#define ANA_TOP_SPK_TOP_SPARE3 0x813c
#define DIG_CTRL0_TOP_CLK_CFG  0x8418
#define DIG_CTRL0_SDCA_COMMIT          0x8419
#define DIG_CTRL0_CLK_SOURCE_ENABLE    0x841a
#define DIG_CTRL0_SYS_CLK_SEL          0x841b
#define DIG_CTRL0_CDC_CLK_CTL          0x841c
#define DIG_CTRL0_PA_FSM_CTL   0x8420
#define DIG_CTRL0_POWER_FSM_CTL0       0x8423
#define DIG_CTRL0_POWER_FSM_CTL1       0x8424
#define DIG_CTRL0_PA0_FSM_CTL1         0x842b
#define DIG_CTRL0_PA1_FSM_CTL1         0x8435
#define DIG_CTRL0_VBAT_THRM_FLT_CTL    0x8458
#define DIG_CTRL0_CDC_RXTX_FSCNT_CTL   0x8470
#define DIG_CTRL0_GAIN_RAMP0_CTL1      0x84b4
#define DIG_CTRL0_GAIN_RAMP1_CTL1      0x84b7
#define DIG_CTRL1_I2S_CTL0     0x85A0
#define DIG_CTRL1_I2S_CFG0_TDM_TX      0x85A2
#define DIG_CTRL1_I2S_CFG1_TDM_TX      0x85A3
#define DIG_CTRL1_I2S_TDM_CTL0 0x85A7
#define DIG_CTRL1_I2S_TDM_CTL1 0x85A9
#define DIG_CTRL1_I2S_TDM_CH_RX        0x85AA
#define DIG_CTRL1_I2S_TDM_CH_TX        0x85AB
#define DIG_CTRL1_I2S_RESET_CTL        0x85AE

#define CDC_RX0_RX_PATH_CFG0   0x8601
#define CDC_RX0_RX_PATH_CFG1   0x8602
#define CDC_RX0_RX_PATH_CTL    0x8606
#define RX0_RX_PATH_DSMDEM_CTL 0x8613
#define CDC_RX1_RX_PATH_CFG0   0x8621
#define CDC_RX1_RX_PATH_CFG1   0x8622
#define CDC_RX1_RX_PATH_CTL    0x8626
#define RX1_RX_PATH_DSMDEM_CTL 0x8633
#define CDC_COMPANDER0_CTL0    0x8640
#define CDC_COMPANDER0_CTL7    0x8647
#define CDC_COMPANDER1_CTL0    0x8660
#define CDC_COMPANDER1_CTL7    0x8667
#define CDC_VSENSE0_SPKR_PROT_PATH_CTL 0x86A1
#define CDC_VSENSE1_SPKR_PROT_PATH_CTL 0x86B1
#define CDC_ISENSE0_SPKR_PROT_PATH_CTL 0x86A9
#define CDC_ISENSE1_SPKR_PROT_PATH_CTL 0x86B9
#define CDC_CLSH_V1P8_BP_CTL1  0x86CD
#define CDC_CLSH_V1P8_BP_CTL0  0x86CC
#define CDC_CLSH_CLSH_SIG_DP_CTL0      0x86C7
#define CDC_CLSH_CLSH_V_HD_PA  0x86C3
#define CDC_CLSH_V1P8_BP_CTL2  0x86CE

#define WSA885X_RX_RATE_8000HZ          0x00
#define WSA885X_RX_RATE_16000HZ         0x01
#define WSA885X_RX_RATE_32000HZ         0x02
#define WSA885X_RX_RATE_44100HZ         0x03
#define WSA885X_RX_RATE_48000HZ         0x04
#define WSA885X_RX_RATE_96000HZ         0x05
#define WSA885X_RX_RATE_192000HZ        0x06
#define WSA885X_RX_RATE_384000HZ        0x07

#define WSA885X_VI_RATE_8000HZ          0x00
#define WSA885X_VI_RATE_16000HZ         0x01
#define WSA885X_VI_RATE_44100HZ         0x02
#define WSA885X_VI_RATE_48000HZ         0x03
#define WSA885X_VI_RATE_96000HZ         0x04
#define WSA885X_VI_RATE_22050HZ         0x05
#define WSA885X_VI_RATE_24000HZ         0x06
#define WSA885X_VI_RATE_192000HZ        0x07
#define WSA885X_VI_RATE_384000HZ        0x08

static const char *const supply_name[] = {
	"vdd-io",
	"vdd-1p8",
};

enum {
	WSA885X_IRQ_INT_SAF2WAR = 0,
	WSA885X_IRQ_INT_WAR2SAF,
	WSA885X_IRQ_INT_DISABLE,
	WSA885X_IRQ_INT_PA0_OCP,
	WSA885X_IRQ_INT_PA1_OCP,
	WSA885X_IRQ_INT_CLIP0,
	WSA885X_IRQ_INT_CLIP1,
	WSA885X_IRQ_INT_CLK_WD,
	WSA885X_IRQ_INT_INTR_GPIO1_PIN,
	WSA885X_IRQ_INT_INTR_GPIO2_PIN,
	WSA885X_IRQ_INT_UVLO,
	WSA885X_IRQ_INT_BOP,
	WSA885X_IRQ_INT_PA0_FSM_ERR,
	WSA885X_IRQ_INT_PA1_FSM_ERR,
	WSA885X_IRQ_INT_MAIN_FSM_ERR,
	WSA885X_IRQ_INT_PCM_DATA0_WD,
	WSA885X_IRQ_INT_PCM_DATA1_WD,
	WSA885X_IRQ_INT_PCM_DATA0_DC,
	WSA885X_IRQ_INT_PCM_DATA1_DC,
	WSA885X_IRQ_INT_PLL_UNLOCKED,
	WSA885X_IRQ_INT_PROT_MODE_CHANGE,
	WSA885X_IRQ_INT_PB_CLOCK_VALID,
	WSA885X_IRQ_INT_SENSE_CLOCK_VALID,
	WSA885X_IRQ_MAX,
};

static const char *wsa885x_irq_names[WSA885X_IRQ_MAX] = {
	"WSA885X_IRQ_INT_SAF2WAR",
	"WSA885X_IRQ_INT_WAR2SAF",
	"WSA885X_IRQ_INT_DISABLE",
	"WSA885X_IRQ_INT_PA0_OCP",
	"WSA885X_IRQ_INT_PA1_OCP",
	"WSA885X_IRQ_INT_CLIP0",
	"WSA885X_IRQ_INT_CLIP1",
	"WSA885X_IRQ_INT_CLK_WD",
	"WSA885X_IRQ_INT_INTR_GPIO1_PIN",
	"WSA885X_IRQ_INT_INTR_GPIO2_PIN",
	"WSA885X_IRQ_INT_UVLO",
	"WSA885X_IRQ_INT_BOP",
	"WSA885X_IRQ_INT_PA0_FSM_ERR",
	"WSA885X_IRQ_INT_PA1_FSM_ERR",
	"WSA885X_IRQ_INT_MAIN_FSM_ERR",
	"WSA885X_IRQ_INT_PCM_DATA0_WD",
	"WSA885X_IRQ_INT_PCM_DATA1_WD",
	"WSA885X_IRQ_INT_PCM_DATA0_DC",
	"WSA885X_IRQ_INT_PCM_DATA1_DC",
	"WSA885X_IRQ_INT_PLL_UNLOCKED",
	"WSA885X_IRQ_INT_PROT_MODE_CHANGE",
	"WSA885X_IRQ_INT_PB_CLOCK_VALID",
	"WSA885X_IRQ_INT_SENSE_CLOCK_VALID"};

struct wsa885x_i2c_priv {
	struct i2c_client *client;
	struct regmap *regmap;
	struct device *dev;
	struct snd_soc_component *component;
	const struct snd_soc_component_driver *driver;
	struct snd_soc_dai_driver *dai_driver;
	struct cdc_regulator *regulator;
	struct regulator_bulk_data supplies[SUPPLIES_NUM];
	struct gpio_desc *sd_n;
	uint32_t sample_rate;
	uint32_t *init_table;
	uint32_t init_table_size;
	uint32_t usage_mode;
	uint32_t rx_slot_mask;
	uint8_t virq[WSA885X_IRQ_MAX];
	struct gpio_desc *intr_pin;
	atomic_t open_count;
};

static const struct regmap_irq wsa885x_irqs[WSA885X_IRQ_MAX] = {
	REGMAP_IRQ_REG(WSA885X_IRQ_INT_SAF2WAR, 0, 0x01),
	REGMAP_IRQ_REG(WSA885X_IRQ_INT_WAR2SAF, 0, 0x02),
	REGMAP_IRQ_REG(WSA885X_IRQ_INT_DISABLE, 0, 0x04),
	REGMAP_IRQ_REG(WSA885X_IRQ_INT_PA0_OCP, 0, 0x08),
	REGMAP_IRQ_REG(WSA885X_IRQ_INT_PA1_OCP, 0, 0x10),
	REGMAP_IRQ_REG(WSA885X_IRQ_INT_CLIP0, 0, 0x20),
	REGMAP_IRQ_REG(WSA885X_IRQ_INT_CLIP1, 0, 0x40),
	REGMAP_IRQ_REG(WSA885X_IRQ_INT_CLK_WD, 0, 0x80),
	REGMAP_IRQ_REG(WSA885X_IRQ_INT_INTR_GPIO1_PIN, 1, 0x00),
	REGMAP_IRQ_REG(WSA885X_IRQ_INT_INTR_GPIO2_PIN, 1, 0x02),
	REGMAP_IRQ_REG(WSA885X_IRQ_INT_UVLO, 1, 0x04),
	REGMAP_IRQ_REG(WSA885X_IRQ_INT_BOP, 1, 0x08),
	REGMAP_IRQ_REG(WSA885X_IRQ_INT_PA0_FSM_ERR, 1, 0x10),
	REGMAP_IRQ_REG(WSA885X_IRQ_INT_PA1_FSM_ERR, 1, 0x20),
	REGMAP_IRQ_REG(WSA885X_IRQ_INT_MAIN_FSM_ERR, 1, 0x40),
	REGMAP_IRQ_REG(WSA885X_IRQ_INT_PCM_DATA0_WD, 1, 0x80),
	REGMAP_IRQ_REG(WSA885X_IRQ_INT_PCM_DATA1_WD, 2, 0x00),
	REGMAP_IRQ_REG(WSA885X_IRQ_INT_PCM_DATA0_DC, 2, 0x02),
	REGMAP_IRQ_REG(WSA885X_IRQ_INT_PCM_DATA1_DC, 2, 0x04),
	REGMAP_IRQ_REG(WSA885X_IRQ_INT_PLL_UNLOCKED, 2, 0x08),
	REGMAP_IRQ_REG(WSA885X_IRQ_INT_PROT_MODE_CHANGE, 2, 0x10),
	REGMAP_IRQ_REG(WSA885X_IRQ_INT_PB_CLOCK_VALID, 2, 0x20),
	REGMAP_IRQ_REG(WSA885X_IRQ_INT_SENSE_CLOCK_VALID, 2, 0x40),
};

static const struct regmap_irq_chip wsa885x_irq_chip = {
	.name = "wsa885x_irq",
	.irqs = wsa885x_irqs,
	.num_irqs = ARRAY_SIZE(wsa885x_irqs),
	.num_regs = 3,
	.status_base = WSA885X_INTR_STATUS0,
	.mask_base = WSA885X_INTR_MASK0,
	//.type_base = WSA885X_INTR_LEVEL0,
	.ack_base = WSA885X_INTR_CLEAR0,
	.use_ack = 1,
	.runtime_pm = false,
	//.handle_post_irq = wsa885x_handle_post_irq,
	.irq_drv_data = NULL,
};

static const struct regmap_range_cfg regmap_ranges[] = {
	{
		.range_min = 0,
		.range_max = 0x88ff,
		.selector_reg = 0x0,
		.selector_mask = 0xFF,
		.selector_shift = 0,
		.window_start = 0,
		.window_len = 0x100,
	},
};

static const struct reg_default codec_reg_defaults[] = {
	{SMP_AMP_CTRL_STEREO_STEREO_SMP_AMP_CTRL_I2S, 0x00},
	{SMP_AMP_CTRL_STEREO_IT21_CLUSERINDEX, 0x01},
	{SMP_AMP_CTRL_STEREO_CMT_GRP_MASK, 0x00},
	{SMP_AMP_CTRL_STEREO_OT23_USAGE, 0x00},
	{SMP_AMP_CTRL_STEREO_CS21_CLOCK_VALID, 0x00},
	{SMP_AMP_CTRL_STEREO_CS21_SAMPLERATEINDEX, 0x04},
	{SMP_AMP_CTRL_STEREO_PPU21_POSTURENUMBER, 0x01},
	{SMP_AMP_CTRL_STEREO_FU21_MUTE_CH2X0, 0x01},
	{SMP_AMP_CTRL_STEREO_FU21_MUTE_CH2X1, 0x01},
	{SMP_AMP_CTRL_STEREO_FU21_CH_VOL_CH2X0_MSB, 0xac},
	{SMP_AMP_CTRL_STEREO_FU21_CH_VOL_CH2X0_LSB, 0x00},
	{SMP_AMP_CTRL_STEREO_FU21_CH_VOL_CH2X1_MSB, 0xac},
	{SMP_AMP_CTRL_STEREO_FU21_CH_VOL_CH2X1_LSB, 0x00},
	{SMP_AMP_CTRL_STEREO_PDE23_REQ_PS, 0x03},
	{SMP_AMP_CTRL_STEREO_PDE23_ACT_PS, 0x03},
	{SMP_AMP_CTRL_STEREO_OT23_USAGE, 0x00},
	{SMP_AMP_CTRL_STEREO_CS24_SAMPLERATEINDEX, 0x03},
	{SMP_AMP_CTRL_STEREO_CS24_SAMPLERATEINDEX, 0x03},
	{ANA_TOP_PON_CKSK_CTL_0, 0x00},
	{ANA_TOP_BG_TVP_UVLO1_PROG, 0x19},
	{ANA_TOP_BG_TVP_UVLO2_PROG, 0x22},
	{ANA_PLL_DIV_CTL_0, 0x0c},
	{ANA_PLL_DIV_CTL_1, 0x50},
	{ANA_TOP_PLL_VCO_CTL, 0x00},
	{ANA_TOP_PLL_LOOPFILT_0, 0xb4},
	{ANA_TOP_PLL_OVRD_CTL, 0x00},
	{ANA_TOP_BG_TVP_OVRD_CTL, 0x00},
	{ANA_TOP_BOOST_STB_CTRL2, 0x03},
	{ANA_TOP_BOOST_STB_CTRL3, 0x3c},
	{ANA_TOP_BOOST_BYP_CTRL2, 0xc5},
	{ANA_TOP_BOOST_BYP_CTRL3, 0x13},
	{ANA_TOP_BOOST_MISC, 0x79},
	{ANA_TOP_BOOST_PWRSTAGE_CTRL2, 0x40},
	{ANA_TOP_BOOST_PWRSTAGE_CTRL4, 0xff},
	{ANA_TOP_PLL_STATUS_0, 0x00},
	{ANA_TOP_PLL_STATUS_1, 0x00},
	{ANA_TOP_IVSENSE_ADC_MODE_CTL2, 0x84},
	{ANA_TOP_IVSENSE_ADC_MODE_CTL3, 0x02},
	{ANA_TOP_IVSENSE_ADC_REF_CTL, 0x00},
	{ANA_TOP_IVSENSE_ADC_CDAC_CAL_CTL2, 0xe0},
	{ANA_TOP_SPK_TOP_PWRSTG_CH1_CTRL3, 0xa4},
	{ANA_TOP_SPK_TOP_PWRSTG_CH1_TUNE3, 0xc9},
	{ANA_TOP_SPK_TOP_PWRSTG_CH2_CTRL3, 0xa4},
	{ANA_TOP_SPK_TOP_PWRSTG_CH2_TUNE3, 0xc5},
	{ANA_TOP_SPK_TOP_PWRSTG_CH2_TUNE3, 0xc9},
	{DIG_CTRL0_TOP_CLK_CFG, 0x00},
	{DIG_CTRL0_SDCA_COMMIT, 0x00},
	{DIG_CTRL0_CLK_SOURCE_ENABLE, 0x00},
	{DIG_CTRL0_SYS_CLK_SEL, 0x00},
	{DIG_CTRL0_CDC_CLK_CTL, 0x0e},
	{DIG_CTRL0_PA_FSM_CTL, 0x00},
	{DIG_CTRL0_POWER_FSM_CTL0, 0x01},
	{DIG_CTRL0_POWER_FSM_CTL1, 0x00},
	{DIG_CTRL0_PA0_FSM_CTL1, 0x45},
	{DIG_CTRL0_PA1_FSM_CTL1, 0x45},
	{DIG_CTRL0_VBAT_THRM_FLT_CTL, 0x7f},
	{DIG_CTRL0_CDC_RXTX_FSCNT_CTL, 0x00},
	{DIG_CTRL0_GAIN_RAMP0_CTL1, 0x00},
	{DIG_CTRL0_GAIN_RAMP1_CTL1, 0x00},
	{DIG_CTRL1_I2S_CTL0, 0x06},
	{DIG_CTRL1_I2S_CFG0_TDM_TX, 0x00},
	{DIG_CTRL1_I2S_CFG1_TDM_TX, 0x00},
	{DIG_CTRL1_I2S_TDM_CTL0, 0x00},
	{DIG_CTRL1_I2S_TDM_CTL1, 0x05},
	{DIG_CTRL1_I2S_TDM_CH_TX, 0x00},
	{DIG_CTRL1_I2S_RESET_CTL, 0x00},
	{CDC_RX0_RX_PATH_CFG0, 0x80},
	{CDC_RX0_RX_PATH_CFG1, 0x64},
	{CDC_RX0_RX_PATH_CTL, 0x04},
	{RX0_RX_PATH_DSMDEM_CTL, 0x00},
	{CDC_RX1_RX_PATH_CFG0, 0x80},
	{CDC_RX1_RX_PATH_CFG1, 0x64},
	{CDC_RX1_RX_PATH_CTL, 0x04},
	{RX1_RX_PATH_DSMDEM_CTL, 0x00},
	{CDC_COMPANDER0_CTL0, 0x00},
	{CDC_COMPANDER0_CTL7, 0x2a},
	{CDC_COMPANDER1_CTL0, 0x00},
	{CDC_COMPANDER1_CTL7, 0x2a},
	{CDC_VSENSE0_SPKR_PROT_PATH_CTL, 0x04},
	{CDC_VSENSE1_SPKR_PROT_PATH_CTL, 0x04},
	{CDC_ISENSE0_SPKR_PROT_PATH_CTL, 0x04},
	{CDC_ISENSE1_SPKR_PROT_PATH_CTL, 0x04},
	{CDC_CLSH_V1P8_BP_CTL1, 0x44},
	{CDC_CLSH_V1P8_BP_CTL0, 0x66},
	{CDC_CLSH_CLSH_SIG_DP_CTL0, 0x00},
	{CDC_CLSH_CLSH_V_HD_PA, 0x0f},
	{CDC_CLSH_V1P8_BP_CTL2, 0x01},
};

static void reg_update_sequence(struct regmap *regmap)
{
	regmap_write(regmap, DIG_CTRL1_I2S_TDM_CTL1, 0x11);
	regmap_write(regmap, DIG_CTRL1_I2S_TDM_CTL0, 0x04);
	regmap_update_bits(regmap, DIG_CTRL1_I2S_TDM_CTL0, 0x01, 0x01);
	regmap_write(regmap, DIG_CTRL1_I2S_TDM_CH_TX, 0x01);
	regmap_update_bits(regmap, DIG_CTRL1_I2S_TDM_CH_TX, 0x02, 0x02);
}

static int wait_for_pde_state(struct wsa885x_i2c_priv *wsa885x,
							  int ps, int reg)
{
	int act_ps, cnt = 0, clock_valid;
	int rc = 0;

	do {
		usleep_range(1000, 1500);
		/* wait and read actual_PS */
		rc = regmap_read(wsa885x->regmap,
						 SMP_AMP_CTRL_STEREO_PDE23_ACT_PS,
						 &act_ps);

		if (rc == 0 && act_ps == ps)
			return rc;
	} while (++cnt < 5);

	regmap_read(wsa885x->regmap,
				SMP_AMP_CTRL_STEREO_CS21_CLOCK_VALID,
				&clock_valid);
	dev_err(wsa885x->component->dev,
			"simple amp ps%d request failed, func num 1 act_ps %d, cs_valid:%d\n",
			ps, act_ps, clock_valid);
	return -EINVAL;
}

static int codec_hw_params(struct snd_pcm_substream *substream,
						   struct snd_pcm_hw_params *params,
						   struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct wsa885x_i2c_priv *wsa885x = snd_soc_component_get_drvdata(component);
	uint8_t value, cs21_sample_rate_idx, cs24_sample_rate_idx;
	int open_count = 0;

	dev_dbg(wsa885x->dev, "%s: HW Params called with sampling rate as %d\n", __func__,
				params_rate(params));

	open_count = atomic_read(&wsa885x->open_count);
	if (open_count > 1)
		return 0;

	wsa885x->sample_rate = params_rate(params);
	switch (wsa885x->sample_rate) {
	case 8000:
		value = 0x00;
		cs21_sample_rate_idx = WSA885X_RX_RATE_8000HZ;
		cs24_sample_rate_idx = WSA885X_VI_RATE_8000HZ;
		break;
	case 16000:
		value = 0x01;
		cs21_sample_rate_idx = WSA885X_RX_RATE_16000HZ;
		cs24_sample_rate_idx = WSA885X_VI_RATE_16000HZ;
		break;
	case 32000:
		value = 0x02;
		cs21_sample_rate_idx = WSA885X_RX_RATE_32000HZ;
		cs24_sample_rate_idx = WSA885X_VI_RATE_48000HZ;
		break;
	case 44100:
		value = 0x03;
		cs21_sample_rate_idx = WSA885X_RX_RATE_44100HZ;
		cs24_sample_rate_idx = WSA885X_VI_RATE_44100HZ;
		break;
	case 48000:
		value = 0x03;
		cs21_sample_rate_idx = WSA885X_RX_RATE_48000HZ;
		cs24_sample_rate_idx = WSA885X_VI_RATE_48000HZ;
		break;
	case 88200:
	case 96000:
		value = 0x04;
		cs21_sample_rate_idx = WSA885X_RX_RATE_96000HZ;
		cs24_sample_rate_idx = WSA885X_VI_RATE_96000HZ;
		break;
	case 176400:
	case 192000:
		value = 0x05;
		cs21_sample_rate_idx = WSA885X_RX_RATE_192000HZ;
		cs24_sample_rate_idx = WSA885X_VI_RATE_192000HZ;
		break;
	case 352800:
	case 384000:
		value = 0x06;
		cs21_sample_rate_idx = WSA885X_RX_RATE_384000HZ;
		cs24_sample_rate_idx = WSA885X_VI_RATE_384000HZ;
		break;
	default: // need to add 24 and 22.5
		dev_err(component->dev, "sampling rate %d is not supported\n",
				params_rate(params));
		return -EINVAL;
	}

	regmap_update_bits(wsa885x->regmap, DIG_CTRL1_I2S_CTL0, 0x1f,
				(value << 1) + 1); // only bits 1:4 + last bit
	regmap_write(wsa885x->regmap, DIG_CTRL1_I2S_RESET_CTL, 0x00);
	regmap_write(wsa885x->regmap, SMP_AMP_CTRL_STEREO_CS21_SAMPLERATEINDEX,
				 cs21_sample_rate_idx);
	regmap_write(wsa885x->regmap, SMP_AMP_CTRL_STEREO_CS24_SAMPLERATEINDEX,
				 cs24_sample_rate_idx);
	return 0;
}

static int codec_set_tdm_slot(struct snd_soc_dai *dai,
							  unsigned int tx_slot_mask,
							  unsigned int rx_slot_mask, int slots,
							  int slot_width)
{
	// Set TDM slot configuration
	// This is where you would configure the codec registers for TDM
	struct snd_soc_component *component = dai->component;
	struct wsa885x_i2c_priv *wsa885x =
		snd_soc_component_get_drvdata(component);

	dev_dbg(wsa885x->dev, "%s: TDM num_slots configured as %d\n", __func__, slots);

	if (atomic_inc_return(&wsa885x->open_count) > 1)
		return 0;

	regmap_update_bits(wsa885x->regmap, DIG_CTRL1_I2S_RESET_CTL, 0x01, 0x01);
	if (wsa885x->rx_slot_mask == 0b11) {
		regmap_update_bits(wsa885x->regmap, DIG_CTRL1_I2S_CFG0_TDM_TX,
						   0x01,
						   0x01); // slot0 isense0
		regmap_update_bits(wsa885x->regmap, DIG_CTRL1_I2S_CFG0_TDM_TX,
						   0x20, 0x20); // slot1 isense1
		regmap_update_bits(wsa885x->regmap, DIG_CTRL1_I2S_CFG1_TDM_TX,
						   0x05,
						   0x05); // slot3 cps0
		regmap_update_bits(wsa885x->regmap, DIG_CTRL1_I2S_CFG1_TDM_TX,
						   0x60, 0x60); // slot4 cps1
		reg_update_sequence(wsa885x->regmap);
		regmap_update_bits(wsa885x->regmap, DIG_CTRL1_I2S_TDM_CH_TX,
						   0x04, 0x04);
		regmap_update_bits(wsa885x->regmap, DIG_CTRL1_I2S_TDM_CH_TX,
						   0x08, 0x08);
	} else if (wsa885x->rx_slot_mask == 0b01) {
		regmap_update_bits(wsa885x->regmap, DIG_CTRL1_I2S_CFG0_TDM_TX,
						   0x01,
						   0x01); // slot0 isense0
		regmap_update_bits(wsa885x->regmap, DIG_CTRL1_I2S_CFG0_TDM_TX,
						   0x50, 0x50); // slot1 cps0
		reg_update_sequence(wsa885x->regmap);
	} else if (wsa885x->rx_slot_mask == 0b10) {
		regmap_update_bits(wsa885x->regmap, DIG_CTRL1_I2S_CFG0_TDM_TX,
						   0x02,
						   0x02); // slot0 isense0
		regmap_update_bits(wsa885x->regmap, DIG_CTRL1_I2S_CFG0_TDM_TX,
						   0x60, 0x60); // slot1 cps0
		reg_update_sequence(wsa885x->regmap);
	}
	regmap_update_bits(wsa885x->regmap, DIG_CTRL1_I2S_CTL0, 0x01, 0x01);
	regmap_update_bits(wsa885x->regmap, DIG_CTRL1_I2S_RESET_CTL, 0x01,
					   0x00);

	return 0;
}

static int codec_set_sysclk(struct snd_soc_dai *dai, int clk_id,
							unsigned int freq, int dir)
{
	struct snd_soc_component *component = dai->component;
	struct wsa885x_i2c_priv *wsa885x =
		snd_soc_component_get_drvdata(component);
	uint8_t pll_div;
	unsigned int status;
	int i, open_count = 0;

	dev_dbg(wsa885x->dev, "%s: Freq set as  %d\n", __func__, freq);

	open_count = atomic_read(&wsa885x->open_count);
	if (open_count > 1)
		return 0;

	pll_div = CLK_RATE_FIXED / freq;
	regmap_write(wsa885x->regmap, ANA_TOP_BG_TVP_OVRD_CTL, 0x03);
	regmap_write(wsa885x->regmap, DIG_CTRL0_SYS_CLK_SEL, 0x04);
	regmap_write(wsa885x->regmap, ANA_TOP_PLL_LOOPFILT_0, 0xB4);
	regmap_write(wsa885x->regmap, ANA_TOP_PLL_VCO_CTL, 0x00);
	regmap_write(wsa885x->regmap, ANA_TOP_PLL_OVRD_CTL, 0x00);
	regmap_write(wsa885x->regmap, ANA_PLL_DIV_CTL_0, pll_div);
	regmap_write(wsa885x->regmap, DIG_CTRL0_CLK_SOURCE_ENABLE, 0x02);

	usleep_range(15000, 15100);
	regmap_read(wsa885x->regmap, ANA_TOP_PLL_STATUS_0, &status);
	if (status & 0b01)
		dev_dbg(wsa885x->component->dev, "PLL is locked");

	regmap_write(wsa885x->regmap, DIG_CTRL0_SYS_CLK_SEL, 0x00);
	regmap_write(wsa885x->regmap, DIG_CTRL0_POWER_FSM_CTL1, 0x01);

	for (i = 0; i < wsa885x->init_table_size / 2; i++) {
		regmap_write(wsa885x->regmap, wsa885x->init_table[2 * i],
					  wsa885x->init_table[2 * i + 1]);
	}
	return 0;
}

static int codec_mute_stream(struct snd_soc_dai *dai, int mute, int stream)
{
	struct wsa885x_i2c_priv *wsa885x = snd_soc_dai_get_drvdata(dai);
	int ret = 0, ps0 = 0, ps3 = 3;

	dev_dbg(wsa885x->dev, "%s: Stream is %s\n", __func__, mute ? "muted" : "unmuted");
	if (mute) {
		regmap_write(wsa885x->regmap, SMP_AMP_CTRL_STEREO_PDE23_REQ_PS,
					 0x03);
		ret = wait_for_pde_state(wsa885x, ps3, SMP_AMP_CTRL_STEREO_PDE23_ACT_PS);
		if (!ret) {
			dev_err(wsa885x->component->dev,
			"success! function 1, actual ps %d\n",
					ps3);
		}
	} else {
		regmap_write(wsa885x->regmap, DIG_CTRL0_PA_FSM_CTL, 0x00);
		regmap_write(wsa885x->regmap, SMP_AMP_CTRL_STEREO_OT23_USAGE,
					 wsa885x->usage_mode);
		regmap_write(wsa885x->regmap, SMP_AMP_CTRL_STEREO_IT21_CLUSERINDEX,
					 0x01);
		regmap_write(wsa885x->regmap, SMP_AMP_CTRL_STEREO_PPU21_POSTURENUMBER,
					 0x01);
		regmap_write(wsa885x->regmap,
					 SMP_AMP_CTRL_STEREO_FU21_CH_VOL_CH2X0_MSB, 0x00);
		regmap_write(wsa885x->regmap,
					 SMP_AMP_CTRL_STEREO_FU21_CH_VOL_CH2X0_LSB, 0x00);
		regmap_write(wsa885x->regmap,
					 SMP_AMP_CTRL_STEREO_FU21_CH_VOL_CH2X1_MSB, 0x00);
		regmap_write(wsa885x->regmap,
					 SMP_AMP_CTRL_STEREO_FU21_CH_VOL_CH2X1_LSB, 0x00);
		regmap_write(wsa885x->regmap, DIG_CTRL0_SDCA_COMMIT, 0x01);
		regmap_write(wsa885x->regmap, SMP_AMP_CTRL_STEREO_PDE23_REQ_PS,
					 0x00);
		ret = wait_for_pde_state(wsa885x, ps0, SMP_AMP_CTRL_STEREO_PDE23_ACT_PS);
		if (!ret)
			dev_dbg(wsa885x->component->dev,
					"success! function 1, actual ps %d\n", ps0);
		else {
			dev_err(wsa885x->component->dev,
					"PS0 request failed\n");
			goto exit;
		}

		if (wsa885x->rx_slot_mask == 0b11) {
			regmap_write(wsa885x->regmap, DIG_CTRL0_PA_FSM_CTL,
					0x03);
		} else if (wsa885x->rx_slot_mask == 0b01) {
			regmap_write(wsa885x->regmap, DIG_CTRL0_PA_FSM_CTL,
					0x01);
		} else if (wsa885x->rx_slot_mask == 0b10) {
			regmap_write(wsa885x->regmap, DIG_CTRL0_PA_FSM_CTL,
					0b10);
			regmap_write(wsa885x->regmap, DIG_CTRL1_I2S_TDM_CH_RX,
					0b01);
		}

		regmap_write(wsa885x->regmap,
					 SMP_AMP_CTRL_STEREO_FU21_MUTE_CH2X0, 0x00);
		regmap_write(wsa885x->regmap,
					 SMP_AMP_CTRL_STEREO_FU21_MUTE_CH2X1, 0x00);
		regmap_write(wsa885x->regmap, DIG_CTRL0_SDCA_COMMIT, 0x01);
	}
exit:
	return ret;
}

static int codec_mute_stream_virt_c1(struct snd_soc_dai *dai, int mute, int stream)
{
	struct wsa885x_i2c_priv *wsa885x = snd_soc_dai_get_drvdata(dai);
	int ret = 0, ps0 = 0, ps3 = 3, open_count = 0;

	dev_dbg(wsa885x->dev, "%s: Stream is %s\n", __func__, mute ? "muted" : "unmuted");
	if (mute) {
		open_count = atomic_dec_return(&wsa885x->open_count);
		if (open_count > 0) {
			regmap_update_bits(wsa885x->regmap, DIG_CTRL0_PA_FSM_CTL, 0b01, 0x00);
			return 0;
		}

		regmap_write(wsa885x->regmap, DIG_CTRL0_PA_FSM_CTL, 0x00);
		regmap_write(wsa885x->regmap, SMP_AMP_CTRL_STEREO_PDE23_REQ_PS,
					 0x03);
		ret = wait_for_pde_state(wsa885x, ps3, SMP_AMP_CTRL_STEREO_PDE23_ACT_PS);
		if (!ret) {
			dev_err(wsa885x->component->dev,
			"success! function 1, actual ps %d\n",
					ps3);
		}
	} else {
		open_count = atomic_read(&wsa885x->open_count);
		if (open_count > 1) {
			regmap_update_bits(wsa885x->regmap, DIG_CTRL0_PA_FSM_CTL, 0b01, 0b01);
			return 0;
		}

		regmap_write(wsa885x->regmap, DIG_CTRL0_PA_FSM_CTL, 0x00);
		regmap_write(wsa885x->regmap, SMP_AMP_CTRL_STEREO_OT23_USAGE,
					 wsa885x->usage_mode);
		regmap_write(wsa885x->regmap, SMP_AMP_CTRL_STEREO_IT21_CLUSERINDEX,
					 0x01);
		regmap_write(wsa885x->regmap, SMP_AMP_CTRL_STEREO_PPU21_POSTURENUMBER,
					 0x01);
		regmap_write(wsa885x->regmap,
					 SMP_AMP_CTRL_STEREO_FU21_CH_VOL_CH2X0_MSB, 0x00);
		regmap_write(wsa885x->regmap,
					 SMP_AMP_CTRL_STEREO_FU21_CH_VOL_CH2X0_LSB, 0x00);
		regmap_write(wsa885x->regmap,
					 SMP_AMP_CTRL_STEREO_FU21_CH_VOL_CH2X1_MSB, 0x00);
		regmap_write(wsa885x->regmap,
					 SMP_AMP_CTRL_STEREO_FU21_CH_VOL_CH2X1_LSB, 0x00);
		regmap_write(wsa885x->regmap, DIG_CTRL0_SDCA_COMMIT, 0x01);
		regmap_write(wsa885x->regmap, SMP_AMP_CTRL_STEREO_PDE23_REQ_PS,
					 0x00);
		ret = wait_for_pde_state(wsa885x, ps0, SMP_AMP_CTRL_STEREO_PDE23_ACT_PS);
		if (!ret)
			dev_dbg(wsa885x->component->dev,
					"success! function 1, actual ps %d\n", ps0);
		else {
			dev_err(wsa885x->component->dev,
					"PS0 request failed\n");
			goto exit;
		}

		regmap_write(wsa885x->regmap, DIG_CTRL0_PA_FSM_CTL, 0b01);

		regmap_write(wsa885x->regmap,
					 SMP_AMP_CTRL_STEREO_FU21_MUTE_CH2X0, 0x00);
		regmap_write(wsa885x->regmap,
					 SMP_AMP_CTRL_STEREO_FU21_MUTE_CH2X1, 0x00);
		regmap_write(wsa885x->regmap, DIG_CTRL0_SDCA_COMMIT, 0x01);
	}
exit:
	return ret;
}

static int codec_mute_stream_virt_c2(struct snd_soc_dai *dai, int mute, int stream)
{
	struct wsa885x_i2c_priv *wsa885x = snd_soc_dai_get_drvdata(dai);
	int ret = 0, ps0 = 0, ps3 = 3, open_count = 0;

	dev_dbg(wsa885x->dev, "%s: Stream is %s\n", __func__, mute ? "muted" : "unmuted");
	if (mute) {
		open_count = atomic_dec_return(&wsa885x->open_count);
		if (open_count > 0) {
			regmap_update_bits(wsa885x->regmap, DIG_CTRL0_PA_FSM_CTL, 0b10, 0x00);
			return 0;
		}
		regmap_write(wsa885x->regmap, DIG_CTRL0_PA_FSM_CTL, 0x00);
		regmap_write(wsa885x->regmap, SMP_AMP_CTRL_STEREO_PDE23_REQ_PS,
					 0x03);
		ret = wait_for_pde_state(wsa885x, ps3, SMP_AMP_CTRL_STEREO_PDE23_ACT_PS);
		if (!ret) {
			dev_err(wsa885x->component->dev,
			"success! function 1, actual ps %d\n",
					ps3);
		}
	} else {
		open_count = atomic_read(&wsa885x->open_count);
		if (open_count > 1) {
			regmap_update_bits(wsa885x->regmap, DIG_CTRL0_PA_FSM_CTL, 0b10, 0b10);
			return 0;
		}

		regmap_write(wsa885x->regmap, SMP_AMP_CTRL_STEREO_OT23_USAGE,
					 wsa885x->usage_mode);
		regmap_write(wsa885x->regmap, SMP_AMP_CTRL_STEREO_IT21_CLUSERINDEX,
					 0x01);
		regmap_write(wsa885x->regmap, SMP_AMP_CTRL_STEREO_PPU21_POSTURENUMBER,
					 0x01);
		regmap_write(wsa885x->regmap,
					 SMP_AMP_CTRL_STEREO_FU21_CH_VOL_CH2X0_MSB, 0x00);
		regmap_write(wsa885x->regmap,
					 SMP_AMP_CTRL_STEREO_FU21_CH_VOL_CH2X0_LSB, 0x00);
		regmap_write(wsa885x->regmap,
					 SMP_AMP_CTRL_STEREO_FU21_CH_VOL_CH2X1_MSB, 0x00);
		regmap_write(wsa885x->regmap,
					 SMP_AMP_CTRL_STEREO_FU21_CH_VOL_CH2X1_LSB, 0x00);
		regmap_write(wsa885x->regmap, DIG_CTRL0_SDCA_COMMIT, 0x01);
		regmap_write(wsa885x->regmap, SMP_AMP_CTRL_STEREO_PDE23_REQ_PS,
					 0x00);
		ret = wait_for_pde_state(wsa885x, ps0, SMP_AMP_CTRL_STEREO_PDE23_ACT_PS);
		if (!ret)
			dev_dbg(wsa885x->component->dev,
					"success! function 1, actual ps %d\n", ps0);
		else {
			dev_err(wsa885x->component->dev,
					"PS0 request failed\n");
			goto exit;
		}

		regmap_write(wsa885x->regmap, DIG_CTRL0_PA_FSM_CTL, 0b10);

		regmap_write(wsa885x->regmap,
					 SMP_AMP_CTRL_STEREO_FU21_MUTE_CH2X0, 0x00);
		regmap_write(wsa885x->regmap,
					 SMP_AMP_CTRL_STEREO_FU21_MUTE_CH2X1, 0x00);
		regmap_write(wsa885x->regmap, DIG_CTRL0_SDCA_COMMIT, 0x01);
	}
exit:
	return ret;
}

static int codec_hw_free(struct snd_pcm_substream *substream,
						 struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct wsa885x_i2c_priv *wsa885x =
		snd_soc_component_get_drvdata(component);
	int open_count = 0;

	dev_dbg(wsa885x->dev, "%s: HW Free, resetting I2S registers\n", __func__);

	open_count = atomic_read(&wsa885x->open_count);
	if (open_count > 0)
		return 0;

	wsa885x->rx_slot_mask = 0x00;
	/* Reset I2S register in any case */
	regmap_write(wsa885x->regmap, DIG_CTRL1_I2S_RESET_CTL, 0x00);
	regmap_write(wsa885x->regmap, DIG_CTRL1_I2S_CFG0_TDM_TX, 0x00);
	regmap_write(wsa885x->regmap, DIG_CTRL1_I2S_CFG1_TDM_TX, 0x00);
	regmap_write(wsa885x->regmap, DIG_CTRL1_I2S_TDM_CTL1, 0x05);
	regmap_write(wsa885x->regmap, DIG_CTRL1_I2S_TDM_CTL0, 0x00);
	regmap_write(wsa885x->regmap, DIG_CTRL1_I2S_TDM_CH_TX, 0x00);
	regmap_write(wsa885x->regmap, DIG_CTRL1_I2S_CTL0, 0x06);

	/* Reset Clock */
	regmap_write(wsa885x->regmap, DIG_CTRL0_CLK_SOURCE_ENABLE, 0x00);
	regmap_write(wsa885x->regmap, ANA_TOP_BG_TVP_OVRD_CTL, 0x00);

	return 0;
}

static const struct snd_soc_dai_ops wsa885x_i2c_dai_ops[] = {
	{
		.hw_params = codec_hw_params,
		.set_tdm_slot = codec_set_tdm_slot,
		.set_sysclk = codec_set_sysclk,
		.mute_stream = codec_mute_stream,
		.hw_free = codec_hw_free,
	},
	{
		.hw_params = codec_hw_params,
		.set_tdm_slot = codec_set_tdm_slot,
		.set_sysclk = codec_set_sysclk,
		.mute_stream = codec_mute_stream_virt_c1,
		.hw_free = codec_hw_free,
	},
	{
		.hw_params = codec_hw_params,
		.set_tdm_slot = codec_set_tdm_slot,
		.set_sysclk = codec_set_sysclk,
		.mute_stream = codec_mute_stream_virt_c2,
		.hw_free = codec_hw_free,
	},
};

static struct snd_soc_dai_driver wsa885x_i2c_dai[] = {
	{
		.name = "wsa885x_dai_drv",
		.playback = {
			.stream_name = "WSA885X I2C TDM Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE |
					   SNDRV_PCM_FMTBIT_S32_LE,
		},
		.ops = &wsa885x_i2c_dai_ops[0],
	},
	{
		.name = "wsa885x_dai_drv_virt_c1",
		.playback = {
			.stream_name = "WSA885X I2C VIRTUAL C1 TDM Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE |
					   SNDRV_PCM_FMTBIT_S32_LE,
		},
		.ops = &wsa885x_i2c_dai_ops[1],
	},
	{
		.name = "wsa885x_dai_drv_virt_c2",
		.playback = {
			.stream_name = "WSA885X I2C VIRTUAL C2 TDM Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE |
					   SNDRV_PCM_FMTBIT_S32_LE,
		},
		.ops = &wsa885x_i2c_dai_ops[2],
	},
};

static int wsa885x_gpio_set(struct wsa885x_i2c_priv *wsa885x, bool val)
{
	int ret = 0;

	if (val)
		ret = gpiod_direction_output(wsa885x->sd_n, 1);
	else
		ret = gpiod_direction_output(wsa885x->sd_n, 0);

	if (ret < 0) {
		dev_err_ratelimited(wsa885x->dev, "%s: failed to set GPIO: %d\n", __func__,
							ret);
	}
	return ret;
}

static void wsa885x_gpio_powerdown(void *data)
{
	gpiod_direction_output(data, 1);
}

static struct snd_soc_dai_driver *get_dai_driver(struct device *dev)
{
	struct snd_soc_dai_driver *dai_drv = NULL;

	dai_drv = devm_kzalloc(dev, ARRAY_SIZE(wsa885x_i2c_dai) * sizeof(struct snd_soc_dai_driver),
						   GFP_KERNEL);
	if (!dai_drv)
		return NULL;

	memcpy((void *)dai_drv, &wsa885x_i2c_dai,
		   ARRAY_SIZE(wsa885x_i2c_dai) * sizeof(struct snd_soc_dai_driver));

	return dai_drv;
}

static bool wsa885x_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case ANA_TOP_PLL_STATUS_0:
	case ANA_TOP_PLL_STATUS_1:
	case SMP_AMP_CTRL_STEREO_PDE23_ACT_PS:
	case SMP_AMP_CTRL_STEREO_CS21_CLOCK_VALID:
	case WSA885X_INTR_STATUS0:
	case WSA885X_INTR_STATUS0 + 1:
	case WSA885X_INTR_STATUS0 + 2:
	case WSA885X_INTR_CLEAR0:
	case WSA885X_INTR_CLEAR0 + 1:
	case WSA885X_INTR_CLEAR0 + 2:
		return true;
	default:
		return false;
	}
}

static bool wsa885x_readable_register(struct device *dev, unsigned int reg)
{
	if (reg >= 0 && reg <= 0x88ff)
		return true;
	return false;
}

static bool wsa885x_writeable_register(struct device *dev, unsigned int reg)
{
	if (reg >= 0 && reg <= 0x88ff) {
		if (reg == ANA_TOP_PLL_STATUS_0 ||
			reg == WSA885X_INTR_STATUS0 ||
			reg == WSA885X_INTR_STATUS0 + 1 ||
			reg == WSA885X_INTR_STATUS0 + 2 ||
			reg == SMP_AMP_CTRL_STEREO_PDE23_ACT_PS ||
			reg == SMP_AMP_CTRL_STEREO_CS21_CLOCK_VALID)
			return false;
		return true;
	}
	return false;
}

static const struct regmap_config regmap_cfg = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0x88FF,
	.ranges = regmap_ranges,
	.num_ranges = ARRAY_SIZE(regmap_ranges),
	.reg_defaults = codec_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(codec_reg_defaults),
	.volatile_reg = wsa885x_volatile_register,
	.writeable_reg = wsa885x_writeable_register,
	.readable_reg = wsa885x_readable_register,
	.cache_type = REGCACHE_MAPLE,
	.use_single_read = true,
	.use_single_write = true,
};

static int wsa885x_component_probe(struct snd_soc_component *component)
{
	struct wsa885x_i2c_priv *wsa885x =
		snd_soc_component_get_drvdata(component);
	wsa885x->component = component;
	snd_soc_component_init_regmap(component, wsa885x->regmap);
	// enable interrupts
	regmap_write(wsa885x->regmap, DIG_CTRL1_SPMI_PAD_GPIO2_CTL, 0x2e); // check again
	regmap_write(wsa885x->regmap, DIG_CTRL1_INTR_MODE, 0x01);
	regmap_write(wsa885x->regmap, DIG_CTRL1_PIN_CT, 0x04);
	regmap_write(wsa885x->regmap, WSA885X_INTR_MASK0, 0x00);
	regmap_write(wsa885x->regmap, WSA885X_INTR_MASK0 + 1, 0x00);
	regmap_write(wsa885x->regmap, WSA885X_INTR_MASK0 + 2, 0x78);

	return 0;
}

static void wsa885x_component_remove(struct snd_soc_component *component)
{
	struct wsa885x_i2c_priv *wsa885x =
		snd_soc_component_get_drvdata(component);

	if (!wsa885x)
		return;

	devm_regmap_qti_debugfs_unregister(wsa885x->regmap);
	snd_soc_component_exit_regmap(component);
}

static void wsa885x_regulator_disable(void *data)
{
	regulator_bulk_disable(SUPPLIES_NUM, data);
}

/* Get and Put the usage Modes */
static int wsa885x_i2c_usage_modes_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct wsa885x_i2c_priv *wsa885x_i2c =
		snd_soc_component_get_drvdata(component);

	if (!wsa885x_i2c)
		return -EINVAL;

	ucontrol->value.integer.value[0] = wsa885x_i2c->usage_mode;

	return 0;
}

static int wsa885x_i2c_usage_modes_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct wsa885x_i2c_priv *wsa885x_i2c =
		snd_soc_component_get_drvdata(component);

	if (!wsa885x_i2c)
		return -EINVAL;

	wsa885x_i2c->usage_mode = ucontrol->value.integer.value[0];

	dev_dbg(component->dev, "%s: Usage mode:%d\n", __func__,
			wsa885x_i2c->usage_mode);

	return 0;
}

/* Get and Put for RX Select Channel */
static int wsa885x_i2c_rx_slot_mask_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct wsa885x_i2c_priv *wsa885x_i2c =
		snd_soc_component_get_drvdata(component);

	ucontrol->value.enumerated.item[0] = wsa885x_i2c->rx_slot_mask;

	return 0;
}

static int wsa885x_i2c_rx_slot_mask_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct wsa885x_i2c_priv *wsa885x_i2c =
		snd_soc_component_get_drvdata(component);

	wsa885x_i2c->rx_slot_mask = ucontrol->value.enumerated.item[0];

	dev_dbg(component->dev, "%s: Rx channel:%d select\n", __func__,
			wsa885x_i2c->rx_slot_mask);
	return 0;
}

static const struct snd_kcontrol_new wsa885x_snd_controls[] = {
	SOC_SINGLE_EXT("OT23 Usage Mode", SND_SOC_NOPM, 0, 8, 0,
				   wsa885x_i2c_usage_modes_get,
				   wsa885x_i2c_usage_modes_put),

	SOC_SINGLE_EXT("Rx Slot Mask", SND_SOC_NOPM, 0, 4, 0,
				   wsa885x_i2c_rx_slot_mask_get,
				   wsa885x_i2c_rx_slot_mask_put),
};

static const struct snd_soc_component_driver wsa885x_i2c_component = {
	.name = NULL,
	.probe = wsa885x_component_probe,
	.remove = wsa885x_component_remove,
	.controls = wsa885x_snd_controls,
	.num_controls = ARRAY_SIZE(wsa885x_snd_controls),
	.dapm_widgets = NULL, // Add DAPM widgets if needed
	.num_dapm_widgets = 0,
	.dapm_routes = NULL, // Add DAPM routes if needed
	.num_dapm_routes = 0,
};

static int handle_wsa885x_i2c_irq(int irq, void *data)
{
	struct wsa885x_i2c_priv *wsa885x = data;

	// Handle the interrupt based on the IRQ number
	switch (irq) {
	case WSA885X_IRQ_INT_SAF2WAR:
	case WSA885X_IRQ_INT_WAR2SAF:
	case WSA885X_IRQ_INT_PA0_OCP:
	case WSA885X_IRQ_INT_PA1_OCP:
	case WSA885X_IRQ_INT_CLIP0:
	case WSA885X_IRQ_INT_CLIP1:
	case WSA885X_IRQ_INT_CLK_WD:
	case WSA885X_IRQ_INT_BOP:
	case WSA885X_IRQ_INT_UVLO:
	case WSA885X_IRQ_INT_PCM_DATA0_DC:
	case WSA885X_IRQ_INT_PCM_DATA1_DC:
	case WSA885X_IRQ_INT_PLL_UNLOCKED:
	case WSA885X_IRQ_INT_PROT_MODE_CHANGE:
	case WSA885X_IRQ_INT_PB_CLOCK_VALID:
	case WSA885X_IRQ_INT_SENSE_CLOCK_VALID:
		break;
	case WSA885X_IRQ_INT_PCM_DATA0_WD:
	case WSA885X_IRQ_INT_PCM_DATA1_WD:
		if (!wsa885x)
			return IRQ_NONE;
		if (irq == WSA885X_IRQ_INT_PCM_DATA0_WD) {
			regmap_update_bits(wsa885x->regmap, 0x84A0,
								0x04, 0x00);
			regmap_update_bits(wsa885x->regmap, 0x84A0,
								0x04, 0x01);
		} else {
			regmap_update_bits(wsa885x->regmap, 0x84A4,
								0x04, 0x00);
			regmap_update_bits(wsa885x->regmap, 0x84A4,
								0x04, 0x01);
		}
		break;
	case WSA885X_IRQ_INT_PA0_FSM_ERR:
	case WSA885X_IRQ_INT_PA1_FSM_ERR:
	case WSA885X_IRQ_INT_MAIN_FSM_ERR:
		if (!wsa885x)
			return IRQ_NONE;

		if (irq == WSA885X_IRQ_INT_MAIN_FSM_ERR) {
			regmap_update_bits(wsa885x->regmap, WSA885X_POWER_FSM_CTL0,
						0x08, 0x00);
			regmap_update_bits(wsa885x->regmap, WSA885X_POWER_FSM_CTL0,
						0x08, 0x08);
			regmap_update_bits(wsa885x->regmap, WSA885X_POWER_FSM_CTL0,
						0x08, 0x00);
		} else if (irq == WSA885X_IRQ_INT_PA0_FSM_ERR) {
			regmap_update_bits(wsa885x->regmap, WSA885X_PA0_FSM_CTL0,
								0x04, 0x00);
			regmap_update_bits(wsa885x->regmap, WSA885X_PA0_FSM_CTL0,
								0x04, 0x04);
			regmap_update_bits(wsa885x->regmap, WSA885X_PA0_FSM_CTL0,
								0x04, 0x00);
		} else if (irq == WSA885X_IRQ_INT_PA1_FSM_ERR) {
			regmap_update_bits(wsa885x->regmap, WSA885X_PA1_FSM_CTL0,
								0x04, 0x00);
			regmap_update_bits(wsa885x->regmap, WSA885X_PA1_FSM_CTL0,
								0x04, 0x04);
			regmap_update_bits(wsa885x->regmap, WSA885X_PA1_FSM_CTL0,
								0x04, 0x00);
		}
		break;
	default:
		dev_warn(wsa885x->dev, "Unhandled IRQ: %d\n", irq);
		return IRQ_NONE;
	}

	pr_err_ratelimited("%s: handled %s interrupt\n", __func__,
							wsa885x_irq_names[irq]);

	return IRQ_HANDLED;
}

static irqreturn_t wsa885x_interrupt_handler(int irq, void *data)
{
	unsigned int status[NUM_REGS];
	int i, bit, ret = IRQ_NONE;
	int irq_num;
	struct wsa885x_i2c_priv *wsa885x = data;
	int status_reg[NUM_REGS] = {
			WSA885X_INTR_STATUS0,
			WSA885X_INTR_STATUS0 + 1,
			WSA885X_INTR_STATUS0 + 2
	};
	int clear_reg[NUM_REGS] = {
			WSA885X_INTR_CLEAR0,
			WSA885X_INTR_CLEAR0 + 1,
			WSA885X_INTR_CLEAR0 + 2
	};

	pr_debug("%s: interrupt for irq = %d triggered\n", __func__, irq);
	/* Read all status registers */
	for (i = 0; i < NUM_REGS; i++) {
		ret = regmap_read(wsa885x->regmap, status_reg[i], &status[i]);
		if (ret) {
			dev_err(wsa885x->dev, "Failed to read status_reg[%d] (0x%x): %d\n",
						i, status_reg[i], ret);
			return IRQ_NONE;
		}
	}

	/* Handle each interrupt bit */
	for (i = 0; i < NUM_REGS; i++) {
		for (bit = 0; bit < 8; bit++) {
			if (status[i] & (1 << bit)) {
				irq_num = i * 8 + bit;
				ret = handle_wsa885x_i2c_irq(irq_num, wsa885x);
				/* Clear the interrupt by writing 1 to the bit */
				regmap_update_bits(wsa885x->regmap,
								   clear_reg[i],
								   (1 << bit),
								   (1 << bit));
				/* Optionally clear again to 0 if needed */
				regmap_update_bits(wsa885x->regmap,
								   clear_reg[i],
								   (1 << bit), 0);
			}
		}
	}

	return ret;
}

int wsa885x_register_irq(struct wsa885x_i2c_priv *wsa885x)
{
	int ret;

	// Get the IRQ number for the GPIO
	int irq_number = gpiod_to_irq(wsa885x->intr_pin);

	if (irq_number < 0) {
		pr_err("Failed to get IRQ number\n");
		gpiod_put(wsa885x->intr_pin);
		return irq_number;
	}

	ret = devm_request_threaded_irq(wsa885x->dev, irq_number, NULL,
					wsa885x_interrupt_handler,
		IRQF_SHARED | IRQF_ONESHOT | IRQF_TRIGGER_FALLING, "WSA885X I2C Interrupt",
					wsa885x);
	if (ret) {
		dev_err(wsa885x->dev, "Failed to request IRQ for wsa885x i2c\n");
		gpiod_put(wsa885x->intr_pin);
		return ret;
	}
	return ret;
}

static int wsa885x_i2c_probe(struct i2c_client *client)
{
	struct wsa885x_i2c_priv *wsa885x;
	const char *wsa885x_i2c_codec_name_of = NULL;
	struct snd_soc_component_driver *component_drv = NULL;
	int ret, i;
	struct device *dev = &client->dev;

	wsa885x = devm_kzalloc(&client->dev, sizeof(struct wsa885x_i2c_priv),
						   GFP_KERNEL);
	if (!wsa885x)
		return -ENOMEM;

	wsa885x->client = client;
	wsa885x->dev = dev;
	wsa885x->regmap = devm_regmap_init_i2c(client, &regmap_cfg);
	atomic_set(&wsa885x->open_count, 0);

	if (IS_ERR(wsa885x->regmap))
		return PTR_ERR(wsa885x->regmap);

	wsa885x->dev = dev;

	wsa885x->init_table_size =
		of_property_count_u32_elems(dev->of_node, "wsa885x-init-table");

	if (wsa885x->init_table_size <= 0) {
		dev_err(dev, "%s: Failed to count elements from init table\n",
				__func__);
		return -EINVAL;
	}

	if (wsa885x->init_table_size % 2 != 0) {
		dev_err(dev, "%s: Invalid number of elements in init table\n",
				__func__);
		return -EINVAL;
	}

	wsa885x->init_table = devm_kzalloc(
		dev, wsa885x->init_table_size * sizeof(u32), GFP_KERNEL);
	if (!wsa885x->init_table)
		return -ENOMEM;

	if (of_property_read_u32_array(dev->of_node, "wsa885x-init-table",
					wsa885x->init_table,
					wsa885x->init_table_size)) {
		dev_err(dev,
				"%s: Failed to read wsa885x-initialization-table\n",
				__func__);
		return -EINVAL;
	}
	for (i = 0; i < SUPPLIES_NUM; i++)
		wsa885x->supplies[i].supply = supply_name[i];

	ret = devm_regulator_bulk_get(dev, SUPPLIES_NUM, wsa885x->supplies);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get regulators\n");

	ret = regulator_bulk_enable(SUPPLIES_NUM, wsa885x->supplies);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to enable regulators\n");

	ret = devm_add_action_or_reset(dev, wsa885x_regulator_disable,
								   wsa885x->supplies);
	if (ret) {
		dev_err(dev, "failed to devm_add_action_or_reset, %d\n", ret);
		return ret;
	}

	wsa885x->sd_n =
		devm_gpiod_get_optional(dev, "powerdown", GPIOD_OUT_HIGH);
	if (IS_ERR(wsa885x->sd_n))
		return dev_err_probe(dev, PTR_ERR(wsa885x->sd_n),
							 "Shutdown Control GPIO not found\n");

	ret = wsa885x_gpio_set(wsa885x, false);
	if (ret != 0)
		return ret;
	/*
	 * Add 5msec delay to provide sufficient time for
	 * soundwire auto enumeration of slave devices as
	 * per HW requirement.
	 */

	ret = devm_add_action_or_reset(dev, wsa885x_gpio_powerdown,
								   wsa885x->sd_n);
	if (ret) {
		dev_err(dev, "failed to devm_add_action_or_reset, %d\n", ret);
		return ret;
	}

	ret = of_property_read_string(dev->of_node, "qcom,codec-name",
								  &wsa885x_i2c_codec_name_of);
	if (ret) {
		dev_err(dev, "Looking up %s property in node %s failed\n",
				"qcom,codec-name", dev->of_node->full_name);
		goto err;
	}

	component_drv = devm_kzalloc(dev, sizeof(*component_drv), GFP_KERNEL);
	if (!component_drv) {
		ret = -ENOMEM;
		goto err;
	}

	memcpy((void *)component_drv, &wsa885x_i2c_component,
		   sizeof(*component_drv));

	component_drv->name =
		devm_kstrdup(dev, wsa885x_i2c_codec_name_of, GFP_KERNEL);
	if (!component_drv->name) {
		ret = -ENOMEM;
		goto err;
	}
	wsa885x->driver = component_drv;

	wsa885x->dai_driver = get_dai_driver(dev);

	ret = devm_snd_soc_register_component(dev, wsa885x->driver, wsa885x->dai_driver,
						   ARRAY_SIZE(wsa885x_i2c_dai));
	if (ret) {
		dev_err(dev, "Codec component %s registration failed\n",
				wsa885x->driver->name);
	} else {
		dev_dbg(dev, "Codec component %s registration success!\n",
				wsa885x->driver->name);
		dev_dbg(dev, "Codec component:dai %s registration success!\n",
				wsa885x->dai_driver[0].name);
	}
	i2c_set_clientdata(client, wsa885x);
	devm_regmap_qti_debugfs_register(dev, wsa885x->regmap);

	wsa885x->intr_pin = devm_gpiod_get_optional(dev, "interrupt", GPIOD_IN);
	if (IS_ERR(wsa885x->intr_pin)) {
		ret = PTR_ERR(wsa885x->intr_pin);
		dev_err(dev, "Failed to get interrupt pin, %d\n", ret);
		goto err;
	}

	ret = wsa885x_register_irq(wsa885x);
	if (ret)
		dev_err(dev, "wsa885x irq registration failed with ERROR = %d\n", ret);

err:
	return 0;
}

static const struct of_device_id wsa885x_i2c_dt_match[] = {
	{
		.compatible = "qcom,wsa885x-i2c",
	},
	{}};

static const struct i2c_device_id wsa885x_id_i2c[] = {
	{"wsa885x_i2c", 0},
	{}};
MODULE_DEVICE_TABLE(i2c, wsa885x_id_i2c);

static struct i2c_driver i2c_slave_driver = {
	.driver = {
		.name = "wsa885x_i2c",
		.of_match_table = wsa885x_i2c_dt_match,
	},
	.probe = wsa885x_i2c_probe,
	.id_table = wsa885x_id_i2c,
};

module_i2c_driver(i2c_slave_driver);

MODULE_DESCRIPTION("Qualcomm Technologies, Inc");
MODULE_LICENSE("GPL");
