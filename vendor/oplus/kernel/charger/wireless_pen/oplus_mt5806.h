/***********************************************************
** SPDX-License-Identifier: GPL-2.0-only
** Copyright (C), 2025-2025 Oplus. All rights reserved.
** File: oplus_mt5806.h
** Description: mt5806 ic
** Date: 2025-11-12
** -----------Revision History: -------------------------------
** <author>        <data>    <version >       <desc>
****************************************************************/

#ifndef __MT5806_H__
#define __MT5806_H__

#include <linux/version.h>

#define MT_WLS_FAIL	-1
#define MT_WLS_SUCCESS	0

/*****************************************************************************
 *  CMD REG
 ****************************************************************************/
#define ADDR_BUFFER0	0x20000A00
#define ADDR_BUFFER1	0x20000B00
#define ADDR_CMD	0x200009FC
#define ADDR_FLAG	0x200009F8
#define ADDR_BUF_SIZE	0x20001808
#define ADDR_FW_VER	0x2000180C

#define MT_MSEC_PER_SEC		1000
#define LCD_REG_DELAY_SEC	5
#define ATTACH_WAIT_TIME	50
#define ADDR_STR_LEN		128

/*gpio operator define*/
#define GP_0	0	/* wls_sleep_gpio */
#define GP_1	1	/* scan_mode_gpio */
#define GP_2	2	/* wls_sw_en_gpio */
#define GP_3	3	/* wls_off_state_gpio */
#define GP_4	4	/* boost_on_gpio */

#define FULL_SOC	100
#define MIN_TO_MS	(60000)
#define POWER_EXPIRED_TIME_DEFAULT	120
#define PING_EXPIRED_TIME_DEFAULT_SEC	10

#define is_between(left, right, value) \
		(((left) >= (right) && (left) >= (value) && (value) >= (right)) ||\
		((left) <= (right) && (left) <= (value) && (value) <= (right)))

union mtp_q_data {
	struct {
		u16 mt5806_q_mtp;
		u16 mt5806_f_mtp;
		u16 mt5806_q_mtp_varnce;
		u16 mt5806_f_mtp_varnce;
		u16 mt5806_q_val_verf_crc;
		u16 mt5806_cali_num;
	} q_info;
	u8 q_mtp_data[12];
};

enum PEN_STATUS {
	PEN_STATUS_UNDEFINED,
	PEN_STATUS_NEAR,
	PEN_STATUS_FAR,
};

enum POWER_ENABLE_REASON {
	PEN_REASON_UNDEFINED,
	PEN_REASON_NEAR,
	PEN_REASON_RECHARGE,
};

enum POWER_DISABLE_REASON {
	PEN_REASON_UNKNOWN,
	PEN_REASON_FAR,
	PEN_REASON_CHARGE_FULL,
	PEN_REASON_CHARGE_TIMEOUT,
	PEN_REASON_CHARGE_STOP,
	PEN_REASON_CHARGE_EPT,
	PEN_REASON_CHARGE_OCP,
	PEN_OFF_REASON_MAX,
};

enum pen_con_step {
	STEP_INIT = 0,
	STEP_PEN_ID,
	STEP_PEN_ADDR1,
	STEP_PEN_ADDR2,
	STEP_PEN_ADDR3,
	STEP_IDLE,
};

enum wls_print_reason {
	PR_ERROR 	= 0,
	PR_INFO		= 1,
	PR_WARN 	= 2,
	PR_DBG		= 3,
};

enum wls_fod_threshold {
	FODTHS0 	= 0,  /* RPP < 900mW */
	FODTHS1		= 1,  /* 900mW＜RPP≤1800mW */
	FODTHS2 	= 2,  /* RPP > 1800mW */
	FODTHS_MAX	= 3,
};

enum TX_CHARGE_DISABLE_CALLNAME {
	TX_CHARGE_DISABLE_CALLNAME_UNDEFINED = 0,
	TX_CHARGE_DISABLE_CALLNAME_CAMERA = 9,
	TX_CHARGE_DISABLE_CALLNAME_MAX = 32, /* support up to 32 callnames */
};

#define MSG_LEN		5
#define MSG_MAX_LEN	8

/* bits mask */
#define BIT0	0x01
#define BIT1	0x02
#define BIT2	0x04
#define BIT3	0x08
#define BIT4	0x10
#define BIT5	0x20
#define BIT6	0x40
#define BIT7	0x80
#define BIT8	0x100
#define BIT9	0x200
#define BIT10	0x400
#define BIT11	0x800
#define BIT12	0x1000
#define BIT13	0x2000
#define BIT14	0x4000
#define BIT15	0x8000
#define BIT16	0x10000
#define BIT17	0x20000
#define BIT18	0x40000

/* used registers define */
struct mt_reg_s {
	uint16_t reg_name;
	uint16_t reg_bytes_len;
	uint32_t reg_addr;
};

enum {
	MT_COMM_REG_CHIP_ID,
	MT_COMM_REG_FW_VER,
	MT_COMM_REG_FW_VER_MAJ,
	MT_COMM_REG_SYS_MODE,
	MT_COMM_REG_INT_EN,
	MT_COMM_REG_INT_FLAG,
	MT_COMM_REG_INT_CLR,
	MT_COMM_REG_CMD,
	MT_COMM_REG_MAX
};

struct mt_reg_s mt_comm_reg[MT_COMM_REG_MAX] = {
	/* reg name 			bytes number	reg address */
	{MT_COMM_REG_CHIP_ID,		2,	0x166C},
	{MT_COMM_REG_FW_VER,		2,	0x0002},
	{MT_COMM_REG_FW_VER_MAJ,	2,	0x0006},
	{MT_COMM_REG_SYS_MODE,		1,	0x0005},
	{MT_COMM_REG_INT_EN,		4,	0x0010},
	{MT_COMM_REG_INT_FLAG,		4,	0x0014},
	{MT_COMM_REG_INT_CLR,		4,	0x0018},
	{MT_COMM_REG_CMD,		4,	0x0008},
};

enum mt_err_reason {
	MT_ERR_NULL,
	MT_ERR_PING_TIMEOUT,
	MT_ERR_OTP,
	MT_ERR_OCP,
	MT_ERR_OVP,
	MT_ERR_LVP,
	MT_ERR_FOD,
	MT_ERR_PING_OVP,
	MT_ERR_PING_OCP,
	MT_ERR_SOC_FULL,
	MT_ERR_MAX,
};

static const char *const mt_err_reason_text[] = {
	[MT_ERR_NULL] = "null",
	[MT_ERR_PING_TIMEOUT] = "err_wlspen_ping_timeout",
	[MT_ERR_OTP] = "err_wlspen_chg_stop_otp",
	[MT_ERR_OCP] = "err_wlspen_chg_stop_ocp",
	[MT_ERR_OVP] = "err_wlspen_chg_stop_ovp",
	[MT_ERR_LVP] = "err_wlspen_chg_stop_lvp",
	[MT_ERR_FOD] = "err_wlspen_chg_stop_fod",
	[MT_ERR_PING_OVP] = "err_wlspen_chg_stop_ping_ovp",
	[MT_ERR_PING_OCP] = "err_wlspen_chg_stop_ping_ocp",
	[MT_ERR_SOC_FULL] = "err_wlspen_chg_stop_soc_full",
};

struct mt_record_track {
	int pen_id;
	long start_time;
	long end_time;
	int start_soc;
	int end_soc;
	enum mt_err_reason reason_type;
};

#define REG_INT_EN_H			0x0D /* reserved reg */
#define REG_TX_MODE_CHARGE_STATE	0x0005
#define REG_TX_MODE_VIN			0x0090
#define REG_TX_MODE_IIN			0x008E

#define REG_SYS_MODE			0x0004
#define SYS_MODE_TXMODE			1 /* tx mode */

#define REG_COMMAND			0x08
#define REG_COMMAND_SYNC		0x0F
#define REG_CALI_CNT			0xF2
#define REG_CALI_WIDTH_L		0xF4
#define REG_CALI_WIDTH_H		0xF5
#define REG_NO_RX_CNT			0xE4
#define REG_NO_RX_WIDTH_L		0xE6
#define REG_NO_RX_WIDTH_H		0xE7
#define REG_Q_CALI_RESULT		0x13
#define REG_RST_MODE			0x0E /* reserved reg */
#define RST_MODE_WAKUP_DEFAULT		0x00
#define RST_MODE_WAKUP_AFTER_SLEEP	0x11
#define RST_MODE_WAKUP_OTHER		0x55

#define REG_EQUIPMENT_MODE_SET		0x9A /* reserved reg */
#define EQUIPMENT_MODE_ENTER		0xAA
#define EQUIPMENT_MODE_EXIT		0xFF

#define BC_HEADER			0x36
#define BC_DATA_SIZE			0x9

#define PPP_HEADER			0x20
#define PPP_DATA_SIZE			0x9

#define REG_EPT_TYPE			0xB0
#define MT_EPT_OCP			(0x01 << 2)
#define MT_EPT_OVP			(0x01 << 3)
#define MT_EPT_LVP			(0x01 << 4)
#define MT_EPT_FOD			(0x01 << 5)
#define MT_EPT_OTP			(0x01 << 6)
#define MT_EPT_PING_OVP			(0x01 << 1)
#define MT_EPT_PING_OCP			(0x01 << 2)

#define REG_CNT_VARIANCE_L		0xF6
#define REG_CNT_VARIANCE_H		0xF7
#define REG_WIDTH_VARIANCE_L		0xF8
#define REG_WIDTH_VARIANCE_H		0xF9
#define REG_CALI_NUM			0xFC

#define MAC_PKG_HEAD			0x48
#define MAC_PKG_CMD_CKECK		0xC1
#define MAC_PKG_CMD_ADD1		0xB6
#define MAC_PKG_CMD_ADD2		0xB7

#define POWER_PKG_HEAD			0x28
#define POWER_PKG_CMD			0x17

#define MAX_FW_NAME_LENGTH		128
#define CMD_START_Q_CALI_BIT		BIT0

#define MT5806_CRC16_POLY		0x1021
#define MT5806_CRC16_INIT		0xffff

enum {
	WLS_FW_UPDATE_RESULT_NONE,
	WLS_FW_UPDATE_RESULT_SUCCESS,
	WLS_FW_UPDATE_RESULT_ERROR,
	WLS_FW_UPDATE_RESULT_ON_GOING,
};

enum mt5806_q_cali_result {
	MT5806_Q_UNCALIBRATIN,
	MT5806_Q_CALI_FAIL,
	MT5806_Q_CALI_SUCC,
	MT5806_Q_CALIBRATING,
	MT5806_Q_WRITE_MTP,
};

enum CALIBRATE_STATUS {
	Q_CALI_UNKNOWN,
	Q_CALI_IN_PROGRESS,
	Q_CALI_SUCCESS,
	Q_CALI_FAIL,
	Q_CALI_CALIBRATED,
	Q_CALI_UNCALIBRATED,
};

struct q_cali_result {
	int q_cali_times;
	int q_cali_cnt;
	int q_cali_width;
	int q_cali_cnt_var;
	int q_cali_width_var;
};

static const char *const wls_fw_update_result_str[] = {
	"0",
	"1",	/*Success*/
	"I2C_error",
	"FW_CrC_error",
	"FW_Cycle_error",
	"unknow_error",
};

struct mtp_fw {
	u8 *data;
	u16 size;
	u16 crc;
	u32 version;
};

#define MT_BLE_MAC_ADDR_MIN_LEN 10
#define MT_MAC_PKG_LEN 5
struct mt5806_mac_addr {
	uint8_t data_mac[10];
	bool mac_pkg_1;
	bool mac_pkg_2;
};

/* chip_info: 0x0000 ~ 0x000C */
#define MT5806_CHIP_INFO_ADDR	0x0000
#define MT5806_CHIP_INFO_LEN	14
/* chip id register */
#define MT5806_CHIP_ID_ADDR	0x0000
#define MT5806_HW_CHIP_ID_ADDR	0x166C
#define MT5806_CHIP_ID_LEN	2
#define MT5806_CHIP_ID		0x5806

/*
 * tx mode
 */
/* tx_cmd register */
#define MT5806_TX_CMD_ADDR			0x0008
#define MT5806_TX_CMD_LEN			4
#define MT5806_TX_CMD_VAL			1
#define MT5806_TX_CMD_OPENLOOP			BIT(0)
#define MT5806_TX_CMD_OPENLOOP_SHIFT		0
#define MT5806_TX_CMD_RENORM			BIT(1)
#define MT5806_TX_CMD_RENORM_SHIFT		1
#define MT5806_TX_CMD_SETPERIOD			BIT(2)
#define MT5806_TX_CMD_SETPERIOD_SHIFT		2
#define MT5806_TX_CMD_RST_SYS			BIT(4)
#define MT5806_TX_CMD_RST_SYS_SHIFT		4
#define MT5806_TX_CMD_CLEAR_INT			BIT(5)
#define MT5806_TX_CMD_CLEAR_INT_SHIFT		5
#define MT5806_TX_CMD_SEND_MSG			BIT(6)
#define MT5806_TX_CMD_SEND_MSG_SHIFT		6
#define MT5806_TX_CMD_OVP			BIT(7)
#define MT5806_TX_CMD_OVP_SHIFT			7
#define MT5806_TX_CMD_OCP			BIT(8)
#define MT5806_TX_CMD_OCP_SHIFT			8
#define MT5806_TX_CMD_PING_OCP			BIT(9)
#define MT5806_TX_CMD_PING_OCP_SHIFT		9
#define MT5806_TX_CMD_FOD_CTRL			BIT(10)
#define MT5806_TX_CMD_FOD_CTRL_SHIFT		10
#define MT5806_TX_CMD_LOW_POWER			BIT(11)
#define MT5806_TX_CMD_LOW_POWER_SHIFT		11
#define MT5806_TX_CMD_START_TX			BIT(12)
#define MT5806_TX_CMD_START_TX_SHIFT		12
#define MT5806_TX_CMD_STOP_TX			BIT(13)
#define MT5806_TX_CMD_STOP_TX_SHIFT		13
#define MT5806_TX_CMD_CLEAR_EPT			BIT(14)
#define MT5806_TX_CMD_CLEAR_EPT_SHIFT		14
#define MT5806_TX_CMD_FW_VERIFY			BIT(15)
#define MT5806_TX_CMD_FW_VERIFY_SHIFT		15
#define MT5806_TX_CMD_Q_SCAN			BIT(16)
#define MT5806_TX_CMD_Q_SCAN_SHIFT		16
/* tc_cmd_flag register */
#define MT5806_TX_CMD_SYNC_ADDR			0x000F
#define MT5806_TX_CMD_SYNC_VAL			0x0055
/* tx_irq_en register */
#define MT5806_TX_IRQ_EN_ADDR			0x0010
#define MT5806_TX_IRQ_EN_LEN			4
#define MT5806_TX_IRQ_EN_VAL			0xE02C90F
/* tx_irq_latch register */
#define MT5806_TX_IRQ_ADDR			0x0014
#define MT5806_TX_IRQ_LEN			4
#define MT5806_TX_IRQ_SS_PKG_RCVD		BIT(0)
#define MT5806_TX_IRQ_ID_PKT_RCVD		BIT(1)
#define MT5806_TX_IRQ_CFG_PKT_RCVD		BIT(2)
#define MT5806_TX_IRQ_REMOVE_POWER		BIT(6)
#define MT5806_TX_IRQ_POWER_TRANS 		BIT(8)
#define MT5806_TX_IRQ_POWERON			BIT(9)
#define MT5806_TX_IRQ_PP_PKT_RCVD		BIT(11)
#define MT5806_TX_IRQ_TX_DISABLE		BIT(13)
#define MT5806_TX_IRQ_TX_ENABLE			BIT(14)
#define MT5806_TX_IRQ_EPT_PKT_RCVD		BIT(15)
#define MT5806_TX_IRQ_START_PING		BIT(17)
#define MT5806_TX_IRQ_RX_ATTACH			BIT(25)
#define MT5806_TX_IRQ_RX_REMOVED		BIT(26)
#define MT5806_TX_IRQ_Q_CAIL			BIT(27)
/* tx_irq_clr register */
#define MT5806_TX_IRQ_CLR_ADDR			0x0018
#define MT5806_TX_IRQ_CLR_LEN			4
#define MT5806_TX_IRQ_CLR_ALL			0xFFFFFFFF
/* tx_rcvd_msg_data register */
#define MT5806_TX_RCVD_MSG_HEADER_ADDR		0x0020
#define MT5806_TX_RCVD_MSG_CMD_ADDR		0x0021
#define MT5806_TX_RCVD_MSG_DATA_ADDR		0x0022
/* rcvd_msg: bit[0]:header, bit[1]:cmd, bit[2:5]:data */
#define MT5806_RCVD_MSG_DATA_LEN		4
#define MT5806_RCVD_MSG_PKT_LEN			6
#define MT5806_RCVD_PKT_BUFF_LEN		8
#define MT5806_RCVD_PKT_STR_LEN			64
/* tx_send_msg_data register */
#define MT5806_TX_SEND_MSG_HEADER_ADDR		0x0036
#define MT5806_TX_SEND_MSG_CMD_ADDR		0x0037
#define MT5806_TX_SEND_MSG_DATA_ADDR		0x0038
/* send_msg: bit[0]:header, bit[1]:cmd, bit[2:5]:data */
#define MT5806_SEND_MSG_DATA_LEN		4
#define MT5806_SEND_MSG_PKT_LEN			6
/* tx_max_fop, in kHz */
#define MT5806_TX_MAX_FOP_ADDR			0x004C
#define MT5806_TX_MAX_FOP_LEN			2
#define MT5806_TX_MAX_FOP		        145
#define MT5806_TX_FOP_STEP		        1
/* tx_min_fop, in kHz */
#define MT5806_TX_MIN_FOP_ADDR			0x004E
#define MT5806_TX_MIN_FOP_LEN			2
#define MT5806_TX_MIN_FOP			120
/* tx_ping_freq, in kHz */
#define MT5806_TX_PING_FREQ_ADDR		0x0050
#define MT5806_TX_PING_FREQ_LEN			2
#define MT5806_TX_PING_FREQ			130
#define MT5806_TX_PING_FREQ_MIN			100
#define MT5806_TX_PING_FREQ_MAX			150
#define MT5806_TX_PING_STEP			1
/* tx ping ocp addr */
#define MT5806_TX_PING_OCP_TH_ADDR		0x0052
#define MT5806_TX_PING_OCP_TH_LEN		2
#define MT5806_TX_PING_OCP_TH			800
/* tx_ocp_thres register, in mA */
#define MT5806_TX_OCP_TH_ADDR			0x0054
#define MT5806_TX_OCP_TH_LEN			2
#define MT5806_TX_OCP_TH			2000
#define MT5806_TX_OCP_TH_STEP			1
/* tx_ovp_thres register, in mV */
#define MT5806_TX_OVP_TH_ADDR			0x0058
#define MT5806_TX_OVP_TH_LEN			2
#define MT5806_TX_OVP_TH			20000
#define MT5806_TX_OVP_TH_STEP			1
/* fod thd */
#define MT5806_TX_FOD_THD_ADDR			0x0060
#define MT5806_TX_PLOSS_TH0_VAL			2200
/* tx_ping_duty_cycle register */
#define MT5806_TX_PT_DC_ADDR			0x0063
#define MT5806_TX_HALF_BRIDGE_DC		255
#define MT5806_TX_FULL_BRIDGE_DC		150
/* tx ept_reason register */
#define MT5806_TX_EPT_REASON_ADDR		0x006A
#define MT5806_TX_EPT_REASON_LEN		2
/* tx ploss counter */
#define MT5806_TX_PLOSS_CNT_ADDR		0x006C
#define MT5806_TX_PLOSS_CNT_VAL			3
/* tx_oper_freq register, in 4Hz */
#define MT5806_TX_OP_FREQ_ADDR			0x006E
#define MT5806_TX_OP_FREQ_LEN			2
#define MT5806_TX_OP_FREQ_STEP			10
/* tx_clr_int_flag register */
#define MT5806_TX_IRQ_CLR_CTRL_ADDR		0x0070
#define MT5806_TX_IRQ_CLR_CTRL_LEN		1
#define MT5806_TX_IRQ_CLR_CTRL			1
/* pen on the pad status register  */
#define MT5806_TX_PEN_ON_THE_PAD_STA_ADDR	0x0076
#define MT5806_TX_PEN_ON_THE_PAD		0x55
#define MT5806_TX_PEN_LEAVE_PAD			0x00
/* tx_get_temp register */
#define MT5806_TX_GET_TEMP_ADDR			0x00D8
/* tx_vrect register, in mV */
#define MT5806_TX_VRECT_ADDR			0x008A
#define MT5806_TX_VRECT_LEN			2
/* tx_iin register, in mA */
#define MT5806_TX_IIN_ADDR			0x008E
#define MT5806_TX_IIN_LEN			2
/* tx_vin register, in mV */
#define MT5806_TX_VIN_ADDR			0x0090
#define MT5806_TX_VIN_LEN			2
/* tx_fod_status register */
#define MT5806_TX_Q_FOD_FODTH0_ADDR		0x0094
#define MT5806_TX_Q_FOD_FODTH1_ADDR		0x0096
#define MT5806_TX_Q_FOD_FODTH2_ADDR		0x0098
#define MT5806_TX_Q_FOD_LEN			2
/* tx_ping_interval, in ms */
#define MT5806_TX_PING_INTERVAL_ADDR		0x00A6
#define MT5806_TX_PING_INTERVAL_LEN		2
#define MT5806_TX_PING_INTERVAL_STEP		1
#define MT5806_TX_PING_INTERVAL_MIN		0
#define MT5806_TX_PING_INTERVAL_MAX		1000
#define MT5806_TX_PING_INTERVAL			120
/* tx_pwm_duty register */
#define MT5806_TX_PWM_DUTY_ADDR			0x00A8
#define MT5806_TX_PWM_DUTY_LEN			1
/* tx fsk depthoffset register */
#define MT5806_TX_FSK_DEPTH_ADDR		0x00A9
#define MT5806_TX_FSK_DEPTH_OFFSET		130
/* tx_ept_type register */
#define MT5806_TX_EPT_SRC_ADDR			0x00B0
#define MT5806_TX_EPT_SRC_LEN			4
#define MT5806_TX_EPT_SRC_CMD			BIT(0)
#define MT5806_TX_EPT_SRC_SS			BIT(1)
#define MT5806_TX_EPT_SRC_ID			BIT(2)
#define MT5806_TX_EPT_SRC_XID			BIT(3)
#define MT5806_TX_EPT_SRC_CFG_CNT		BIT(4)
#define MT5806_TX_EPT_SRC_PCH			BIT(5)
#define MT5806_TX_EPT_SRC_EPT_TIMEOUT		BIT(7)
#define MT5806_TX_EPT_SRC_CEP_TIMEOUT		BIT(8)
#define MT5806_TX_EPT_SRC_RPP_TIMEOUT		BIT(9)
#define MT5806_TX_EPT_SRC_OCP			BIT(10)
#define MT5806_TX_EPT_SRC_OVP			BIT(11)
#define MT5806_TX_EPT_SRC_LVP			BIT(12)
#define MT5806_TX_EPT_SRC_FOD			BIT(13)
#define MT5806_TX_EPT_SRC_OTP			BIT(14)
#define MT5806_TX_EPT_SRC_CFG			BIT(16)
#define MT5806_TX_EPT_SRC_PING_OVP		BIT(17)
#define MT5806_TX_EPT_SRC_PING_OCP		BIT(18)
#define MT5806_TX_EPT_SRC_PKTERR		BIT(19)
/* tx_charge_device_type register */
#define MT5806_TX_CHARGE_DEV_ADDR		0x00BA
#define MT5806_MTP_Q_ADDR			0x7800

/* tx_q_no_rx_cnt register */
#define MT5806_TX_CALI_CNT_ADDR			0x00F2
#define MT5806_TX_CALI_CNT_LEN			2
/* tx_q_no_rx_width register */
#define MT5806_TX_CALI_WIDTH_ADDR		0x00F4
#define MT5806_TX_CALI_WIDTH_LEN		2
/* tx_q_no_rx_cnt_var register */
#define MT5806_TX_CALI_CNT_VAR_ADDR		0x00F6
#define MT5806_TX_CALI_CNT_VAR_LEN		2
/* tx_q_no_rx_width_var register */
#define MT5806_TX_CALI_WIDTH_VAR_ADDR		0x00F8
#define MT5806_TX_CALI_WIDTH_VAR_LEN		2

/* tx_q_no_rx_cnt register */
#define MT5806_TX_MTP_CNT_ADDR			0x00E4
#define MT5806_TX_MTP_CNT_LEN			2
/* tx_q_no_rx_width register */
#define MT5806_TX_MTP_WIDTH_ADDR		0x00E6
#define MT5806_TX_MTP_WIDTH_LEN			2
/* tx_q_no_rx_cnt_var register */
#define MT5806_TX_MTP_CNT_VAR_ADDR		0x00E8
#define MT5806_TX_MTP_CNT_VAR_LEN		2
/* tx_q_no_rx_width_var register */
#define MT5806_TX_MTP_WIDTH_VAR_ADDR		0x00EA
#define MT5806_TX_MTP_WIDTH_VAR_LEN		2

/*
 * mtp register
 */

#define MT5806_MTP_PGM_SIZE			512
/* fw reverision */
#define MT5806_MTP_MINOR_ADDR			0x0002
#define MT5806_MTP_MAJOR_ADDR			0x0006
/* fw length register */
#define MT5806_FW_LENGTH_ADDR			0x00DA
/* fw crc16 value */
#define MT5806_FW_CRC16VALUE_ADDR		0x00DC
/* fw verify status */
#define MT5806_FW_VERIFY_STATUS_ADDR		0x00DE
#define MT5806_FW_VERIFY_STATUS_BUSY_VAL	0x0000
#define MT5806_FW_VERIFY_STATUS_FAIL_VAL	0x5555
#define MT5806_FW_VERIFY_STATUS_OK_VAL		0xAAAA
/* bootloader chipid addr */
#define MT5806_BOOTLOADER_CHIPID_ADDR		0x022C
/* bootloader start addr */
#define MT5806_BTLOADR_ADDR			0x0800
/* bootloader ctrl */
#define MT5806_BOOT_CTRL_ADDR			0x0000
#define MT5806_BOOT_CTRL_WRITE_CMD		0x0110
#define MT5806_BOOT_CTRL_READ_CMD		0X0220
#define MT5806_BOOT_CTRL_MTP_ERASE_CMD		0x0440
#define MT5806_BOOT_CTRL_CRC_VERIFY_CMD		0x0550
#define MT5806_BOOT_CTRL_APP_ERASE_CMD		0x0880
#define MT5806_BOOT_CTRL_LIB_ERASE_CMD		0x0990
#define MT5806_BOOT_CTRL_FAC_DATA_CMD		0x0AA0
#define MT5806_BOOT_CTRL_DYN_DATA_CMD		0x0BB0
/* bootloader status addr */
#define MT5806_BOOT_STATUS_ADDR			0x0002
/* bootloader status value */
#define MT5806_BOOT_STATUS_WRITE_OK_VAL		0x0000
#define MT5806_BOOT_STATUS_WRITE_ERR_VAL	0x1111
#define MT5806_BOOT_STATUS_READ_OK_VAL		0x0000
#define MT5806_BOOT_STATUS_READ_ERR_VAL		0x2222
#define MT5806_BOOT_STATUS_ERASE_OK_VAL		0x0000
#define MT5806_BOOT_STATUS_ERASE_ERR_VAL	0x3333
#define MT5806_BOOT_STATUS_DATA_OK_VAL		0x0000
#define MT5806_BOOT_STATUS_DATA_ERR_VAL		0x4444
#define MT5806_BOOT_STATUS_CRC_OK_VAL		0x0000
#define MT5806_BOOT_STATUS_CRC_ERR_VAL		0x5555
#define MT5806_BOOT_STATUS_IDLE_VAL		0x0000
#define MT5806_BOOT_STATUS_BUSY_VAL		0xAAAA
/* bootloader pgm addr addr */
#define MT5806_BOOT_PGM_ADDR_ADDR		0x0004
/* bootloader pgm addr start value */
#define MT5806_BOOT_PGM_ADDR_START_VAL		0
/* bootloader pgm length addr */
#define MT5806_BOOT_PGM_LEN_ADDR		0x0008
/* bootloader pgm crc16 addr */
#define MT5806_BOOT_PGM_VERIFY_ADDR		0x000C
/* bootloader chip crc16 addr */
#define MT5806_BOOT_PGM_VERIFY_CHIP_ADDR	0x0010
/* bootloader pgm buffer addr */
#define MT5806_BOOT_PGM_BUFFER_ADDR		0x0014

/* load fw start addr */
#define MT5806_FW_ADDR				0x0000
#define MT5806_FW_ERASE_TIME_MS			100

/* load app start addr */
#define MT5806_BOOT_APP_ADDR			0x0000
#define MT5806_BOOT_APP_ERASE_TIME_MS		3500
/* load lib start addr */
#define MT5806_BOOT_LIB_ADDR			0x3000
#define MT5806_BOOT_LIB_ERASE_TIME_MS		5500
/* load q_val&F_val addr */
#define MT5806_BOOT_QF_BUFFER0_ADDR		0x7E00 /* dynamic calibrate */
#define MT5806_BOOT_QF_BUFFER1_ADDR		0x7F00 /* factory calibrate */
#define MT5806_BOOT_FACTOR_ERASE_TIME_MS	70
#define MT5806_BOOT_FACTOR_DATA_LEN		4
/* cortex M0 core */
#define MT5806_PMU_WDGEN_ADDR			0x1408
#define MT5806_WDG_DISABLE			0x95
#define MT5806_WDG_ENABLE			0x59
#define MT5806_PMU_FLAG_ADDR			0x1400
#define MT5806_WDT_INTFALG			0x01
#define MT5806_SYS_KEY_ADDR			0x1244
#define MT5806_KEY_VAL				0x57
#define MT5806_CODE_REMAP_ADDR			0x1208
#define MT5806_CODE_REMAP_VAL			0x11
#define MT5806_M0_CTRL_ADDR			0x1200
#define MT5806_M0_HOLD_VAL			0x20
#define MT5806_M0_RST_VAL			0x80
#define MT5806_SYS_CLK_ADDR			0x1608
#define MT5806_SYS_CLK_VAL			0x0F
#define MT5806_PWM_DRV_ADDR			0x4064
#define MT5806_PWM_DISABLE			0x00
#define MT5806_DMOD_SRCSEL_ADDR			0x4200
#define MT5806_DMOD_DMA_DISABLE			0x00

#define MT_OCP_THRESHOLD			800
#define MT_OVP_THRESHOLD			9000
#define MT_FOD_THRES0				3000
#define MT_FOD_THRES1				3300
#define MT_FOD_THRES2				3800
#define SOC_THRESHOLD				90
#define DEFAULT_BOOST_VOLT_MV			5800
#define MAX_BOOST_VOLT_MV			14750
#define MIN_BOOST_VOLT_MV			2000
#define MAX_RETRY			        5
#define BOOST_SET_RETRY				60
#define BOOST_SET_DELAY_500MS			500
#define BOOST_WORK_MAX_RETRY			10
#define MAX_FW_NAME_LENGTH			128
#define IS_SUPPORT_BY_HBOOST			1
#define TX_WAKEUP_WAIT_MS			2500

#define MT_LOCAL_T_NS_TO_S_THD			(1000000000)
#define MT_TRACK_TIME_THRESHOLD_SEC		(60)
#define MT_SOC_INVALID				(-1)

#if (defined(CONFIG_OPLUS_CHARGER_MTK) || LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
#include <uapi/linux/rtc.h>
struct timeval {
	__kernel_old_time_t tv_sec; /* seconds */
	__kernel_suseconds_t tv_usec; /* microseconds */
};
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)) */
#endif /* defined(CONFIG_OPLUS_CHARGER_MTK)
	|| LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0) */
#endif
