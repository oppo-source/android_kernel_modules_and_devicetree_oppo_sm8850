/***********************************************************
** SPDX-License-Identifier: GPL-2.0-only
** Copyright (C), 2025-2025 Oplus. All rights reserved.
** File: oplus_mt5806.c
** Description: mt5806 ic
** Date: 2025-11-12
** -----------Revision History: -------------------------------
** <author>        <data>    <version >       <desc>
****************************************************************/

#include <linux/types.h>
#include <linux/i2c.h>
#include <linux/debugfs.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/regmap.h>
#include <linux/pinctrl/consumer.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/workqueue.h>
#include <linux/sysfs.h>
#include <linux/gpio.h>
#include <linux/bitops.h>
#include <linux/jiffies.h>
#include <linux/reboot.h>
#include <linux/notifier.h>
#include <linux/firmware.h>
#include <linux/version.h>
#include <linux/timer.h>
#include <uapi/linux/time.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeup.h>
#include <linux/printk.h>
#include <linux/power_supply.h>
#include <linux/alarmtimer.h>
#include <linux/rtc.h>
#include <linux/sched/clock.h>
#define CREATE_TRACE_POINTS
#include "wireless_pen_trace.h"

#if IS_ENABLED(CONFIG_OPLUS_MTK_DRM_GKI_NOTIFY_CHG)
#include <linux/mtk_panel_ext.h>
#include <linux/mtk_disp_notify.h>
#endif
#include "oplus_mt5806.h"
#include "oplus_mt5806_fw.h"
#include "oplus_wireless_pen_glink.h"

#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
#include <soc/oplus/system/oplus_project.h>
#endif /* CONFIG_DISABLE_OPLUS_FUNCTION*/

#if IS_ENABLED(CONFIG_DRM_PANEL_NOTIFY) || IS_ENABLED(CONFIG_OPLUS_CHG_DRM_PANEL_NOTIFY)
#include <linux/soc/qcom/panel_event_notifier.h>
#include <drm/drm_panel.h>
#endif

struct mt5806_dev *wls_tx;
int mt_pen_log_level = PR_INFO;
#define mt_wls_log(num, fmt, ...) \
	do { \
		if (mt_pen_log_level >= (int)num) \
			printk(KERN_ERR "[mt5806]:" pr_fmt(fmt), ##__VA_ARGS__); \
	} while (0)
module_param(mt_pen_log_level, int, 0644);
MODULE_PARM_DESC(mt_pen_log_level, "wireless_pen log level");

struct mt5806_access_func {
	int (*read)(struct mt5806_dev *chip, u16 reg, u8 *val);
	int (*write)(struct mt5806_dev *chip, u16 reg, u8 val);
	int (*write_mask)(struct mt5806_dev *chip, u16 reg, u8 mask, u8 val);
	int (*read_buf)(struct mt5806_dev *chip, u16 reg, u8 *buf, u32 size);
	int (*write_buf)(struct mt5806_dev *chip, u16 reg, u8 *buf, u32 size);
};

struct mt5806_dev {
	char *name;
	struct i2c_client *client;
	struct device *dev;
	struct device *wireless_dev;
	struct regmap *regmap;
	struct regmap *regmap32;
	struct mt5806_access_func bus;
	int address;
	int data;

	/*struct wake_lock*/
	struct wakeup_source *program_wake_lock;
	struct wakeup_source *mt_iic_wake_lock;
	struct wakeup_source *mt_wls_wake_lock;
	struct mutex pm_lock;
	struct mutex program_lock;
	struct mutex i2c_mutex;
	struct mutex irq_lock;
	struct mutex tx_disable_callname_lock;

	/*wls fw update*/
	struct mtp_fw fw;
	const struct firmware *firm_data_bin;
	unsigned char *firmware_data;
	unsigned int fw_data_length;
	bool wls_fw_is_burning;
	int wls_fw_update_result;
	const char *fw_bin_name;
	uint16_t bin_fw_ver_major;
	uint16_t bin_fw_ver_minor;

	/*gpio*/
	int wls_irq_gpio;
	int wls_irq;
	int wls_off_state_gpio;
	int wls_sleep_gpio;
	int scan_mode_gpio; /* high: sleep time 200ms,low: sleep 2s */
	int wls_sw_en_gpio;
	int boost_on_gpio;
	bool led_on;

	int is_supply_by_hboost;
	int hboost_default_volt;
	int hboost_attach_volt;
	int hboost_volt;
	int hboost_status;

	struct pinctrl *mt_pinctrl;
	struct pinctrl_state *wls_irq_gpio_default;
	struct pinctrl_state *wls_irq_gpio_active;
	struct pinctrl_state *wls_irq_gpio_sleep;

	struct pinctrl_state *wls_sw_en_default;
	struct pinctrl_state *wls_sw_en_active;
	struct pinctrl_state *wls_sw_en_sleep;

	struct pinctrl_state *wls_sleep_gpio_default;
	struct pinctrl_state *wls_sleep_gpio_active;
	struct pinctrl_state *wls_sleep_gpio_sleep;

	struct pinctrl_state *scan_mode_gpio_default;
	struct pinctrl_state *scan_mode_gpio_active;
	struct pinctrl_state *scan_mode_gpio_sleep;

	struct pinctrl_state *wls_off_state_gpio_default;
	struct pinctrl_state *wls_off_state_gpio_active;
	struct pinctrl_state *wls_off_state_gpio_sleep;

	struct pinctrl_state *boost_on_gpio_default;
	struct pinctrl_state *boost_on_gpio_active;
	struct pinctrl_state *boost_on_gpio_sleep;

	struct delayed_work max_chg_time_check_work;
	struct delayed_work lcd_notify_reg_work;
	struct delayed_work equipment_work;
	struct delayed_work ping_timeout_work;
	struct work_struct set_boost_work;
	struct work_struct rechg_work;
	struct work_struct error_attach_check_work;
	struct work_struct init_work;
	struct work_struct track_record_upload_work;
	uint8_t pen_present;
	uint8_t pen_status;
	uint64_t ble_mac_addr;
	uint64_t mac_check_data;
	uint8_t pen_id;
	uint64_t tx_start_time;
	uint64_t upto_ble_time;
	uint64_t ping_succ_time;
	uint16_t fw_version;
	int tx_voltage;
	int tx_current;
	int rx_soc;
	int pen_soc;
	int soc_threshould;
	uint8_t charge_allow;
	bool init_work_succ;

	uint32_t power_enable_time;
	uint8_t power_enable_reason;
	uint8_t power_disable_reason;
	uint8_t power_disable_count[PEN_OFF_REASON_MAX];
	int power_expired_time;

	uint8_t q_cali_status;
	struct q_cali_result q_cali_result;
	/*param*/
	u16 chip_id;
	u32 app_fw_ver;
	int reverse_vin_ma;
	int pen_match_step;
	uint8_t pen_addr_data[8];
	char pen_addr_buf[32];

	/*reg val*/
	u32 int_flag;
	u32 int_clr;

	/*flag*/
	bool i2c_ready;
	int equipment_mode;
	int std_rx_cnt[2];
	int std_rx_width[2];
	volatile bool cali_done;
	int cali_cnt_variance;
	int cali_width_variance;
	union mtp_q_data q_mtp_data;
	union mtp_q_data q_cali_data;

#if IS_ENABLED(CONFIG_DRM_PANEL_NOTIFY) || IS_ENABLED(CONFIG_OPLUS_CHG_DRM_PANEL_NOTIFY)
	struct drm_panel *active_panel;
	void *notifier_cookie;
#else
	struct notifier_block mt_fb_notify;
#endif

	struct mt_record_track mt_record_track;
	uint32_t tx_disable_callname; /* bitmask for multiple callnames */
	bool support_camera_tx_disable; /* dtsi configurable support for CAMERA callname */
};

struct mt5806_irq_handler_info {
	const char *name;
	int (*handler)(struct mt5806_dev *chip);
};

struct mt5806_irq_info {
	u16 irq_flag_reg;
	u16 irq_clr_reg;
	u16 irq_ss_reg;
	u32 val;
	u32 pre_val;
	struct mt5806_irq_handler_info irq_handler[32];
};

static DECLARE_WAIT_QUEUE_HEAD(i2c_waiter);
static int equipment_tx_wakeup(struct mt5806_dev *chip);
static void equipment_tx_wakeup_disable(struct mt5806_dev *chip);
static void mt_set_gpio_value(struct mt5806_dev *chip, int gp_num, int value);
static void mt_set_chg_enable(struct mt5806_dev *chip, bool enable);
static int mt_register_lcd_notify(struct mt5806_dev *chip);
static int mt_get_cali_status(struct mt5806_dev *chip);
static int mt5806_fw_q_mtp_get(struct mt5806_dev *chip);
static void mt_track_upload(struct mt5806_dev *chip, enum mt_err_reason reason_type);
static void mt5806_send_mislocated_uevent(struct device *dev);

static long get_timestamp_ms(void)
{
	struct timespec64 now;
	ktime_get_real_ts64(&now);
	return timespec64_to_ns(&now) / NSEC_PER_MSEC;
}

/**************************************************************************************
 *                                 common function                                     *
 ***************************************************************************************/
static inline void do_gettimeofday(struct timeval *tv)
{
	struct timespec64 now;

	ktime_get_real_ts64(&now);
	tv->tv_sec = now.tv_sec;
	tv->tv_usec = now.tv_nsec / 1000;
}

static int mt_get_local_time_s(void)
{
	int local_time_s;

	local_time_s = local_clock() / MT_LOCAL_T_NS_TO_S_THD;
	mt_wls_log(PR_INFO, "local_time_s:%d\n", local_time_s);

	return local_time_s;
}

/**************************************************************************************
 *                             mt interface function                                  *
 ***************************************************************************************/
static int mt5806_read(struct mt5806_dev *chip, u16 reg, u8 *val)
{
	unsigned int temp;
	int rc = 0;

	if (!chip || !chip->regmap) {
		mt_wls_log(PR_ERROR, "[%s] chip or regmap is NULL\n", __func__);
		return -EINVAL;
	}

	mutex_lock(&chip->i2c_mutex);
	rc = regmap_read(chip->regmap, reg, &temp);
	if (rc >= 0)
		*val = (u8)temp;
	else
		mt_wls_log(PR_ERROR, "read fail:0x%02X, rc=%d\n", reg, rc);

	mutex_unlock(&chip->i2c_mutex);

	return rc;
}

static int mt5806_write(struct mt5806_dev *chip, u16 reg, u8 val)
{
	int rc = 0;

	if (!chip || !chip->regmap) {
		mt_wls_log(PR_ERROR, "[%s] chip or regmap is NULL\n", __func__);
		return -EINVAL;
	}

	mutex_lock(&chip->i2c_mutex);
	rc = regmap_write(chip->regmap, reg, val);
	if (rc < 0)
		mt_wls_log(PR_ERROR, "write fail:0x%02x to 0x%02x, rc=%d\n",
		           val, reg, rc);

	mutex_unlock(&chip->i2c_mutex);

	return rc;
}

static int mt5806_masked_write(struct mt5806_dev *chip, u16 reg, u8 mask, u8 val)
{
	int rc = 0;
	u8 temp = 0;

	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip is NULL\n", __func__);
		return -EINVAL;
	}

	rc = mt5806_read(chip, reg, &temp);
	if (rc < 0) {
		mt_wls_log(PR_ERROR, "read failed: reg=%03X, rc=%d\n", reg, rc);
		return rc;
	}
	temp &= ~mask;
	temp |= val & mask;
	rc = mt5806_write(chip, reg, temp);
	if (rc < 0) {
		mt_wls_log(PR_ERROR, "write failed: reg=%03X, rc=%d\n", reg, rc);
		return rc;
	}

	return 0;
}

static int mt5806_read_buffer(struct mt5806_dev *chip, u16 reg, u8 *buf, u32 size)
{
	int rc = 0;

	if (!chip || !chip->regmap || !buf) {
		mt_wls_log(PR_ERROR, "[%s] chip, regmap or buf is NULL\n", __func__);
		return -EINVAL;
	}

	mutex_lock(&chip->i2c_mutex);
	rc = regmap_bulk_read(chip->regmap, reg, buf, size);
	if (rc < 0)
		mt_wls_log(PR_ERROR, "read fail:size:0x%02x to reg:0x%02x: %d\n", size, reg, rc);

	mutex_unlock(&chip->i2c_mutex);

	return rc;
}

static int mt5806_write_buffer(struct mt5806_dev *chip, u16 reg, u8 *buf, u32 size)
{
	int rc = 0;

	if (!chip || !chip->regmap || !buf) {
		mt_wls_log(PR_ERROR, "[%s] chip, regmap or buf is NULL\n", __func__);
		return -EINVAL;
	}

	mutex_lock(&chip->i2c_mutex);
	rc = regmap_bulk_write(chip->regmap, reg, buf, size);
	if (rc < 0)
		mt_wls_log(PR_ERROR, "Failed regmap_bulk_write rc=%d\n", rc);

	mutex_unlock(&chip->i2c_mutex);

	return rc;
}

static int mt5806_write_word(struct mt5806_dev *chip, u16 reg, u16 value)
{
	int ret;
	u8 write_date[2];

	if (!chip || !chip->regmap) {
		mt_wls_log(PR_ERROR, "[%s] chip or regmap is NULL\n", __func__);
		return -EINVAL;
	}

	write_date[0] = value & 0xff;
	write_date[1] = (value >> 8) & 0xff;

	mutex_lock(&chip->i2c_mutex);
	ret = regmap_raw_write(chip->regmap, reg, write_date, 2);
	mutex_unlock(&chip->i2c_mutex);

	if (ret < 0) {
		mt_wls_log(PR_ERROR, "[%s] i2c write error!\n", __func__);
		return MT_WLS_FAIL;
	}
	return MT_WLS_SUCCESS;
}

static int mt5806_read_word(struct mt5806_dev *chip, u16 addr, u16 *value)
{
	int ret;
	u8 read_date[2];
	u16 r_data = 0;

	if (!chip || !chip->regmap || !value) {
		mt_wls_log(PR_ERROR, "[%s] chip, regmap or value is NULL\n", __func__);
		return -EINVAL;
	}

	mutex_lock(&chip->i2c_mutex);
	ret = regmap_raw_read(chip->regmap, addr, read_date, 2);
	mutex_unlock(&chip->i2c_mutex);

	if (ret < 0) {
		mt_wls_log(PR_ERROR, "[%s] i2c read error!\n", __func__);
		return MT_WLS_FAIL;
	}

	r_data  = read_date[1];
	r_data = r_data  << 8;
	r_data |= read_date[0];

	*value = r_data;
	return  MT_WLS_SUCCESS;
}

static int mt5806_write_dword(struct mt5806_dev *chip, u16 reg, u32 value)
{
	int ret;
	u8 write_date[4];

	if (!chip || !chip->regmap) {
		mt_wls_log(PR_ERROR, "[%s] chip or regmap is NULL\n", __func__);
		return -EINVAL;
	}

	write_date[0] = value & 0xff;
	write_date[1] = (value >> 8) & 0xff;
	write_date[2] = (value >> 16) & 0xff;
	write_date[3] = (value >> 24) & 0xff;

	mutex_lock(&chip->i2c_mutex);
	ret = regmap_raw_write(chip->regmap, reg, write_date, 4);
	mutex_unlock(&chip->i2c_mutex);

	if (ret < 0) {
		mt_wls_log(PR_ERROR, "[%s] i2c write error!\n", __func__);
		return MT_WLS_FAIL;
	}
	return MT_WLS_SUCCESS;
}


//*****************************for program************************
static u32 mt5806_fw_crc16_cal(u8 *data, u32 length)
{
	int i = 0;
	int j = 0;
	u32 poly = 1 << 16 | MT5806_CRC16_POLY;
	u32 crc = MT5806_CRC16_INIT;

	if (data == NULL)
		return 0;

	for (i = 0; i < length; i++) {
		crc ^= (u32)data[i] << 8;
		for (j = 0; j < 8; j++) {
			crc = crc << 1;
			if (crc & 0x10000)
				crc ^= poly;
		}
	}
	return crc;
}

static unsigned int mt5806_mtp_fw_crc(unsigned char *data, unsigned int len)
{
	unsigned int crc = 0xFFFF;

	crc = mt5806_fw_crc16_cal(data, len);
	crc = crc ^ 0xFFFF;

	return crc;
}

static void mt5806_reset_power(struct mt5806_dev *chip)
{
	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip is NULL\n", __func__);
		return;
	}

	mt_set_gpio_value(chip, GP_2, 0);
	msleep(1000);
	mt_set_gpio_value(chip, GP_2, 1);

	return;
}

static int mt5806_get_reverse_vin(struct mt5806_dev *chip)
{
	int rc = 0;
	int reverse_vin_mv = 0;
	u8 data[2];

	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip is NULL\n", __func__);
		return -EINVAL;
	}

	if (chip->wls_fw_is_burning) {
		mt_wls_log(PR_ERROR, "wls fw is burning,return\n");
		return -EINVAL;
	}

	memset(data, 0, sizeof(data));
	rc = chip->bus.read_buf(chip, REG_TX_MODE_VIN, data, sizeof(data));
	if (rc < 0) {
		mt_wls_log(PR_ERROR, "read reverse iin failed\n\n");
	} else {
		reverse_vin_mv = data[0] | ((data[1] & 0xFF) << 8);
		mt_wls_log(PR_INFO, "reverse_vin_mv = %d\n", reverse_vin_mv);
	}

	return reverse_vin_mv;
}

static int mt5806_get_reverse_iin(struct mt5806_dev *chip)
{
	int rc = 0;
	int reverse_iin_ma = 0;
	u8 data[2];

	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip is NULL\n", __func__);
		return -EINVAL;
	}

	if (chip->wls_fw_is_burning) {
		mt_wls_log(PR_ERROR, "wls fw is burning,return\n");
		return -EINVAL;
	}

	memset(data, 0, sizeof(data));
	rc = chip->bus.read_buf(chip, REG_TX_MODE_IIN, data, sizeof(data));
	if (rc < 0) {
		mt_wls_log(PR_ERROR, "read reverse iin failed\n\n");
	} else {
		reverse_iin_ma = data[0] | ((data[1] & 0xFF) << 8);
		mt_wls_log(PR_INFO, "reverse_iin_ma = %d\n", reverse_iin_ma);
	}

	return reverse_iin_ma;
}

static int mt5806_get_rx_chip_id(struct mt5806_dev *chip)
{
	int rc = 0;
	u8 data[4];

	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip is NULL\n", __func__);
		return -EINVAL;
	}

	if (chip->wls_fw_is_burning) {
		mt_wls_log(PR_ERROR, "wls fw is burning,return\n");
		return -EINVAL;
	}

	memset(data, 0, sizeof(data));
	rc = chip->bus.read_buf(chip, mt_comm_reg[MT_COMM_REG_CHIP_ID].reg_addr, data,
	     mt_comm_reg[MT_COMM_REG_CHIP_ID].reg_bytes_len);
	if (rc < 0) {
		chip->chip_id = 0;
		mt_wls_log(PR_ERROR, "read rx chip id failed\n");
		return -EINVAL;
	}

	chip->chip_id = data[0] | (data[1] << 8);
	mt_wls_log(PR_INFO, "rx chip_id = 0x%02X\n", chip->chip_id);

	return rc;
}

#define FW_VER_LEN 4
static int mt5806_get_app_fw_ver(struct mt5806_dev *chip)
{
	int i;
	int rc = 0;
	u8 data[FW_VER_LEN];
	u32 app_fw_ver = 0;

	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip is NULL\n", __func__);
		return -EINVAL;
	}

	if (chip->wls_fw_is_burning) {
		mt_wls_log(PR_ERROR, "wls fw is burning,return\n");
		return -EINVAL;
	}

	memset(data, 0, sizeof(data));
	rc = chip->bus.read_buf(chip, mt_comm_reg[MT_COMM_REG_FW_VER].reg_addr,
	                        data, mt_comm_reg[MT_COMM_REG_FW_VER].reg_bytes_len);
	if (rc < 0) {
		chip->app_fw_ver = 0;
		mt_wls_log(PR_ERROR, "read app fw ver failed\n");
		return -EINVAL;
	}
	rc = chip->bus.read_buf(chip, mt_comm_reg[MT_COMM_REG_FW_VER_MAJ].reg_addr,
	                        &data[2], mt_comm_reg[MT_COMM_REG_FW_VER_MAJ].reg_bytes_len);
	if (rc < 0) {
		chip->chip_id = 0;
		mt_wls_log(PR_ERROR, "read app fw ver failed\n");
		return -EINVAL;
	}

	for (i = FW_VER_LEN - 1; i >= 0; i--) {
		app_fw_ver |= data[i] << (i * 8);
	}
	chip->app_fw_ver = app_fw_ver;

	mt_wls_log(PR_ERROR, "app_fw_ver = 0x%02X\n", chip->app_fw_ver);
	return rc;
}

static void mt5806_read_chip_info(struct mt5806_dev *chip)
{
	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip is NULL\n", __func__);
		return;
	}

	if (chip->wls_fw_is_burning) {
		mt_wls_log(PR_ERROR, "wls fw is burning,return\n");
		return;
	}

	mt_set_gpio_value(chip, GP_2, 1);
	msleep(50);
	equipment_tx_wakeup(chip);

	mt5806_get_rx_chip_id(chip);
	mt5806_get_app_fw_ver(chip);
	mt5806_fw_q_mtp_get(chip);

	msleep(50);
	equipment_tx_wakeup_disable(chip);
}

static int mt5806_get_irq_flag(struct mt5806_dev *chip)
{
	int rc = 0;
	u8 data[4];

	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip is NULL\n", __func__);
		return -EINVAL;
	}

	if (chip->wls_fw_is_burning) {
		mt_wls_log(PR_ERROR, "wls fw is burning,return\n");
		return -EINVAL;
	}

	memset(data, 0, sizeof(data));
	rc = chip->bus.read_buf(chip, mt_comm_reg[MT_COMM_REG_INT_FLAG].reg_addr,
	                        data, mt_comm_reg[MT_COMM_REG_INT_FLAG].reg_bytes_len);
	if (rc < 0) {
		chip->int_flag = 0;
		mt_wls_log(PR_ERROR, "read interrupnt register failed\n");
		return -EINVAL;
	}

	chip->int_flag = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
	mt_wls_log(PR_INFO, "int_flag = 0x%08X\n", chip->int_flag);

	return rc;
}

/**************************************************************************************
 *                                 tx irq handler function                             *
 ***************************************************************************************/
static int mt5806_irq_tx_init(struct mt5806_dev *chip)
{
	int rc = 0;

	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip is NULL\n", __func__);
		return -EINVAL;
	}

	chip->pen_match_step = STEP_INIT;
	memset(chip->pen_addr_data, 0x0, sizeof(chip->pen_addr_data));
	memset(chip->pen_addr_buf, 0x0, sizeof(chip->pen_addr_buf));
	mt_wls_log(PR_INFO, "tx_irq init done,pen_match_step=%d\n", chip->pen_match_step);

	return rc;
}

static int mt5806_irq_none_handler(struct mt5806_dev *chip)
{
	return 0;
}

static int mt5806_irq_tx_init_done_handler(struct mt5806_dev *chip)
{
	return mt5806_irq_tx_init(chip);
}

static int mt5806_irq_tx_ping_handler(struct mt5806_dev *chip)
{
	mt_wls_log(PR_INFO, "tx_irq ping\n");

	return 0;
}

static int mt5806_irq_tx_receive_ssp_handler(struct mt5806_dev *chip)
{
	int rc = 0;
	struct timeval now_time;

	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip is NULL\n", __func__);
		return -EINVAL;
	}

	mt_wls_log(PR_INFO, "tx_irq ssp\n");
	do_gettimeofday(&now_time);
	chip->ping_succ_time = now_time.tv_sec * 1000 + now_time.tv_usec / 1000 - chip->tx_start_time;
	mt_wls_log(PR_INFO, "mt5806 ping_succ_time(%lld)\n",
	           chip->ping_succ_time);
	cancel_delayed_work(&chip->ping_timeout_work);

	return rc;
}

static int mt5806_irq_tx_receive_idp_handler(struct mt5806_dev *chip)
{
	int rc = 0;
	uint8_t data[PPP_DATA_SIZE + 1] = {0};

	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip is NULL\n", __func__);
		return -EINVAL;
	}

	rc = chip->bus.read_buf(chip, PPP_HEADER, data, PPP_DATA_SIZE);
	if (rc < 0) {
		mt_wls_log(PR_ERROR, "read id fail\n");
		return rc;
	}

	mt_wls_log(PR_INFO, "id:[0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x]\n",
	           data[0], data[1], data[2], data[3], data[4], data[5]);

	return rc;
}

static int mt5806_irq_tx_receive_cfgp_handler(struct mt5806_dev *chip)
{
	int rc = 0;
	uint8_t data[PPP_DATA_SIZE + 1] = {0};

	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip is NULL\n", __func__);
		return -EINVAL;
	}

	rc = chip->bus.read_buf(chip, PPP_HEADER, data, PPP_DATA_SIZE);
	if (rc < 0) {
		mt_wls_log(PR_ERROR, "read id fail\n");
		return rc;
	}

	mt_wls_log(PR_INFO, "cfg:[0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x]\n",
	           data[0], data[1], data[2], data[3], data[4], data[5]);

	return rc;
}

static uint64_t mt_recv_ble_mac_addr(uint8_t *data, size_t data_len)
{
	uint64_t ble_addr;
	uint8_t decode_addr[6];

	if (!data) {
		mt_wls_log(PR_ERROR, "[%s] data null\n", __func__);
		return 0;
	}

	if (data_len < MT_BLE_MAC_ADDR_MIN_LEN) {
		mt_wls_log(PR_ERROR, "[%s] data length array out of bounds\n",
			__func__);
		return 0;
	}

	decode_addr[5] = ((data[9] & 0x0F) << 4) | ((data[4] & 0xF0) >> 4);
	decode_addr[4] = ((data[8] & 0x0F) << 4) | ((data[3] & 0xF0) >> 4);
	decode_addr[3] = ((data[7] & 0x0F) << 4) | ((data[2] & 0xF0) >> 4);

	decode_addr[2] = ((data[4] & 0x0F) << 4) | ((data[9] & 0xF0) >> 4);
	decode_addr[1] = ((data[3] & 0x0F) << 4) | ((data[8] & 0xF0) >> 4);
	decode_addr[0] = ((data[2] & 0x0F) << 4) | ((data[7] & 0xF0) >> 4);

	ble_addr = decode_addr[5]  << 16 | decode_addr[4] << 8 | decode_addr[3];
	ble_addr = ble_addr << 24 | decode_addr[2] << 16 | decode_addr[1] << 8 | decode_addr[0];

	mt_wls_log(PR_DBG, "[%s]ble_addr = 0x%llx\n", __func__, ble_addr);

	return ble_addr;
}

static bool mt_ble_mac_valid(struct mt5806_dev *chip, uint8_t *data_mac, size_t data_len)
{
	uint64_t check_data = 0;

	if (!chip || !data_mac) {
		mt_wls_log(PR_ERROR, "[%s] chip or data_mac null\n", __func__);
		return false;
	}

	if (chip->ble_mac_addr == 0) {
		mt_wls_log(PR_ERROR, "[%s] ble_mac_addr is 0\n", __func__);
		return false;
	}

	if (data_len < MT_BLE_MAC_ADDR_MIN_LEN) {
		mt_wls_log(PR_ERROR, "[%s] data length array out of bounds\n",
			__func__);
		return false;
	}

	check_data = chip->mac_check_data;

	if (((data_mac[9] & 0xFF) ^ (data_mac[8] & 0xFF) ^ (data_mac[7] & 0xFF))
		!= ((check_data >> 8) & 0xFF))
		return false;

	if (((data_mac[4] & 0xFF) ^ (data_mac[3] & 0xFF) ^ (data_mac[2] & 0xFF))
		!= (check_data & 0xFF))
		return false;


	mt_wls_log(PR_INFO, "mt5806_valid_check: %02x %02x.\n",
		(int)((check_data >> 8) & 0xFF),
		(int)(check_data & 0xFF));
	return true;
}

static uint64_t mt_recv_mac_check_data(uint8_t *data)
{
	if (!data) {
		mt_wls_log(PR_ERROR, "[%s] data null\n", __func__);
		return 0;
	}
	return (uint64_t)(data[2] << 8 | data[3]);
}

static int mt_recv_pen_soc(uint8_t *data)
{
	if (!data) {
		mt_wls_log(PR_ERROR, "[%s] data null\n", __func__);
		return MT_WLS_FAIL;
	}
	return (int)data[2];
}

static void mt5806_send_uevent(struct device *dev, bool status, uint64_t mac_addr, uint8_t pen_id)
{
	char status_string[64] = {0};
	char addr_string[64] = {0};
	char pen_id_string[64] = {0};
	char *envp[] = {status_string, addr_string, pen_id_string, NULL};
	int ret = 0;

	snprintf(status_string, sizeof(status_string), "pencil_status=%d", status);
	snprintf(addr_string, sizeof(addr_string), "pencil_addr=%llx", mac_addr);
	snprintf(pen_id_string, sizeof(pen_id_string), "pencil_id=%x", pen_id);
	ret = kobject_uevent_env(&dev->kobj, KOBJ_CHANGE, envp);
	if (ret)
		mt_wls_log(PR_ERROR, "%s: kobject_uevent_fail, ret = %d",
		           __func__, ret);

	mt_wls_log(PR_INFO, "send uevent:%s, %s, %s.\n", status_string, addr_string, pen_id_string);
}

static void mt_set_chg_enable(struct mt5806_dev *chip, bool enable)
{
	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip null\n", __func__);
		return;
	}

	if (enable) {
		mt_set_gpio_value(chip, GP_3, 0);
		chip->charge_allow = 1;
	} else {
		mt_set_gpio_value(chip, GP_3, 1);
		chip->charge_allow = 0;
	}
}

static void mt5806_handle_mac_check_cmd(struct mt5806_dev *chip, uint8_t *data)
{
	if (!chip || !data)
		return;

	chip->mac_check_data = mt_recv_mac_check_data(data);
	mt_wls_log(PR_INFO, "[%s]get add_check\n", __func__);
	chip->pen_id = data[4];
	mt_wls_log(PR_INFO, "[%s]get pen_id %d\n", __func__, chip->pen_id);
}

static void mt5806_handle_mac_addr_pkg1(struct mt5806_mac_addr *mac_info, uint8_t *data)
{
	if (!mac_info || !data)
		return;

	memmove(mac_info->data_mac, data, sizeof(uint8_t) * MT_MAC_PKG_LEN);
	mac_info->mac_pkg_1 = true;
}

static void mt5806_handle_mac_addr_pkg2(struct mt5806_mac_addr *mac_info, uint8_t *data)
{
	if (!mac_info || !data)
		return;

	memmove(mac_info->data_mac + sizeof(uint8_t) * MT_MAC_PKG_LEN, data,
	       sizeof(uint8_t) * MT_MAC_PKG_LEN);
	mac_info->mac_pkg_2 = true;
}

static void mt5806_check_and_disable_charge_by_callname(struct mt5806_dev *chip)
{
	uint32_t tx_disable_callname;

	mutex_lock(&chip->tx_disable_callname_lock);
	tx_disable_callname = chip->tx_disable_callname;
	mutex_unlock(&chip->tx_disable_callname_lock);

	if (chip->pen_present == 1 && tx_disable_callname != 0)
		mt_set_chg_enable(chip, false);
}

static void mt5806_process_mac_addr(struct mt5806_dev *chip,
					struct mt5806_mac_addr *mac_info)
{
	struct timeval now_time;

	if (!chip || !mac_info)
		return;

	if (!mac_info->mac_pkg_1 || !mac_info->mac_pkg_2)
		return;

	mt_wls_log(PR_INFO, "data_mac=%d [0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x]\n",
		   chip->pen_match_step, mac_info->data_mac[0], mac_info->data_mac[1],
		   mac_info->data_mac[2], mac_info->data_mac[3], mac_info->data_mac[4],
		   mac_info->data_mac[5], mac_info->data_mac[6], mac_info->data_mac[7],
		   mac_info->data_mac[8], mac_info->data_mac[9]);

	chip->ble_mac_addr = mt_recv_ble_mac_addr(mac_info->data_mac, sizeof(mac_info->data_mac));
	if (chip->ble_mac_addr != 0) {
		do_gettimeofday(&now_time);
		chip->upto_ble_time = now_time.tv_sec * 1000 + now_time.tv_usec / 1000 -
				      chip->tx_start_time;
		mt_wls_log(PR_INFO, "[%s]get ble addr(%lld)\n", __func__,
			   chip->upto_ble_time);
	}

	if (mt_ble_mac_valid(chip, mac_info->data_mac, sizeof(mac_info->data_mac))) {
		chip->pen_present = 1;
		if (chip->power_enable_reason != PEN_REASON_RECHARGE) {
			mt5806_send_uevent(chip->wireless_dev, chip->pen_present,
					chip->ble_mac_addr, chip->pen_id);
			mt_wls_log(PR_INFO, "[%s]report %d\n", __func__, chip->pen_present);
		}
	} else {
		mt_set_chg_enable(chip, false);
	}

	mac_info->mac_pkg_1 = false;
	mac_info->mac_pkg_2 = false;

	mt5806_check_and_disable_charge_by_callname(chip);
}

static void mt5806_handle_power_cmd(struct mt5806_dev *chip, uint8_t *data)
{
	if (!chip || !data)
		return;

	chip->pen_soc = mt_recv_pen_soc(data);

	if (chip->pen_soc == 100) {
		mt_set_chg_enable(chip, false);
		chip->power_disable_reason = PEN_REASON_CHARGE_FULL;
		mt_wls_log(PR_INFO, "[%s] soc full\n", __func__);
		mt_track_upload(chip, MT_ERR_SOC_FULL);
	} else {
		mt_set_chg_enable(chip, false);
		chip->power_disable_reason = PEN_REASON_CHARGE_STOP;
		mt_wls_log(PR_INFO, "[%s] charge stop\n", __func__);
	}
}

static int mt5806_irq_tx_receive_pkt_handler(struct mt5806_dev *chip)
{
	int rc = 0;
	uint8_t data[PPP_DATA_SIZE + 1] = {0};
	static struct mt5806_mac_addr mac_info = {0};

	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip null\n", __func__);
		return -EINVAL;
	}

	rc = chip->bus.read_buf(chip, PPP_HEADER, data, PPP_DATA_SIZE);
	if (rc < 0) {
		mt_wls_log(PR_ERROR, "read id fail\n");
		return rc;
	}

	mt_wls_log(PR_INFO, "pen_match_step=%d [0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x]\n",
		   chip->pen_match_step, data[0], data[1], data[2], data[3], data[4], data[5]);

	if ((data[0] == MAC_PKG_HEAD) && (data[1] == MAC_PKG_CMD_CKECK)) {
		mac_info.mac_pkg_1 = false;
		mac_info.mac_pkg_2 = false;
		mt5806_handle_mac_check_cmd(chip, data);
	}

	if ((data[0] == MAC_PKG_HEAD) && (data[1] == MAC_PKG_CMD_ADD1)) {
		mt5806_handle_mac_addr_pkg1(&mac_info, data);
	}

	if ((data[0] == MAC_PKG_HEAD) && (data[1] == MAC_PKG_CMD_ADD2)) {
		mt5806_handle_mac_addr_pkg2(&mac_info, data);
	}

	mt5806_process_mac_addr(chip, &mac_info);

	if ((data[0] == POWER_PKG_HEAD) && (data[1] == POWER_PKG_CMD)) {
		mt5806_handle_power_cmd(chip, data);
	}

	return rc;
}

static bool mt5806_has_ept_error(u8 *data)
{
	u8 data1_mask;
	u8 data2_mask;

	if (!data)
		return false;

	data1_mask = MT_EPT_OCP | MT_EPT_OVP | MT_EPT_LVP | MT_EPT_FOD | MT_EPT_OTP;
	if (data[1] & data1_mask)
		return true;

	data2_mask = MT_EPT_PING_OVP | MT_EPT_PING_OCP;
	if (data[2] & data2_mask)
		return true;

	return false;
}

static enum mt_err_reason mt5806_get_ept_error_type(u8 *data)
{
	if (!data)
		return MT_ERR_NULL;

	if (data[1] & MT_EPT_OTP)
		return MT_ERR_OTP;
	if (data[1] & MT_EPT_FOD)
		return MT_ERR_FOD;
	if (data[1] & MT_EPT_LVP)
		return MT_ERR_LVP;
	if (data[1] & MT_EPT_OVP)
		return MT_ERR_OVP;
	if (data[1] & MT_EPT_OCP)
		return MT_ERR_OCP;

	if (data[2] & MT_EPT_PING_OVP)
		return MT_ERR_PING_OVP;
	if (data[2] & MT_EPT_PING_OCP)
		return MT_ERR_PING_OCP;

	return MT_ERR_NULL;
}

/* ept = end power transfe,ovp/ovp... */
static int mt5806_irq_tx_ept_handler(struct mt5806_dev *chip)
{
	int rc = 0;
	u8 data[4] = {0};
	enum mt_err_reason err_type = MT_ERR_NULL;

	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip is NULL\n", __func__);
		return -EINVAL;
	}

	rc = chip->bus.read_buf(chip, REG_EPT_TYPE, data, sizeof(data));
	if (rc < 0) {
		mt_wls_log(PR_ERROR, "[%s] ept type read fail\n", __func__);
		return rc;
	}

	mt_wls_log(PR_ERROR, "reg_ept=0x%x:data0=0x%02X,data1=0x%02xdata2=0x%02X,data3=0x%02x\n",
	           REG_EPT_TYPE, data[0], data[1], data[2], data[3]);

	if (mt5806_has_ept_error(data)) {
		err_type = mt5806_get_ept_error_type(data);
		mt_set_chg_enable(chip, false);
		mt_set_gpio_value(chip, GP_1, 1);
		chip->power_disable_reason = PEN_REASON_CHARGE_EPT;
		mt_wls_log(PR_ERROR, "wls_off_state_gpio=%d,scan_gpio=%d\n",
			gpio_get_value(chip->wls_off_state_gpio),
			gpio_get_value(chip->scan_mode_gpio));
		if (err_type != MT_ERR_NULL) {
			mt_track_upload(chip, err_type);
		}
	}

	rc = chip->bus.write_mask(chip, REG_COMMAND + 1, BIT6, BIT6);
	rc |= chip->bus.write(chip, MT5806_TX_CMD_SYNC_ADDR, MT5806_TX_CMD_SYNC_VAL);
	if (rc < 0)
		mt_wls_log(PR_ERROR, "MT5806_TX_CMD_SYNC_ADDR failed\n");

	return rc;
}

static int mt5806_irq_tx_rpp_to_handler(struct mt5806_dev *chip)
{
	return 0;
}

static int mt5806_irq_tx_cep_to_handler(struct mt5806_dev *chip)
{
	return 0;
}

static void power_expired_do_check(struct mt5806_dev *chip)
{
	struct timeval now_time;
	uint64_t time_offset = 0;

	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip is NULL\n", __func__);
		return;
	}

	mt_wls_log(PR_ERROR, "[%s] [%d %d]\n",  __func__, chip->charge_allow, chip->pen_present);

	if (chip->charge_allow && chip->pen_status == PEN_STATUS_NEAR) {
		do_gettimeofday(&now_time);
		time_offset = now_time.tv_sec * 1000 + now_time.tv_usec / 1000 - chip->tx_start_time;
		mt_wls_log(PR_ERROR, "[%s] time_offset = %lld\n", __func__, time_offset);
		if (time_offset > chip->power_expired_time * MIN_TO_MS) {
			mt_set_chg_enable(chip, false);
			chip->power_disable_reason = PEN_REASON_CHARGE_TIMEOUT;
		}
	}
}

static void error_attach_check_work_func(struct work_struct *work)
{
	int count = 0;
	struct mt5806_dev *chip =
		container_of(work, struct mt5806_dev, error_attach_check_work);

	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip null\n", __func__);
		return;
	}

	if (!chip->mt_wls_wake_lock) {
		mt_wls_log(PR_ERROR, "[%s] mt_wls_wake_lock is NULL\n", __func__);
		return;
	}

	__pm_stay_awake(chip->mt_wls_wake_lock);

	while ((count++ < ATTACH_WAIT_TIME) && (!chip->pen_present) && chip->charge_allow) {
		msleep(100); /* wait pen_present every 100ms*/
		mt_wls_log(PR_ERROR, "[%s] count %d\n", __func__, count);
		if (chip->pen_status == PEN_STATUS_FAR) {
			__pm_relax(chip->mt_wls_wake_lock);
			return;
		}
	}

	if (!chip->pen_present && chip->pen_status == PEN_STATUS_NEAR) {
		mt_wls_log(PR_ERROR, "[%s] set allow false %d\n", __func__, count);
		mt_set_chg_enable(chip, false);
	}

	__pm_relax(chip->mt_wls_wake_lock);
}

static void mt_track_record_reset(struct mt5806_dev *chip)
{
	struct mt_record_track *track;

	if (!chip) {
		mt_wls_log(PR_ERROR, "chip is null\n");
		return;
	}
	track = &chip->mt_record_track;
	track->start_time = 0;
	track->end_time = 0;
	track->start_soc = MT_SOC_INVALID;
	track->end_soc = MT_SOC_INVALID;
	track->reason_type = MT_ERR_NULL;
	track->pen_id = 0;
	return;
}

static void mt_track_upload(struct mt5806_dev *chip, enum mt_err_reason reason_type)
{
	struct mt_record_track *track;

	if (!chip) {
		mt_wls_log(PR_ERROR, "chip is null\n");
		return;
	}

	track = &chip->mt_record_track;
	track->pen_id = chip->pen_id;
	track->end_soc = chip->rx_soc > 0 ? chip->rx_soc : MT_SOC_INVALID;
	track->end_time = mt_get_local_time_s();
	track->reason_type = reason_type;

	if (track->end_time - track->start_time > MT_TRACK_TIME_THRESHOLD_SEC || reason_type != MT_ERR_NULL)
		schedule_work(&chip->track_record_upload_work);
	return;
}

static void mt_track_record_upload_work(struct work_struct *work)
{
	struct mt5806_dev *chip =
		container_of(work, struct mt5806_dev, track_record_upload_work);
	const char *err_reason;
	struct mt_record_track *track;

	if (!chip) {
		mt_wls_log(PR_ERROR, "chip is null\n");
		return;
	}

	track = &chip->mt_record_track;
	if (track->reason_type >= ARRAY_SIZE(mt_err_reason_text) || track->reason_type < 0) {
		mt_wls_log(PR_ERROR, "wls err reason inval\n");
		return;
	}
	mt_wls_log(PR_ERROR, "pen_id:%d, start_time:%ld, end_time:%ld, start_soc:%d, end_soc:%d, reason_type:%d\n",
		track->pen_id, track->start_time, track->end_time, track->start_soc, track->end_soc, track->reason_type);
	err_reason = mt_err_reason_text[track->reason_type];
	trace_wls_pen_chg_stat(get_timestamp_ms(), track->pen_id, track->start_time, track->end_time, track->start_soc,
	                        track->end_soc, err_reason);
	return;
}

static void mt_max_chg_time_check_work_func(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct mt5806_dev *chip =
		container_of(dwork, struct mt5806_dev, max_chg_time_check_work);

	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip null\n", __func__);
		return;
	}
	if (!chip->mt_wls_wake_lock) {
		mt_wls_log(PR_ERROR, "[%s] mt_wls_wake_lock is NULL\n", __func__);
		return;
	}
	__pm_stay_awake(chip->mt_wls_wake_lock);
	power_expired_do_check(chip);
	__pm_relax(chip->mt_wls_wake_lock);
}

static void mt_ping_timeout_work_func(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct mt5806_dev *chip =
		container_of(dwork, struct mt5806_dev, ping_timeout_work);

	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip null\n", __func__);
		return;
	}

	mt_track_upload(chip, MT_ERR_PING_TIMEOUT);
}

static void set_boost_by_attach_status(bool attach, struct mt5806_dev *chip)
{
	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip is NULL\n", __func__);
		return;
	}

	if (attach)
		chip->hboost_volt = chip->hboost_attach_volt;
	else
		chip->hboost_volt = chip->hboost_default_volt;

	mt_wls_log(PR_INFO, "attach status:%d, hboost_volt:%d!\n",
	           attach, chip->hboost_volt);
	schedule_work(&chip->set_boost_work);
}

static int mt5806_irq_rx_attach_handler(struct mt5806_dev *chip)
{
	int rc = 0;
	struct timeval now_time;

	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip is NULL\n", __func__);
		return -EINVAL;
	}

	mt_wls_log(PR_INFO, "Rx attach!\n");
	do_gettimeofday(&now_time);
	chip->tx_start_time = now_time.tv_sec * 1000 + now_time.tv_usec / 1000;
	if (chip->is_supply_by_hboost == IS_SUPPORT_BY_HBOOST)
		set_boost_by_attach_status(true, chip);
	else
		mt_set_chg_enable(chip, true);
	chip->pen_status = PEN_STATUS_NEAR;
	cancel_work_sync(&chip->error_attach_check_work);
	cancel_delayed_work_sync(&chip->max_chg_time_check_work);
	schedule_work(&chip->error_attach_check_work);
	schedule_delayed_work(&chip->max_chg_time_check_work,
		round_jiffies_relative(msecs_to_jiffies(chip->power_expired_time * MIN_TO_MS)));
	mt_track_record_reset(chip);
	chip->mt_record_track.start_soc = chip->rx_soc > 0 ? chip->rx_soc : MT_SOC_INVALID;
	chip->mt_record_track.start_time = mt_get_local_time_s();
	schedule_delayed_work(&chip->ping_timeout_work,
		msecs_to_jiffies(PING_EXPIRED_TIME_DEFAULT_SEC * 1000));

	return rc;
}

static int mt_wls_reset_sys(struct mt5806_dev *chip)
{
	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip is NULL\n", __func__);
		return -EINVAL;
	}

	if (chip->wls_fw_is_burning) {
		mt_wls_log(PR_INFO, "wls fw is burning,return\n");
		return -EBUSY;
	}

	mt5806_reset_power(chip);
	mt_set_gpio_value(chip, GP_0, 1);
	msleep(TX_WAKEUP_WAIT_MS);

	mt_set_gpio_value(chip, GP_0, 0);
	return 0;
}

static void mt_reset_chip_status(struct mt5806_dev *chip)
{
	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip null\n", __func__);
		return;
	}

	chip->pen_status = PEN_STATUS_FAR;
	chip->pen_present = 0;
	chip->ping_succ_time = 0;
	chip->upto_ble_time = 0;
	chip->tx_start_time = 0;
	chip->ble_mac_addr = 0;
	chip->mac_check_data = 0;
	chip->pen_id = 0;
	chip->tx_voltage = 0;
	chip->tx_current = 0;
	chip->rx_soc = 0;
	if (chip->power_disable_reason == PEN_REASON_CHARGE_EPT)
		(void)mt_wls_reset_sys(chip);
	chip->power_disable_reason = PEN_REASON_UNKNOWN;
	chip->power_enable_reason = PEN_REASON_UNDEFINED;
}

static int mt5806_irq_rx_remove_handler(struct mt5806_dev *chip)
{
	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip is NULL\n", __func__);
		return -EINVAL;
	}

	mt_wls_log(PR_INFO, "Rx remove!\n");
	if (chip->led_on)
		mt_set_gpio_value(chip, GP_1, 1);
	else
		mt_set_gpio_value(chip, GP_1, 0);
	chip->pen_present = 0;
	set_boost_by_attach_status(false, chip);
	mt5806_send_uevent(chip->wireless_dev, chip->pen_present,
	                   chip->ble_mac_addr, chip->pen_id);
	cancel_delayed_work(&chip->max_chg_time_check_work);
	mt_track_upload(chip, MT_ERR_NULL);
	mt_reset_chip_status(chip);
	mt_set_chg_enable(chip, true);

	return 0;
}

static int mt5806_tx_irq_qcali_done_handler(struct mt5806_dev *chip)
{
	int rc = 0;

	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip is NULL\n", __func__);
		return -EINVAL;
	}

	chip->cali_done = true;
	mt_wls_log(PR_INFO, "qcali_done\n");

	return rc;
}

static int mt5806_irq_tx_pen_pos_error_handler(struct mt5806_dev *chip)
{
	int rc = 0;

	if (!chip || !chip->wireless_dev) {
		mt_wls_log(PR_ERROR, "[%s] chip or wireless_dev is NULL\n", __func__);
		return -EINVAL;
	}

	mt_wls_log(PR_INFO, "mt5806_irq_tx_pen_pos_error_handler\n");
	mt5806_send_mislocated_uevent(chip->wireless_dev);
	if (rc)
		mt_wls_log(PR_ERROR, "kobject_uevent_fail, ret=%d", rc);

	return rc;
}
static int mt5806_irq_tx_pen_pos_error_remove_handler(struct mt5806_dev *chip)
{
	int rc = 0;
	char *pen_pos_error[2] = { "pencil_mislocated=0", NULL };

	if (!chip || !chip->wireless_dev) {
		mt_wls_log(PR_ERROR, "[%s] chip or wireless_dev is NULL\n", __func__);
		return -EINVAL;
	}

	mt_wls_log(PR_INFO, "mt5806_irq_tx_pen_pos_error_remove_handler\n");
	rc = kobject_uevent_env(&(chip->wireless_dev->kobj), KOBJ_CHANGE, pen_pos_error);
	if (rc)
		mt_wls_log(PR_ERROR, "kobject_uevent_fail, ret=%d", rc);
	else
		mt_wls_log(PR_INFO, "send pencil_mislocated=0 uevent\n");

	return rc;
}

static struct mt5806_irq_info mt5806_tx_irq = {
	.irq_flag_reg	= 0x0014,
	.irq_clr_reg	= 0x0018,
	.irq_ss_reg	= 0x00A4, /* invalid */
	.val		= 0,
	.pre_val	= 0,
	.irq_handler = {
		{.name = "tx_receive_ssp", /* BIT0 */
		 .handler = mt5806_irq_tx_receive_ssp_handler,
		},
		{.name = "tx_receive_idp", /* BIT1 */
		 .handler = mt5806_irq_tx_receive_idp_handler,
		},
		{.name = "tx_receive_cfgp", /* BIT2 */
		 .handler = mt5806_irq_tx_receive_cfgp_handler,
		},
		{.name = "tx_int_none", /* BIT3 */
		 .handler = mt5806_irq_none_handler,
		},
		{.name = "tx_int_none", /* BIT4 */
		 .handler = mt5806_irq_none_handler,
		},
		{.name = "tx_int_none", /* BIT5 */
		 .handler = mt5806_irq_none_handler,
		},
		{.name = "tx_int_rx_remove", /* BIT6 */
		 .handler = mt5806_irq_rx_remove_handler,
		},
		{.name = "tx_int_none", /* BIT7 */
		 .handler = mt5806_irq_none_handler,
		},
		{.name = "tx_int_none", /* BIT8 */
		 .handler = mt5806_irq_none_handler,
		},
		{.name = "tx_init_done", /* BIT9 */
		 .handler = mt5806_irq_tx_init_done_handler,
		},
		{.name = "tx_int_none", /* BIT10 */
		 .handler = mt5806_irq_none_handler,
		},
		{.name = "tx_receive_pkt", /* BIT11 */
		 .handler = mt5806_irq_tx_receive_pkt_handler,
		},
		{.name = "tx_int_none", /* BIT12 */
		 .handler = mt5806_irq_none_handler,
		},
		{.name = "tx_int_none", /* BIT13 */
		 .handler = mt5806_irq_none_handler,
		},
		{.name = "tx_int_none", /* BIT14 */
		 .handler = mt5806_irq_none_handler,
		},
		{.name = "tx_ept", /* BIT15 */
		 .handler = mt5806_irq_tx_ept_handler,
		},
		{.name = "tx_int_none", /* BIT16 */
		 .handler = mt5806_irq_none_handler,
		},
		{.name = "tx_ping", /* BIT17 */
		 .handler = mt5806_irq_tx_ping_handler,
		},
		{.name = "tx_int_none", /* BIT18 */
		 .handler = mt5806_irq_none_handler,
		},
		{.name = "tx_int_none", /* BIT19 */
		 .handler = mt5806_irq_none_handler,
		},
		{.name = "tx_cep_to", /* BIT20 */
		 .handler = mt5806_irq_tx_cep_to_handler,
		},
		{.name = "tx_rpp_to", /* BIT21 */
		 .handler = mt5806_irq_tx_rpp_to_handler,
		},
		{.name = "tx_int_none", /* BIT22 */
		 .handler = mt5806_irq_none_handler,
		},
		{.name = "tx_int_none", /* BIT23 */
		 .handler = mt5806_irq_none_handler,
		},
		{.name = "tx_int_none", /* BIT24 */
		 .handler = mt5806_irq_none_handler,
		},
		{.name = "tx_int_pen_attach", /* BIT25 */
		 .handler = mt5806_irq_rx_attach_handler,
		},
		{.name = "pen_pos_error_remove", /* BIT26 */
		 .handler = mt5806_irq_tx_pen_pos_error_remove_handler,
		},
		{.name = "tx_int_qcali", /* BIT27 */
		 .handler = mt5806_tx_irq_qcali_done_handler,
		},
		{.name = "pen_pos_error", /* BIT28 */
		 .handler = mt5806_irq_tx_pen_pos_error_handler,
		},
		{.name = "tx_int_none", /* BIT29 */
		 .handler = mt5806_irq_none_handler,
		},
		{.name = "tx_int_none", /* BIT30 */
		 .handler = mt5806_irq_none_handler,
		},
		{.name = "tx_int_none", /* BIT31 */
		 .handler = mt5806_irq_none_handler,
		},
	},
};

/**************************************************************************************
 *                                 fw program function                                 *
 ***************************************************************************************/
static void mt5806_program_awake(struct mt5806_dev *chip)
{
	if (!chip || !chip->program_wake_lock)
		return;
	mutex_lock(&chip->pm_lock);
	__pm_stay_awake(chip->program_wake_lock);
	mutex_unlock(&chip->pm_lock);
}

static void mt5806_program_relax(struct mt5806_dev *chip)
{
	if (!chip || !chip->program_wake_lock)
		return;
	mutex_lock(&chip->pm_lock);
	__pm_relax(chip->program_wake_lock);
	mutex_unlock(&chip->pm_lock);
};

/**************************************************************************************
 *                                 Q program function                                 *
 ***************************************************************************************/
static int mt5806_fw_q_mtp_get(struct mt5806_dev *chip)
{
	int ret;
	u16 q_val_crc = 0;
	u16 crc = 0;
	union mtp_q_data q_mtp_temp;

	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip is NULL\n", __func__);
		return -EINVAL;
	}

	ret = mt5806_read_buffer(chip, MT5806_TX_MTP_CNT_ADDR, q_mtp_temp.q_mtp_data, 10);
	if (ret) {
		mt_wls_log(PR_ERROR, "fw load: get mtp q value failed\n");
		return ret;
	}

	q_val_crc = q_mtp_temp.q_info.mt5806_q_val_verf_crc;
	crc = mt5806_fw_crc16_cal(q_mtp_temp.q_mtp_data, 8);
	mt_wls_log(PR_INFO, "mt5806_q_val_verf_crc:%d, mt5806_q_mtp:%d, mt5806_f_mtp:%d,"
			"mt5806_q_mtp_varnce:%d, mt5806_f_mtp_varnce:%d, crc:%d, q_val_crc:%d\n",
			q_mtp_temp.q_info.mt5806_q_val_verf_crc, q_mtp_temp.q_info.mt5806_q_mtp,
			q_mtp_temp.q_info.mt5806_f_mtp, q_mtp_temp.q_info.mt5806_q_mtp_varnce,
			q_mtp_temp.q_info.mt5806_f_mtp_varnce, crc, q_val_crc);

	if (crc == q_val_crc) {
		chip->q_mtp_data = q_mtp_temp;
		return MT_WLS_SUCCESS;
	} else {
		return MT_WLS_FAIL;
	}
}

static int mt5806_fw_q_cali_get(struct mt5806_dev *chip)
{
	int ret;
	u16 q_val_crc = 0;
	u16 crc = 0;
	union mtp_q_data q_mtp_temp;

	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip is NULL\n", __func__);
		return -EINVAL;
	}

	ret = mt5806_read_buffer(chip, MT5806_TX_CALI_CNT_ADDR, q_mtp_temp.q_mtp_data, 12);
	if (ret) {
		mt_wls_log(PR_ERROR, "fw load: get mtp q value failed\n");
		return ret;
	}

	q_val_crc = q_mtp_temp.q_info.mt5806_q_val_verf_crc;
	crc = mt5806_fw_crc16_cal(q_mtp_temp.q_mtp_data, 8);

	mt_wls_log(PR_INFO, "mt5806_q_val_verf_crc:%d, mt5806_q_mtp:%d, mt5806_f_mtp:%d,"
	           "mt5806_q_mtp_varnce:%d, mt5806_f_mtp_varnce:%d, crc:%d, q_val_crc:%d\n",
	           q_mtp_temp.q_info.mt5806_q_val_verf_crc, q_mtp_temp.q_info.mt5806_q_mtp, q_mtp_temp.q_info.mt5806_f_mtp,
	           q_mtp_temp.q_info.mt5806_q_mtp_varnce, q_mtp_temp.q_info.mt5806_f_mtp_varnce, crc, q_val_crc);

	if (crc == q_val_crc) {
		chip->q_cali_data = q_mtp_temp;
		return MT_WLS_SUCCESS;
	} else {
		return MT_WLS_FAIL;
	}
}

static int mt5806_mtp_status_check(struct mt5806_dev *chip, u16 expect_status)
{
	int i;
	int ret;
	u16 status = 0;

	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip is NULL\n", __func__);
		return -EINVAL;
	}

	/* wait for 10ms*50=500ms for status check, typically 300ms */
	for (i = 0; i < 300; i++) {
		msleep(1);
		ret = mt5806_read_word(chip, MT5806_BOOT_STATUS_ADDR, &status);
		if (ret) {
			mt_wls_log(PR_ERROR, "status_check: read failed\n");
			return ret;
		}
		if (status == expect_status)
			return MT_WLS_SUCCESS;
	}

	mt_wls_log(PR_INFO, "status %x\n", status);
	return MT_WLS_FAIL;
}

static int mt5806_get_major_fw_version(struct mt5806_dev *chip, u16 *fw)
{
	if (!chip || !fw) {
		mt_wls_log(PR_ERROR, "[%s] chip or fw is NULL\n", __func__);
		return -EINVAL;
	}
	return mt5806_read_word(chip, MT5806_MTP_MAJOR_ADDR, fw);
}

static int mt5806_get_minor_fw_version(struct mt5806_dev *chip, u16 *fw)
{
	if (!chip || !fw) {
		mt_wls_log(PR_ERROR, "[%s] chip or fw is NULL\n", __func__);
		return -EINVAL;
	}
	return mt5806_read_word(chip, MT5806_MTP_MINOR_ADDR, fw);
}

static int mt5806_mtp_version_check(struct mt5806_dev *chip)
{
	int ret;
	u16 major_fw_ver = 0;
	u16 minor_fw_ver = 0;

	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip is NULL\n", __func__);
		return -EINVAL;
	}

	ret = mt5806_get_major_fw_version(chip, &major_fw_ver);
	if (ret)
		return ret;
	mt_wls_log(PR_INFO, "[version_check] major_fw=0x%04x\n", major_fw_ver);

	ret = mt5806_get_minor_fw_version(chip, &minor_fw_ver);
	if (ret)
		return ret;
	mt_wls_log(PR_INFO, "[version_check] minor_fw=0x%04x\n", minor_fw_ver);

	if ((major_fw_ver != chip->bin_fw_ver_major) || (minor_fw_ver != chip->bin_fw_ver_minor))
		return MT_WLS_FAIL;

	return 0;
}

#define MT_FW_CRC_CALI_EXCEPT_ADDR 0x7800
#define MT_FW_CRC_EXCEPT_LEN 10
static int mt5806_mtp_crc_check(struct mt5806_dev *chip)
{
	int ret;
	u16 verify_result;
	u16 crc = 0;
	unsigned char *firmware_tmp;
	int i = 0;

	if (!chip || !chip->firmware_data) {
		mt_wls_log(PR_ERROR, "[%s] chip or firmware_data is NULL\n", __func__);
		return -EINVAL;
	}

	firmware_tmp = kmalloc(chip->fw_data_length, GFP_KERNEL);
	if (firmware_tmp != NULL) {
		memset(firmware_tmp, 0, chip->fw_data_length);
		memcpy(firmware_tmp, chip->firmware_data, chip->fw_data_length);
		/* 0x7800 ~ 0x7809 crc check jump */
		for (i = MT_FW_CRC_CALI_EXCEPT_ADDR; i < MT_FW_CRC_CALI_EXCEPT_ADDR + MT_FW_CRC_EXCEPT_LEN; i++)
			firmware_tmp[i] = 0;
	} else {
		mt_wls_log(PR_ERROR, "crc_check: error , firmware_tmp kmalloc err\n");
		return MT_WLS_FAIL;
	}
	crc = mt5806_mtp_fw_crc(firmware_tmp, chip->fw_data_length);
	mt_wls_log(PR_INFO, "crc=0x%x, fw_data_length:%x\n", crc, chip->fw_data_length);
	ret = mt5806_write_word(chip, MT5806_FW_LENGTH_ADDR, chip->fw_data_length);
	ret += mt5806_write_word(chip, MT5806_FW_CRC16VALUE_ADDR, crc);
	ret += mt5806_masked_write(chip, REG_COMMAND + 1, BIT7, BIT7);
	ret += mt5806_write(chip, MT5806_TX_CMD_SYNC_ADDR, MT5806_TX_CMD_SYNC_VAL);
	kvfree(firmware_tmp);

	msleep(100); /* for power on, typically 100ms */

	ret += mt5806_read_word(chip, MT5806_FW_VERIFY_STATUS_ADDR, &verify_result);
	if (ret) {
		mt_wls_log(PR_ERROR, "crc_check: cmd error\n");
		return ret;
	}

	if (verify_result == MT5806_FW_VERIFY_STATUS_OK_VAL) {
		mt_wls_log(PR_INFO, "[crc_check] succ\n");
		return 0;
	}
	mt_wls_log(PR_ERROR, "crc_check: error %x\n", verify_result);
	return MT_WLS_FAIL;
}

static int mt5806_check_mtp_match(struct mt5806_dev *chip)
{
	int ret;

	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip is NULL\n", __func__);
		return -EINVAL;
	}

	ret = mt5806_mtp_version_check(chip);
	if (ret)
		return ret;

	ret = mt5806_mtp_crc_check(chip);
	if (ret)
		return ret;

	return 0;
}

static int mt5806_mtp_load_fw(struct mt5806_dev *chip, u16 start_addr, const u8 *data, u16 len)
{
	int i;
	int ret;
	u16 addr = start_addr; /* start from adrr */
	u16 remaining = len;
	u16 wr_size = 0;
	u16 chksum = 0;
	u32 wr_cnt = 0;
	u8 temp_data[MT5806_MTP_PGM_SIZE + 4] = {0};

	if (!chip || !data) {
		mt_wls_log(PR_ERROR, "[%s] chip or data is NULL\n", __func__);
		return MT_WLS_FAIL;
	}

	while (remaining > 0) {
		wr_cnt++;
		wr_size = remaining > MT5806_MTP_PGM_SIZE ? MT5806_MTP_PGM_SIZE : remaining;
		ret = mt5806_write_dword(chip, MT5806_BOOT_PGM_ADDR_ADDR, addr);
		if (ret) {
			mt_wls_log(PR_ERROR, "load_fw: write addr failed\n");
			return ret;
		}

		ret = mt5806_write_word(chip, MT5806_BOOT_PGM_LEN_ADDR, wr_size);
		if (ret) {
			mt_wls_log(PR_ERROR, "load_fw: write len failed\n");
			return ret;
		}
		if (addr == MT5806_MTP_Q_ADDR) {
			memcpy(temp_data, &data[addr - start_addr], wr_size);
			memcpy(temp_data, chip->q_mtp_data.q_mtp_data, 10);

			chksum = addr + wr_size;
			for (i = 0; i < wr_size; i++)
				chksum += temp_data[i];
			ret = mt5806_write_word(chip, MT5806_BOOT_PGM_VERIFY_ADDR, chksum);
			if (ret) {
				mt_wls_log(PR_ERROR, "load_fw: write checksum failed\n");
				return ret;
			}

			ret = mt5806_write_buffer(chip, MT5806_BOOT_PGM_BUFFER_ADDR,
				(u8 *)&temp_data[0], wr_size);
			if (ret) {
				mt_wls_log(PR_ERROR, "load_fw: write mtp_data failed\n");
				return ret;
			}
		} else {
			chksum = addr + wr_size;
			for (i = 0; i < wr_size; i++)
				chksum += data[addr - start_addr + i];
			ret = mt5806_write_word(chip, MT5806_BOOT_PGM_VERIFY_ADDR, chksum);
			if (ret) {
				mt_wls_log(PR_ERROR, "load_fw: write checksum failed\n");
				return ret;
			}

			ret = mt5806_write_buffer(chip, MT5806_BOOT_PGM_BUFFER_ADDR,
				(u8 *)&data[addr - start_addr], wr_size);
			if (ret) {
				mt_wls_log(PR_ERROR, "load_fw: write mtp_data failed\n");
				return ret;
			}
		}

		ret = mt5806_write_word(chip, MT5806_BOOT_CTRL_ADDR, MT5806_BOOT_CTRL_WRITE_CMD);
		if (ret) {
			mt_wls_log(PR_ERROR, "load_fw: start programming failed\n");
			return ret;
		}

		ret = mt5806_mtp_status_check(chip, MT5806_BOOT_STATUS_WRITE_OK_VAL);
		if (ret) {
			mt_wls_log(PR_ERROR, "load_fw: check mtp status failed\n");
			return ret;
		}
		addr += wr_size;
		remaining -= wr_size;
	}

	return 0;
}

static int mt5806_mtp_load_bootloader(struct mt5806_dev *chip)
{
	int ret;
	int remaining = ARRAY_SIZE(g_mt5806_bootloader);
	int size_to_wr;
	int wr_already = 0;
	u16 chip_id = 0;
	u16 addr = MT5806_BTLOADR_ADDR;
	u8 wr_buff[MT5806_MTP_PGM_SIZE] = { 0 };

	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip is NULL\n", __func__);
		return -EINVAL;
	}

	while (remaining > 0) {
		size_to_wr = remaining > MT5806_MTP_PGM_SIZE ? MT5806_MTP_PGM_SIZE : remaining;
		memcpy(wr_buff, g_mt5806_bootloader + wr_already, size_to_wr);
		ret = mt5806_write_buffer(chip, addr, wr_buff, size_to_wr);
		if (ret) {
			mt_wls_log(PR_ERROR, "load_bootloader: failed, addr=0x%04x\n", addr);
			return ret;
		}
		addr += size_to_wr;
		wr_already += size_to_wr;
		remaining -= size_to_wr;
	}

	ret = mt5806_write(chip, MT5806_M0_CTRL_ADDR, MT5806_M0_RST_VAL);
	if (ret) {
		mt_wls_log(PR_ERROR, "load_bootloader: reset M0 failed\n");
		return ret;
	}
	msleep(50); /* for power on, typically 50ms */
	ret = mt5806_read_word(chip, MT5806_BOOTLOADER_CHIPID_ADDR, &chip_id);
	if (ret)
		return ret;

	mt_wls_log(PR_INFO, "[load_bootloader] chip_id=0x%x\n", chip_id);
	if (chip_id != MT5806_CHIP_ID)
		return MT_WLS_FAIL;

	mt_wls_log(PR_INFO, "[load_bootloader] succ\n");
	return 0;
}

static int mt5806_mtp_pwr_cycle_chip(struct mt5806_dev *chip)
{
	int ret;

	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip is NULL\n", __func__);
		return -EINVAL;
	}

	ret = mt5806_write(chip, MT5806_PMU_WDGEN_ADDR, MT5806_WDG_DISABLE); /* disable wtd */
	ret += mt5806_write(chip, MT5806_PMU_WDGEN_ADDR, MT5806_WDG_DISABLE); /* disable wtd */
	ret += mt5806_write(chip, MT5806_PMU_WDGEN_ADDR, MT5806_WDG_DISABLE); /* disable wtd */
	ret += mt5806_write(chip, MT5806_PMU_FLAG_ADDR, MT5806_WDT_INTFALG); /* clear wtd flag */
	ret += mt5806_write(chip, MT5806_SYS_KEY_ADDR, MT5806_KEY_VAL); /* write key to map */
	ret += mt5806_write(chip, MT5806_M0_CTRL_ADDR, MT5806_M0_HOLD_VAL); /* hold M0 */
	ret += mt5806_write(chip, MT5806_SYS_CLK_ADDR, MT5806_SYS_CLK_VAL); /* enable pwm clk */
	ret += mt5806_write(chip, MT5806_PWM_DRV_ADDR, MT5806_PWM_DISABLE); /* disable pwm */
	ret += mt5806_write(chip, MT5806_DMOD_SRCSEL_ADDR, MT5806_DMOD_DMA_DISABLE); /* disable demod dma */
	ret += mt5806_write(chip, MT5806_CODE_REMAP_ADDR, MT5806_CODE_REMAP_VAL); /* select mtp map addr */
	ret += mt5806_write(chip, MT5806_M0_CTRL_ADDR, MT5806_M0_RST_VAL); /* reset M0 */
	ret += mt5806_write(chip, MT5806_SYS_KEY_ADDR, MT5806_KEY_VAL); /* write key to map */
	ret += mt5806_write(chip, MT5806_M0_CTRL_ADDR, MT5806_M0_HOLD_VAL); /* hold M0 */

	if (ret) {
		mt_wls_log(PR_ERROR, "pwr_cycle_chip: failed\n");
		return MT_WLS_FAIL;
	}
	msleep(50);
	return 0;
}

static int mt5806_copy_firmware_data(struct mt5806_dev *chip)
{
	if (!chip || !chip->firm_data_bin) {
		mt_wls_log(PR_ERROR, "[%s] request firmware fail\n", __func__);
		return MT_WLS_FAIL;
	}

	chip->fw_data_length = (int)chip->firm_data_bin->size;
	chip->firmware_data = kmalloc(chip->fw_data_length, GFP_KERNEL);
	if (!chip->firmware_data) {
		mt_wls_log(PR_ERROR, "[%s] alloc firmware data fail\n", __func__);
		return MT_WLS_FAIL;
	}

	memset(chip->firmware_data, 0, chip->fw_data_length);
	memmove(chip->firmware_data, chip->firm_data_bin->data, chip->fw_data_length);

	return MT_WLS_SUCCESS;
}

static int mt5806_request_firmware(struct mt5806_dev *chip)
{
	char *firmware_path = NULL;
	int retry = 0;
	int ret = 0;

	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip null\n", __func__);
		return MT_WLS_FAIL;
	}

	if (!chip->dev) {
		mt_wls_log(PR_ERROR, "[%s] chip->dev null\n", __func__);
		return MT_WLS_FAIL;
	}

	firmware_path = kzalloc(MAX_FW_NAME_LENGTH, GFP_KERNEL);
	if (firmware_path == NULL) {
		mt_wls_log(PR_ERROR, "[%s] firmware_path alloc fail\n", __func__);
		return MT_WLS_FAIL;
	}

	snprintf(firmware_path, MAX_FW_NAME_LENGTH,
		"wireless_pen/%d/mt5806_firmware.bin", get_project());
	for (retry = 0; retry < MAX_RETRY; retry++) {
		ret = request_firmware(&chip->firm_data_bin, firmware_path, chip->dev);
		if (ret < 0)
			mt_wls_log(PR_ERROR, "[%s]Failed to request firmware retry %d\n",
				__func__, retry);
		else
			break;
	}

	if (ret < 0) {
		kfree(firmware_path);
		return MT_WLS_FAIL;
	}

	ret = mt5806_copy_firmware_data(chip);
	if (ret != MT_WLS_SUCCESS) {
		kfree(firmware_path);
		return MT_WLS_FAIL;
	}

	mt_wls_log(PR_ERROR, "[%s] fw_data count = 0x%x\n", __func__, chip->fw_data_length);
	kfree(firmware_path);
	return MT_WLS_SUCCESS;
}

#define FIRMWARE_VERSION_POS_BASE	0x14
static void mt_get_firmware_version(struct mt5806_dev *chip)
{
	u8 data[FW_VER_LEN] = {0};
	u32 app_fw_ver = 0;
	int ret;
	int i = 0;
	u32 fw_ver_pos;

	if (!chip || !chip->firmware_data) {
		mt_wls_log(PR_ERROR, "[%s] chip or firmware_data is NULL\n", __func__);
		return;
	}

	/* Get firmware version position */
	fw_ver_pos = ((chip->firmware_data[FIRMWARE_VERSION_POS_BASE]) |
			 (chip->firmware_data[FIRMWARE_VERSION_POS_BASE + 1] << 8) |
			 (chip->firmware_data[FIRMWARE_VERSION_POS_BASE + 2] << 16) |
			 (chip->firmware_data[FIRMWARE_VERSION_POS_BASE + 3] << 24)) + 4;
	mt_wls_log(PR_INFO, "fw_ver_pos : %x\n", fw_ver_pos);

	/* Parse firmware version */
	chip->bin_fw_ver_minor = chip->firmware_data[fw_ver_pos + 1] << 8 | chip->firmware_data[fw_ver_pos];
	chip->bin_fw_ver_major = chip->firmware_data[fw_ver_pos + 3] << 8 | chip->firmware_data[fw_ver_pos + 2];
	chip->fw.version = (chip->bin_fw_ver_major << 16) | chip->bin_fw_ver_minor;
	mt_wls_log(PR_INFO, "bin_fw_ver_minor : %x, bin_fw_ver_major: %x, version:%x\n", chip->bin_fw_ver_minor,
	           chip->bin_fw_ver_major, chip->fw.version);

	/* Read current device firmware version */
	ret = chip->bus.read_buf(chip, mt_comm_reg[MT_COMM_REG_FW_VER].reg_addr,
			       data, mt_comm_reg[MT_COMM_REG_FW_VER].reg_bytes_len);
	if (ret < 0) {
		chip->app_fw_ver = 0;
		mt_wls_log(PR_ERROR, "read app fw ver failed\n");
		return;
	}

	ret = chip->bus.read_buf(chip, mt_comm_reg[MT_COMM_REG_FW_VER_MAJ].reg_addr,
			       &data[2], mt_comm_reg[MT_COMM_REG_FW_VER_MAJ].reg_bytes_len);
	if (ret < 0) {
		chip->app_fw_ver = 0;
		mt_wls_log(PR_ERROR, "read app fw ver failed\n");
		return;
	}

	for (i = FW_VER_LEN - 1; i >= 0; i--) {
		app_fw_ver |= data[i] << (i * 8);
		mt_wls_log(PR_ERROR, "mt5806_get_app_fw_ver data-i: %d-%d\n", data[i], i);
	}
	chip->app_fw_ver = app_fw_ver;

	mt_wls_log(PR_INFO, "mt5806_fw check bin_ver:0x%x app_ver:0x%x\n", chip->fw.version, chip->app_fw_ver);

	return;
}

static int mt_load_firmware_to_device(struct mt5806_dev *chip)
{
	int ret = 0;

	if (!chip || !chip->firmware_data) {
		mt_wls_log(PR_ERROR, "[%s] chip or firmware_data is NULL\n", __func__);
		return -EINVAL;
	}

	/* Step1: Load to SRAM */
	ret = mt5806_mtp_pwr_cycle_chip(chip);
	if (ret) {
		mt_wls_log(PR_ERROR, "Chip hold FAIL\n");
		return ret;
	}
	msleep(50);

	/* Step2: Load bootloader */
	mt_wls_log(PR_ERROR, "START LOAD SRAM HEX!\n");
	ret = mt5806_mtp_load_bootloader(chip);
	if (ret) {
		mt_wls_log(PR_ERROR, "BOOTLOADER downloder FAIL\n");
		return ret;
	}
	mt_wls_log(PR_ERROR, "LOAD BOOTLOADER SUCCESSFUL\n");

	/* Step3: Erase MTP */
	ret = mt5806_write_word(chip, MT5806_BOOT_CTRL_ADDR, MT5806_BOOT_CTRL_MTP_ERASE_CMD);
	msleep(10);
	ret += mt5806_mtp_status_check(chip, MT5806_BOOT_STATUS_ERASE_OK_VAL);
	if (ret) {
		mt_wls_log(PR_ERROR, "ERASE FAIL\n");
		return ret;
	}

	/* Step4: Load firmware to MTP */
	mt_wls_log(PR_ERROR, "START LOAD APP HEX\n");
	ret = mt5806_mtp_load_fw(chip, 0, chip->firmware_data, chip->fw_data_length);
	if (ret) {
		mt_wls_log(PR_ERROR, "WRITE BUFFER0 DATA TO MTP FAIL\n");
		return ret;
	}
	mt_wls_log(PR_ERROR, "LOAD APP HEX SUCCESSFUL\n");

	return 0;
}

static int mt5806_program_fw_load_and_verify(struct mt5806_dev *chip)
{
	int ret = 0;

	if (!chip)
		return MT_WLS_FAIL;

	ret = mt_load_firmware_to_device(chip);
	if (ret)
		return ret;

	/* Update firmware version and verify */
	chip->app_fw_ver = chip->fw.version;
	chip->wls_fw_is_burning = false;
	mt5806_reset_power(chip);
	msleep(100);

	ret = mt5806_check_mtp_match(chip);
	if (ret) {
		mt_wls_log(PR_ERROR, "FW CRC error\n");
		return ret;
	}

	mt_wls_log(PR_INFO, "CHERK APP CRC SUCCESSFUL\n");
	return MT_WLS_SUCCESS;
}

static bool mt5806_program_fw_downloader(struct mt5806_dev *chip, bool check_fw_version)
{
	int ret = 0;

	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip is NULL\n", __func__);
		return false;
	}

	mutex_lock(&chip->program_lock);
	mt5806_program_awake(chip);
	chip->wls_fw_is_burning = true;
	equipment_tx_wakeup(chip);

	/* Request firmware */
	ret = mt5806_request_firmware(chip);
	if (ret != MT_WLS_SUCCESS) {
		mt_wls_log(PR_ERROR, "[%s] request firmware fail\n", __func__);
		goto update_fail;
	}

	/* Verify firmware data is valid */
	if (!chip->firmware_data) {
		mt_wls_log(PR_ERROR, "[%s] firmware_data is NULL after request\n", __func__);
		goto update_fail;
	}

	/* Check firmware version */
	mt_get_firmware_version(chip);
	if (check_fw_version) {
		if (chip->fw.version == chip->app_fw_ver) {
			ret = mt5806_mtp_crc_check(chip);
			if (ret) {
				mt_wls_log(PR_ERROR, "FW CRC error\n");
				goto load_firmware;
			}
			chip->wls_fw_is_burning = false;
			equipment_tx_wakeup_disable(chip);
			mt_wls_log(PR_INFO, "fw_version matched, NO need update fw\n");
			chip->wls_fw_update_result = WLS_FW_UPDATE_RESULT_SUCCESS;
			mt5806_program_relax(chip);
			mutex_unlock(&chip->program_lock);
			return true;
		}
	}

load_firmware:
	/* Load firmware to device and verify */
	ret = mt5806_program_fw_load_and_verify(chip);
	if (ret != MT_WLS_SUCCESS)
		goto update_fail;

	mt_wls_log(PR_INFO, "update successfully\n");
	equipment_tx_wakeup_disable(chip);
	chip->wls_fw_update_result = WLS_FW_UPDATE_RESULT_SUCCESS;
	mt5806_program_relax(chip);
	mutex_unlock(&chip->program_lock);
	return true;

update_fail:
	mt_wls_log(PR_INFO, "update fail(%s)\n", wls_fw_update_result_str[chip->wls_fw_update_result]);
	equipment_tx_wakeup_disable(chip);
	chip->wls_fw_is_burning = false;
	chip->wls_fw_update_result = WLS_FW_UPDATE_RESULT_ERROR;
	mt5806_program_relax(chip);
	mutex_unlock(&chip->program_lock);
	return false;
}

static void mt5806_program_fw(struct mt5806_dev *chip)
{
	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip is NULL\n", __func__);
		return;
	}

	mt_wls_log(PR_ERROR, "fw program start ~\n");
	mt5806_read_chip_info(chip);

	mt_set_gpio_value(chip, GP_2, 1);
	msleep(50);
	mt5806_program_fw_downloader(chip, true);
}

static int equipment_tx_wakeup(struct mt5806_dev *chip)
{
	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip is NULL\n", __func__);
		return -EINVAL;
	}

	mt_set_gpio_value(chip, GP_0, 1);
	msleep(TX_WAKEUP_WAIT_MS);

	return 0;
}

static void equipment_tx_wakeup_disable(struct mt5806_dev *chip)
{
	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip is NULL\n", __func__);
		return;
	}

	mt_set_gpio_value(chip, GP_0, 0);
}

static int mt5806_equipment_mode_enable(struct mt5806_dev *chip, bool enable)
{
	int rc;
	u8 cmd;

	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip is NULL\n", __func__);
		return -EINVAL;
	}

	if (enable)
		cmd = EQUIPMENT_MODE_ENTER;
	else
		cmd = EQUIPMENT_MODE_EXIT;

	rc = chip->bus.write(chip, REG_EQUIPMENT_MODE_SET, cmd);
	if (rc < 0) {
		mt_wls_log(PR_ERROR, "write REG_EQUIPMENT_MODE_SET failed\n");
		return rc;
	}

	if (enable)
		chip->equipment_mode = 1;
	else
		chip->equipment_mode = 0;

	return 0;
}

/* Enter equipment mode and perform the production line Q-value calibration process. */
static int mt5806_equipment_calibration_check(struct mt5806_dev *chip)
{
	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip is NULL\n", __func__);
		return -EINVAL;
	}

	if (chip->wls_fw_is_burning) {
		mt_wls_log(PR_INFO, "wls fw is burning, return\n");
		return -EBUSY;
	}

	return 0;
}

static void mt5806_equipment_work(struct work_struct *work)
{
	struct mt5806_dev *chip = container_of(work, struct mt5806_dev, equipment_work.work);
	int rc = 0;
	int i = 0;
	int j = 0;

	mt_wls_log(PR_INFO, "start\n");
	rc = mt5806_equipment_calibration_check(chip);
	if (rc < 0)
		return;

	chip->q_cali_status = Q_CALI_IN_PROGRESS;
	mt5806_reset_power(chip);
	msleep(100);
	mt_set_chg_enable(chip, true);
	mt_wls_log(PR_INFO, "#equipment_tx_wakeup\n");
	rc = equipment_tx_wakeup(chip);
	if (rc < 0)
		mt_wls_log(PR_ERROR, "equipment_tx_wakeup failed\n");

	mt_wls_log(PR_INFO, "#set equipment_mode\n");
	mt5806_equipment_mode_enable(chip, true);
	for (j = 3; j > 0; j--) {
		mt_wls_log(PR_INFO, "#try 3 times calibrate, j=%d \n", j);
		chip->cali_done = false;
		rc = chip->bus.write_mask(chip, REG_COMMAND + 2, CMD_START_Q_CALI_BIT, CMD_START_Q_CALI_BIT);
		rc |= chip->bus.write(chip, MT5806_TX_CMD_SYNC_ADDR, MT5806_TX_CMD_SYNC_VAL);
		if (rc < 0)
			mt_wls_log(PR_ERROR, "write_mask CMD_START_Q_CALI_BIT failed\n");

		i = 40;
		do {
			if (chip->cali_done)
				goto CALI_DONE;
			msleep(100);
		} while (i--);

		mt_wls_log(PR_INFO, "#wait cali_done i=%d\n", i);
		if (i < 0) {
			mt_wls_log(PR_ERROR, "error cali_done timeout!\n");
			continue;
		}
	}

CALI_DONE:
	if (chip->cali_done && mt5806_fw_q_cali_get(chip) == 0) {
		chip->q_mtp_data = chip->q_cali_data;
		chip->q_cali_result.q_cali_cnt = chip->q_cali_data.q_info.mt5806_q_mtp;
		chip->q_cali_result.q_cali_width = chip->q_cali_data.q_info.mt5806_f_mtp;
		chip->q_cali_result.q_cali_cnt_var = chip->q_cali_data.q_info.mt5806_q_mtp_varnce;
		chip->q_cali_result.q_cali_width_var = chip->q_cali_data.q_info.mt5806_f_mtp_varnce;
		chip->q_cali_status = Q_CALI_SUCCESS;
	} else {
		mt_wls_log(PR_ERROR, "cali_result failed\n");
		chip->q_cali_status = Q_CALI_FAIL;
		goto CALI_ERROR;
	}

	if (is_between(chip->std_rx_cnt[0], chip->std_rx_cnt[1], chip->q_cali_result.q_cali_cnt)
		&& is_between(chip->std_rx_width[0], chip->std_rx_width[1], chip->q_cali_result.q_cali_width)) {
		mt5806_program_fw_downloader(chip, false);
		mt_wls_log(PR_INFO, "between std value\n");
	} else {
		mt_wls_log(PR_ERROR, "error not between std value\n");
		goto CALI_ERROR;
	}

	chip->equipment_mode = 0;

	mt5806_equipment_mode_enable(chip, false);
	equipment_tx_wakeup_disable(chip);
	mt5806_reset_power(chip);
	mt_wls_log(PR_INFO, "q calibrate calisuccessfully, cali_rx_cnt=%d,cali_rx_width=%d,"
	           "cali_cnt_variance=%d,cali_width_variance=%d\n",
	           chip->q_cali_result.q_cali_cnt, chip->q_cali_result.q_cali_width,
	           chip->q_cali_result.q_cali_cnt_var, chip->q_cali_result.q_cali_width_var);
	return;

CALI_ERROR:
	memset(&chip->q_cali_result, 0, sizeof(chip->q_cali_result));
	chip->equipment_mode = 0;
	chip->q_cali_status = Q_CALI_FAIL;
	mt5806_equipment_mode_enable(chip, false);
	equipment_tx_wakeup_disable(chip);
}

static int mt_set_boost_volt_mv(int volt_mv)
{
	uint8_t reg_value = 0;
	int volt_temp = volt_mv;

	if (volt_temp > MAX_BOOST_VOLT_MV)
		volt_temp = MAX_BOOST_VOLT_MV;
	else if (volt_temp < MIN_BOOST_VOLT_MV)
		volt_temp = MIN_BOOST_VOLT_MV;

	reg_value = (uint8_t)((volt_temp - 2000) / 50);
	return wireless_pen_send_hboost_volt_req(reg_value);
}

static void set_boost_work_func(struct work_struct *work)
{
	struct mt5806_dev *chip =
		container_of(work, struct mt5806_dev, set_boost_work);
	int retry_count = BOOST_SET_RETRY;
	int rc;

	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip null\n", __func__);
		return;
	}
	if (!chip->mt_wls_wake_lock) {
		mt_wls_log(PR_ERROR, "[%s] mt_wls_wake_lock is NULL\n", __func__);
		return;
	}
	__pm_stay_awake(chip->mt_wls_wake_lock);

	if (chip->is_supply_by_hboost != IS_SUPPORT_BY_HBOOST) {
		mt_wls_log(PR_ERROR, "[%s]not use hboost\n", __func__);
		__pm_relax(chip->mt_wls_wake_lock);
		return;
	}
	chip->hboost_status = HBOOST_IS_SETTING_BOOST;
	while (retry_count > 0) {
		rc = mt_set_boost_volt_mv(chip->hboost_volt);
		if (rc != 0) {
			retry_count--;
			msleep(BOOST_SET_DELAY_500MS); /* try again after 500ms*/
			mt_wls_log(PR_ERROR, "[%s]set boost fail, retry = %d\n",
				__func__, retry_count);
			continue;
		} else {
			mt_set_chg_enable(chip, true);
			mt_wls_log(PR_ERROR, "[%s] set boost success\n", __func__);
			chip->hboost_status = HBOOST_SET_SUCCESS;
			break;
		}
	}
	if (retry_count <= 0)
		chip->hboost_status = HBOOST_SET_FAIL;
	__pm_relax(chip->mt_wls_wake_lock);
}


#define IRQ_BIT_MASK	0x01
#define IRQ_BIT_SHITF	1
static irqreturn_t mt5806_wls_irq_handler(int irq, void *dev_id)
{
	struct mt5806_dev *chip = (struct mt5806_dev *)dev_id;
	int i = 0;
	u32 irq_pending;
	int rc = 0;

	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip null\n", __func__);
		return IRQ_HANDLED;
	}

	mt_wls_log(PR_INFO, "start\n");

	if (!chip->mt_iic_wake_lock) {
		mt_wls_log(PR_ERROR, "[%s] mt_iic_wake_lock is NULL\n", __func__);
		return IRQ_HANDLED;
	}

	__pm_stay_awake(chip->mt_iic_wake_lock);
	wait_event_interruptible_timeout(i2c_waiter, chip->i2c_ready, msecs_to_jiffies(50));
	if (!chip->i2c_ready) {
		mt_wls_log(PR_ERROR, "[%s]iic not ready\n", __func__);
		__pm_relax(chip->mt_iic_wake_lock);
		return IRQ_HANDLED;
	}

	mutex_lock(&chip->irq_lock);

	if (mt5806_get_irq_flag(chip) < 0) {
		mt_wls_log(PR_ERROR, "read irq flag failed\n");
		mutex_unlock(&chip->irq_lock);
		__pm_relax(chip->mt_iic_wake_lock);
		return IRQ_HANDLED;
	}

	mt5806_tx_irq.val = chip->int_flag;
	mt_wls_log(PR_INFO, "mt5806_tx_irq.val=0x%02X\n", mt5806_tx_irq.val);
	for (i = 0; i < ARRAY_SIZE(mt5806_tx_irq.irq_handler); i++) {
		irq_pending = mt5806_tx_irq.val & (IRQ_BIT_MASK << (i * IRQ_BIT_SHITF));
		if (irq_pending && mt5806_tx_irq.irq_handler[i].handler != NULL)
			mt5806_tx_irq.irq_handler[i].handler(chip);
	}
	rc = mt5806_write_dword(chip, MT5806_TX_IRQ_CLR_ADDR, chip->int_flag);
	rc |= chip->bus.write_mask(chip, REG_COMMAND, BIT5, BIT5);
	rc |= chip->bus.write(chip, MT5806_TX_CMD_SYNC_ADDR, MT5806_TX_CMD_SYNC_VAL);
	if (rc < 0)
		mt_wls_log(PR_ERROR, "MT5806_TX_IRQ_CLR_ADDR failed\n");

	mt5806_get_irq_flag(chip);
	mt5806_tx_irq.pre_val = mt5806_tx_irq.val;
	mutex_unlock(&chip->irq_lock);
	__pm_relax(chip->mt_iic_wake_lock);

	mt_wls_log(PR_INFO, "finish\n");

	return IRQ_HANDLED;
}

static void mt5806_lock_work_init(struct mt5806_dev *chip)
{
	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip is NULL\n", __func__);
		return;
	}

	mutex_init(&chip->i2c_mutex);
	mutex_init(&chip->irq_lock);
	mutex_init(&chip->pm_lock);
	mutex_init(&chip->program_lock);
	mutex_init(&chip->tx_disable_callname_lock);

	chip->program_wake_lock = wakeup_source_register(NULL, "wls_program_wake_lock");
	chip->mt_wls_wake_lock = wakeup_source_register(NULL, "mt_wls_wake_lock");
	chip->mt_iic_wake_lock = wakeup_source_register(NULL, "mt_iic_wake_lock");
}

static void mt5806_mutex_destroy(struct mt5806_dev *chip)
{
	if (mutex_is_locked(&chip->i2c_mutex))
		mutex_unlock(&chip->i2c_mutex);
	mutex_destroy(&chip->i2c_mutex);

	if (mutex_is_locked(&chip->irq_lock))
		mutex_unlock(&chip->irq_lock);
	mutex_destroy(&chip->irq_lock);

	if (mutex_is_locked(&chip->pm_lock))
		mutex_unlock(&chip->pm_lock);
	mutex_destroy(&chip->pm_lock);

	if (mutex_is_locked(&chip->program_lock))
		mutex_unlock(&chip->program_lock);
	mutex_destroy(&chip->program_lock);

	if (mutex_is_locked(&chip->tx_disable_callname_lock))
		mutex_unlock(&chip->tx_disable_callname_lock);
	mutex_destroy(&chip->tx_disable_callname_lock);
}

static void mt5806_wakeup_source_destroy(struct mt5806_dev *chip)
{
	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip is NULL\n", __func__);
		return;
	}

	if (chip->program_wake_lock) {
		wakeup_source_unregister(chip->program_wake_lock);
		chip->program_wake_lock = NULL;
	}

	if (chip->mt_wls_wake_lock) {
		wakeup_source_unregister(chip->mt_wls_wake_lock);
		chip->mt_wls_wake_lock = NULL;
	}

	if (chip->mt_iic_wake_lock) {
		wakeup_source_unregister(chip->mt_iic_wake_lock);
		chip->mt_iic_wake_lock = NULL;
	}
}

static void mt5806_lock_work_destroy(struct mt5806_dev *chip)
{
	static bool mutex_destroyed = false;

	if (mutex_destroyed)
		return;

	mt5806_mutex_destroy(chip);
	mt5806_wakeup_source_destroy(chip);
	mutex_destroyed = true;

	return;
}

static const struct regmap_config mt5806_regmap32_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = 0xFFFF,
};

static const struct regmap_config mt5806_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = 0xFFFF,
};

static void mt5806_parse_charge_params(struct mt5806_dev *chip, struct device_node *node)
{
	int rc = 0;

	if (!chip || !node)
		return;

	rc = of_property_read_u32(node, "rx,soc_threshold",
		&chip->soc_threshould);
	if (rc)
		chip->soc_threshould = SOC_THRESHOLD;

	rc = of_property_read_u32(node, "tx,enable_expired_time",
		&chip->power_expired_time);
	if (rc)
		chip->power_expired_time = POWER_EXPIRED_TIME_DEFAULT;
	mt_wls_log(PR_INFO, "enable_expired_time[%d]\n",
		chip->power_expired_time);

	chip->support_camera_tx_disable = of_property_read_bool(node, "support_camera_tx_disable");
	mt_wls_log(PR_INFO, "support_camera_tx_disable[%d]\n",
		chip->support_camera_tx_disable);
}

static void mt5806_parse_cali_params(struct mt5806_dev *chip, struct device_node *node)
{
	int rc;

	if (!chip || !node)
		return;

	rc = of_property_read_u32_array(node, "oplus,std-rx-cnt", chip->std_rx_cnt, 2);
	if (rc) {
		chip->std_rx_cnt[0] = 1;
		chip->std_rx_cnt[1] = 1000;
	}
	mt_wls_log(PR_INFO, "std_rx_cnt = [%d %d]\n",
		   chip->std_rx_cnt[0], chip->std_rx_cnt[1]);

	rc = of_property_read_u32_array(node, "oplus,std-rx-width", chip->std_rx_width, 2);
	if (rc) {
		chip->std_rx_width[0] = 1;
		chip->std_rx_width[1] = 10000;
	}
	mt_wls_log(PR_INFO, "std_rx_width = [%d %d]\n",
		   chip->std_rx_width[0], chip->std_rx_width[1]);
}

static void mt5806_parse_hboost_params(struct mt5806_dev *chip, struct device_node *node)
{
	int rc = 0;

	if (!chip || !node)
		return;

	rc = of_property_read_u32(node, "qcom,pen_is_supply_by_hboost",
				  &chip->is_supply_by_hboost);
	if (rc)
		chip->is_supply_by_hboost = 0;
	mt_wls_log(PR_INFO, "is_supply_by_hboost[%d]\n", chip->is_supply_by_hboost);

	rc = of_property_read_u32(node, "qcom,hboost_default_volt_mv",
				&chip->hboost_default_volt);
	if (rc)
		chip->hboost_default_volt = DEFAULT_BOOST_VOLT_MV;

	rc = of_property_read_u32(node, "qcom,hboost_attach_volt_mv",
				  &chip->hboost_attach_volt);
	if (rc)
		chip->hboost_attach_volt = chip->hboost_default_volt;

	chip->hboost_volt = chip->hboost_default_volt;
	mt_wls_log(PR_INFO, "hboost_default_volt[%d], hboost_attach_volt[%d]\n",
		   chip->hboost_default_volt, chip->hboost_attach_volt);
}

static int mt5806_parse_dt(struct mt5806_dev *chip)
{
	struct device_node *node;

	if (!chip || !chip->dev) {
		mt_wls_log(PR_ERROR, "[%s] chip or dev is NULL\n", __func__);
		return -EINVAL;
	}

	node = chip->dev->of_node;
	if (!node) {
		mt_wls_log(PR_ERROR, "device tree node missing\n");
		return -EINVAL;
	}

	mt5806_parse_charge_params(chip, node);
	mt5806_parse_cali_params(chip, node);
	mt5806_parse_hboost_params(chip, node);

	return 0;
}

static int mt_wls_irq_gpio_init(struct mt5806_dev *chip)
{
	if (!chip || !chip->dev) {
		mt_wls_log(PR_ERROR, "[%s]: chip or dev is null!\n", __func__);
		return -EINVAL;
	}

	chip->mt_pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chip->mt_pinctrl)) {
		mt_wls_log(PR_ERROR, "get pinctrl fail\n");
		return -EINVAL;
	}

	chip->wls_irq_gpio_active =
		pinctrl_lookup_state(chip->mt_pinctrl, "wls_charge_int_active");
	if (IS_ERR_OR_NULL(chip->wls_irq_gpio_active)) {
		mt_wls_log(PR_ERROR, "get wls_irq_gpio_active fail\n");
		return -EINVAL;
	}

	chip->wls_irq_gpio_sleep =
		pinctrl_lookup_state(chip->mt_pinctrl, "wls_charge_int_sleep");
	if (IS_ERR_OR_NULL(chip->wls_irq_gpio_sleep)) {
		mt_wls_log(PR_ERROR, "get wls_irq_gpio_sleep fail\n");
		return -EINVAL;
	}

	chip->wls_irq_gpio_default =
		pinctrl_lookup_state(chip->mt_pinctrl, "wls_charge_int_default");
	if (IS_ERR_OR_NULL(chip->wls_irq_gpio_default)) {
		mt_wls_log(PR_ERROR, "get wls_irq_gpio_default fail\n");
		return -EINVAL;
	}

	if (chip->wls_irq_gpio > 0)
		gpio_direction_input(chip->wls_irq_gpio);

	pinctrl_select_state(chip->mt_pinctrl, chip->wls_irq_gpio_active);

	return 0;
}

static int mt_wls_off_state_gpio_init(struct mt5806_dev *chip)
{
	if (!chip || !chip->dev) {
		mt_wls_log(PR_ERROR, "[%s]: chip or dev is null!\n", __func__);
		return -EINVAL;
	}

	chip->mt_pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chip->mt_pinctrl)) {
		mt_wls_log(PR_ERROR, "get pinctrl fail\n");
		return -EINVAL;
	}

	chip->wls_off_state_gpio_active =
		pinctrl_lookup_state(chip->mt_pinctrl, "wls_off_state_active");
	if (IS_ERR_OR_NULL(chip->wls_off_state_gpio_active)) {
		mt_wls_log(PR_ERROR, "get wls_off_state_gpio_active fail\n");
		return -EINVAL;
	}

	chip->wls_off_state_gpio_sleep =
		pinctrl_lookup_state(chip->mt_pinctrl, "wls_off_state_sleep");
	if (IS_ERR_OR_NULL(chip->wls_off_state_gpio_sleep)) {
		mt_wls_log(PR_ERROR, "get wls_off_state_gpio_sleep fail\n");
		return -EINVAL;
	}

	chip->wls_off_state_gpio_default =
		pinctrl_lookup_state(chip->mt_pinctrl, "wls_off_state_default");
	if (IS_ERR_OR_NULL(chip->wls_off_state_gpio_default)) {
		mt_wls_log(PR_ERROR, "get wls_off_state_gpio_default fail\n");
		return -EINVAL;
	}

	gpio_direction_output(chip->wls_off_state_gpio, 0);
	pinctrl_select_state(chip->mt_pinctrl, chip->wls_off_state_gpio_active);

	return 0;
}


static int mt_wls_sleep_gpio_init(struct mt5806_dev *chip)
{
	if (!chip || !chip->dev) {
		mt_wls_log(PR_ERROR, "[%s]: chip or dev is null!\n", __func__);
		return -EINVAL;
	}

	chip->mt_pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chip->mt_pinctrl)) {
		mt_wls_log(PR_ERROR, "get pinctrl fail\n");
		return -EINVAL;
	}

	chip->wls_sleep_gpio_active =
		pinctrl_lookup_state(chip->mt_pinctrl, "wls_sleep_active");
	if (IS_ERR_OR_NULL(chip->wls_sleep_gpio_active)) {
		mt_wls_log(PR_ERROR, "get wls_sleep_gpio_active fail\n");
		return -EINVAL;
	}

	chip->wls_sleep_gpio_sleep =
		pinctrl_lookup_state(chip->mt_pinctrl, "wls_sleep_sleep");
	if (IS_ERR_OR_NULL(chip->wls_sleep_gpio_sleep)) {
		mt_wls_log(PR_ERROR, "get wls_sleep_gpio_sleep fail\n");
		return -EINVAL;
	}

	chip->wls_sleep_gpio_default =
		pinctrl_lookup_state(chip->mt_pinctrl, "wls_sleep_default");
	if (IS_ERR_OR_NULL(chip->wls_sleep_gpio_default)) {
		mt_wls_log(PR_ERROR, "get wls_sleep_gpio_default fail\n");
		return -EINVAL;
	}

	gpio_direction_output(chip->wls_sleep_gpio, 1);
	pinctrl_select_state(chip->mt_pinctrl, chip->wls_sleep_gpio_active);

	return 0;
}


static int mt_scan_mode_gpio_init(struct mt5806_dev *chip)
{
	if (!chip || !chip->dev) {
		mt_wls_log(PR_ERROR, "[%s]: chip or dev is null!\n", __func__);
		return -EINVAL;
	}

	chip->mt_pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chip->mt_pinctrl)) {
		mt_wls_log(PR_ERROR, "get pinctrl fail\n");
		return -EINVAL;
	}

	chip->scan_mode_gpio_active =
		pinctrl_lookup_state(chip->mt_pinctrl, "wls_scan_active");
	if (IS_ERR_OR_NULL(chip->scan_mode_gpio_active)) {
		mt_wls_log(PR_ERROR, "get scan_mode_gpio_active fail\n");
		return -EINVAL;
	}

	chip->scan_mode_gpio_sleep =
		pinctrl_lookup_state(chip->mt_pinctrl, "wls_scan_sleep");
	if (IS_ERR_OR_NULL(chip->scan_mode_gpio_sleep)) {
		mt_wls_log(PR_ERROR, "get scan_mode_gpio_sleep fail\n");
		return -EINVAL;
	}

	chip->scan_mode_gpio_default =
		pinctrl_lookup_state(chip->mt_pinctrl, "wls_scan_default");
	if (IS_ERR_OR_NULL(chip->scan_mode_gpio_default)) {
		mt_wls_log(PR_ERROR, "get scan_mode_gpio_default fail\n");
		return -EINVAL;
	}

	gpio_direction_output(chip->scan_mode_gpio, 0);
	pinctrl_select_state(chip->mt_pinctrl, chip->scan_mode_gpio_sleep);

	return 0;
}


static int mt_wls_sw_en_gpio_init(struct mt5806_dev *chip)
{
	if (!chip || !chip->dev) {
		mt_wls_log(PR_ERROR, "[%s]: chip or dev is null!\n", __func__);
		return -EINVAL;
	}

	chip->mt_pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chip->mt_pinctrl)) {
		mt_wls_log(PR_ERROR, "get pinctrl fail\n");
		return -EINVAL;
	}

	chip->wls_sw_en_active =
		pinctrl_lookup_state(chip->mt_pinctrl, "wls_sw_en_active");
	if (IS_ERR_OR_NULL(chip->wls_sw_en_active)) {
		mt_wls_log(PR_ERROR, "get wls_sw_en_active fail\n");
		return -EINVAL;
	}

	chip->wls_sw_en_sleep =
		pinctrl_lookup_state(chip->mt_pinctrl, "wls_sw_en_sleep");
	if (IS_ERR_OR_NULL(chip->wls_sw_en_sleep)) {
		mt_wls_log(PR_ERROR, "get wls_sw_en_sleep fail\n");
		return -EINVAL;
	}

	chip->wls_sw_en_default =
		pinctrl_lookup_state(chip->mt_pinctrl, "wls_sw_en_default");
	if (IS_ERR_OR_NULL(chip->wls_sw_en_default)) {
		mt_wls_log(PR_ERROR, "get wls_sw_en_default fail\n");
		return -EINVAL;
	}

	gpio_direction_output(chip->wls_sw_en_gpio, 0);
	pinctrl_select_state(chip->mt_pinctrl, chip->wls_sw_en_sleep);

	return 0;
}


static int get_gpio_with_defer_check(struct device_node *node, const char *name, int *gpio)
{
	int retry;
	int gpio_val;

	if (!node || !name || !gpio) {
		mt_wls_log(PR_ERROR, "Invalid params\n");
		return -EINVAL;
	}

	for (retry = 0; retry < 10; retry++) {
		gpio_val = of_get_named_gpio(node, name, 0);
		if (gpio_val >= 0)
			break;
		msleep(100);
	}

	*gpio = gpio_val;
	return gpio_val;
}

static int init_required_gpio(struct mt5806_dev *chip, const char *name, int *gpio,
		const char *label, int (*init_func)(struct mt5806_dev *))
{
	int gpio_val;
	static int defer_err = 0;

	if (!chip || !chip->dev->of_node) {
		mt_wls_log(PR_ERROR, "Invalid params\n");
		return -EINVAL;
	}

	gpio_val = get_gpio_with_defer_check(chip->dev->of_node, name, gpio);
	if (gpio_val == -EPROBE_DEFER && defer_err < 2) {
		mt_wls_log(PR_ERROR, "%s: GPIO controller not ready, deferring probe\n", name);
		defer_err++;
		return -EPROBE_DEFER;
	}

	if (gpio_val < 0) {
		mt_wls_log(PR_ERROR, "%s not specified after 10 retries, gpio_val:%d\n", name, gpio_val);
		return -EINVAL;
	}

	if (!gpio_is_valid(*gpio)) {
		mt_wls_log(PR_ERROR, "Invalid gpio %s:%d\n", name, *gpio);
		return -EINVAL;
	}

	if (gpio_request(*gpio, label))
		mt_wls_log(PR_ERROR, "Request %s failed, GPIO may be already in use\n", name);


	if (init_func(chip)) {
		mt_wls_log(PR_ERROR, "Init %s failed\n", name);
		return -EINVAL;
	}

	mt_wls_log(PR_INFO, "%s init ok:%d\n", name, *gpio);
	return 0;
}

static int init_irq_gpio(struct mt5806_dev *chip)
{
	int ret = 0;

	ret = init_required_gpio(chip, "wls_int_gpio", &chip->wls_irq_gpio,
			   "wls-int", mt_wls_irq_gpio_init);
	if (ret != 0)
		return ret;

	chip->wls_irq = gpio_to_irq(chip->wls_irq_gpio);
	mt_wls_log(PR_INFO, "IRQ init:%d\n", chip->wls_irq);
	return 0;
}

static int mt5806_wls_gpio_init(struct mt5806_dev *chip)
{
	int ret = 0;

	if (!chip || !chip->dev->of_node) {
		mt_wls_log(PR_ERROR, "Invalid params\n");
		return -EINVAL;
	}
	ret = init_irq_gpio(chip);
	if (ret != 0)
		return ret;

	ret = init_required_gpio(chip, "wls_off_state_gpio", &chip->wls_off_state_gpio,
		"wls-off-state", mt_wls_off_state_gpio_init);
	if (ret != 0)
		return ret;

	ret = init_required_gpio(chip, "wls_sleep_gpio", &chip->wls_sleep_gpio,
		"wls-sleep", mt_wls_sleep_gpio_init);
	if (ret != 0)
		return ret;

	ret = init_required_gpio(chip, "scan_mode_gpio", &chip->scan_mode_gpio,
		"scan-mode", mt_scan_mode_gpio_init);
	if (ret != 0)
		return ret;

	ret = init_required_gpio(chip, "wls_sw_en_gpio", &chip->wls_sw_en_gpio,
		"wls-sw-en", mt_wls_sw_en_gpio_init);
	if (ret != 0)
		return ret;

	return ret;
}

static void rechg_work_func(struct work_struct *work)
{
	struct mt5806_dev *chip =
		container_of(work, struct mt5806_dev, rechg_work);
	struct timeval now_time;

	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip null\n", __func__);
		return;
	}
	if (!chip->mt_wls_wake_lock) {
		mt_wls_log(PR_ERROR, "[%s] mt_wls_wake_lock is NULL\n", __func__);
		return;
	}
	__pm_stay_awake(chip->mt_wls_wake_lock);

	if ((chip->rx_soc > chip->soc_threshould) || (chip->pen_status != PEN_STATUS_NEAR) ||
	    (chip->charge_allow != false) || (chip->power_disable_reason != PEN_REASON_CHARGE_FULL) ||
		chip->tx_disable_callname != 0) {
		mt_wls_log(PR_ERROR, "enable failed for (%d %d %d %d %d).\n",
			chip->rx_soc, chip->pen_status, chip->charge_allow, chip->power_disable_reason,
			chip->tx_disable_callname);
		__pm_relax(chip->mt_wls_wake_lock);
		return;
	}

	do_gettimeofday(&now_time);
	chip->tx_start_time =
		now_time.tv_sec * 1000 + now_time.tv_usec / 1000;
	chip->power_enable_reason = PEN_REASON_RECHARGE;
	schedule_work(&chip->error_attach_check_work);
	cancel_delayed_work_sync(&chip->max_chg_time_check_work);
	mt_set_chg_enable(chip, true);
	schedule_delayed_work(&chip->max_chg_time_check_work,
		round_jiffies_relative(msecs_to_jiffies(
			chip->power_expired_time * MIN_TO_MS)));

	__pm_relax(chip->mt_wls_wake_lock);
}


static int mt_wls_get_no_rx_cnt(struct mt5806_dev *chip)
{
	u8 cnt[2] = {0};
	int rc = 0;
	int curr_rx_cnt = 0;

	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip null\n", __func__);
		return rc;
	}

	rc = chip->bus.read_buf(chip, REG_NO_RX_CNT, cnt, 2);
	if (rc < 0) {
		mt_wls_log(PR_ERROR, "read REG_NO_RX_CNT failed\n");
		return rc;
	}
	curr_rx_cnt = cnt[0] | (cnt[1] << 8);
	return curr_rx_cnt;
}

static int mt_wls_get_no_rx_width(struct mt5806_dev *chip)
{
	int rc = 0;
	u8 no_rx_width[2] = {0};
	int curr_rx_width;

	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip null\n", __func__);
		return rc;
	}

	rc = chip->bus.read_buf(chip, REG_NO_RX_WIDTH_L, no_rx_width, 2);
	if (rc < 0) {
		mt_wls_log(PR_ERROR, "read REG_NO_RX_WIDTH_L failed\n");
		return rc;
	}
	curr_rx_width = no_rx_width[0] | (no_rx_width[1] << 8);

	return curr_rx_width;
}

#define UEVENT_TRIGGER_COOLDOWN_TIME_MS (300)
static void mt5806_send_mislocated_uevent(struct device *dev)
{
	int ret;
	char *envp[] = { "pencil_mislocated=1", NULL };
	static unsigned long last_uevent_time_jiffies = 0;
	unsigned long current_uevent_time_jiffies = jiffies;

	if (!dev) {
		mt_wls_log(PR_ERROR, "[%s] dev null\n", __func__);
		return;
	}

	if (current_uevent_time_jiffies - last_uevent_time_jiffies <=
		HZ * UEVENT_TRIGGER_COOLDOWN_TIME_MS / 1000) {
		last_uevent_time_jiffies = current_uevent_time_jiffies;
		return;
	}

	last_uevent_time_jiffies = current_uevent_time_jiffies;
	ret = kobject_uevent_env(&dev->kobj, KOBJ_CHANGE, envp);
	if (ret)
		mt_wls_log(PR_ERROR, "%s: kobject_uevent_fail, ret = %d",
			__func__, ret);
	else
		mt_wls_log(PR_INFO, "send pencil_mislocated=1 uevent\n");
}


static ssize_t ble_mac_addr_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct mt5806_dev *chip = NULL;

	chip = (struct mt5806_dev *)dev_get_drvdata(dev);
	if (!chip || !chip->init_work_succ) {
		mt_wls_log(PR_ERROR, "chip is NULL\n");
		return -EINVAL;
	}

	if (chip->pen_present) {
		if (chip->ble_mac_addr)
			return sprintf(buf, "0x%llx_(Time:%lldms)\n",
				chip->ble_mac_addr, chip->upto_ble_time);
		else
			return sprintf(buf, "%s", "wait_to_get_addr.\n");
	} else
		return sprintf(buf, "%s", "wait_to_connect.\n");

	return 0;
}
static DEVICE_ATTR_RO(ble_mac_addr);

static ssize_t present_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct mt5806_dev *chip = NULL;

	chip = (struct mt5806_dev *)dev_get_drvdata(dev);
	if (!chip || !chip->init_work_succ) {
		mt_wls_log(PR_ERROR, "chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->pen_present);
}
static DEVICE_ATTR_RO(present);

static ssize_t fw_version_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct mt5806_dev *chip = NULL;

	chip = (struct mt5806_dev *)dev_get_drvdata(dev);
	if (!chip || !chip->init_work_succ) {
		mt_wls_log(PR_ERROR, "chip is NULL\n");
		return -EINVAL;
	}

	if (chip->app_fw_ver == 0)
		(void)mt5806_get_app_fw_ver(chip);

	return sprintf(buf, "0x%x\n", chip->app_fw_ver);
}
static DEVICE_ATTR_RO(fw_version);

static ssize_t q_value_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct mt5806_dev *chip = NULL;
	static int cnt = 0;
	static int width = 0;

	chip = (struct mt5806_dev *)dev_get_drvdata(dev);
	if (!chip || !chip->init_work_succ) {
		mt_wls_log(PR_ERROR, "chip is NULL\n");
		return -EINVAL;
	}

	cnt = mt_wls_get_no_rx_cnt(chip);
	width = mt_wls_get_no_rx_width(chip);

	return sprintf(buf, "cnt:%d width:%d\n", cnt, width);
}
static DEVICE_ATTR_RO(q_value);

static ssize_t tx_voltage_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct mt5806_dev *chip = NULL;

	chip = (struct mt5806_dev *)dev_get_drvdata(dev);
	if (!chip || !chip->init_work_succ) {
		mt_wls_log(PR_ERROR, "chip is NULL\n");
		return -EINVAL;
	}
	if (chip->pen_present)
		chip->tx_voltage = mt5806_get_reverse_vin(chip);

	return sprintf(buf, "%d\n", chip->tx_voltage);
}
static DEVICE_ATTR_RO(tx_voltage);

static ssize_t tx_current_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct mt5806_dev *chip = NULL;

	chip = (struct mt5806_dev *)dev_get_drvdata(dev);
	if (!chip || !chip->init_work_succ) {
		mt_wls_log(PR_ERROR, "chip is NULL\n");
		return -EINVAL;
	}
	if (chip->pen_present)
		chip->tx_current = mt5806_get_reverse_iin(chip);

	return sprintf(buf, "%d\n", chip->tx_current);
}
static DEVICE_ATTR_RO(tx_current);

static ssize_t rx_soc_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct mt5806_dev *chip = NULL;

	chip = (struct mt5806_dev *)dev_get_drvdata(dev);
	if (!chip || !chip->init_work_succ) {
		mt_wls_log(PR_ERROR, "chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->rx_soc);
}

static ssize_t rx_soc_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct mt5806_dev *chip = NULL;
	int val = 0;

	chip = (struct mt5806_dev *)dev_get_drvdata(dev);
	if (!chip || !chip->init_work_succ) {
		mt_wls_log(PR_ERROR, "chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		mt_wls_log(PR_ERROR, "buf error\n");
		return -EINVAL;
	}
	WRITE_ONCE(chip->rx_soc, val);

	if ((chip->rx_soc <= chip->soc_threshould) && \
		(chip->pen_status == PEN_STATUS_NEAR) && \
		(false == chip->charge_allow) && \
		(PEN_REASON_CHARGE_FULL == chip->power_disable_reason)) {
		schedule_work(&chip->rechg_work);
	} else {
		mt_wls_log(PR_ERROR, "mt5806 value: %d, %d, %d.\n",
			chip->rx_soc, chip->pen_status, chip->charge_allow);
	}

	chip->mt_record_track.start_soc = (chip->mt_record_track.start_soc == MT_SOC_INVALID && chip->rx_soc > 0) ?
			chip->rx_soc : chip->mt_record_track.start_soc;
	return count;
}
static DEVICE_ATTR_RW(rx_soc);

static ssize_t wireless_debug_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct mt5806_dev *chip = NULL;

	chip = (struct mt5806_dev *)dev_get_drvdata(dev);
	if (!chip || !chip->init_work_succ) {
		mt_wls_log(PR_ERROR, "chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "[%d:%d]\n[%d]\n[%d]\n[%d]\n[%d]\n[%d %d %d %d]\n", \
			chip->rx_soc, chip->soc_threshould, \
			chip->charge_allow, \
			chip->power_enable_reason, \
			chip->power_expired_time, \
			chip->power_disable_reason, \
			chip->q_cali_status, chip->q_cali_result.q_cali_times,
			chip->q_cali_result.q_cali_cnt, chip->q_cali_result.q_cali_width);
}

static ssize_t wireless_debug_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int val = 0;
	struct mt5806_dev *chip = NULL;

	chip = (struct mt5806_dev *)dev_get_drvdata(dev);
	if (!chip || !chip->init_work_succ) {
		mt_wls_log(PR_ERROR, "chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		mt_wls_log(PR_DBG, "buf error\n");
		return -EINVAL;
	}
	if (val <= 100)
		/* we use 0-100 as soc threshould */
		chip->soc_threshould = val;
	else if (val < 2000)
		/* we use 100-2000 as power expired time */
		chip->power_expired_time = val - FULL_SOC;

	return count;
}

static DEVICE_ATTR_RW(wireless_debug);

static ssize_t ping_time_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct mt5806_dev *chip = NULL;

	chip = (struct mt5806_dev *)dev_get_drvdata(dev);
	if (!chip || !chip->init_work_succ) {
		mt_wls_log(PR_ERROR, "chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%lld\n", chip->ping_succ_time);
}
static DEVICE_ATTR_RO(ping_time);

static ssize_t tx_status_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct mt5806_dev *chip = NULL;

	chip = (struct mt5806_dev *)dev_get_drvdata(dev);
	if (!chip || !chip->init_work_succ) {
		mt_wls_log(PR_ERROR, "chip is NULL\n");
		return -EINVAL;
	}

	if (chip->pen_present)
		return sprintf(buf, "Connected\n");
	else
		return sprintf(buf, "Disconnect\n");
}
static DEVICE_ATTR_RO(tx_status);


static ssize_t wireless_ic_id_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct mt5806_dev *chip = NULL;
	chip = (struct mt5806_dev *)dev_get_drvdata(dev);
	if (!chip || !chip->init_work_succ) {
		mt_wls_log(PR_ERROR, "chip is NULL\n");
		return -EINVAL;
	}

	if (chip->chip_id == 0)
		(void)mt5806_get_rx_chip_id(chip);

	return sprintf(buf, "0x%x\n", chip->chip_id);
}
static DEVICE_ATTR_RO(wireless_ic_id);

static ssize_t q_cali_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct mt5806_dev *chip = dev_get_drvdata(dev);
	const char *status_str;
	const struct q_cali_result *result;

	if (!chip || !chip->init_work_succ) {
		mt_wls_log(PR_ERROR, "chip is NULL\n");
		return -EINVAL;
	}

	result = &chip->q_cali_result;

	switch (chip->q_cali_status) {
	case Q_CALI_IN_PROGRESS:
		return snprintf(buf, PAGE_SIZE, "calibrating\n");
	case Q_CALI_SUCCESS:
		status_str = "pass";
		break;
	case Q_CALI_FAIL:
		status_str = "fail";
		break;
	case Q_CALI_CALIBRATED:
		status_str = "calibrated";
		break;
	case Q_CALI_UNCALIBRATED:
		status_str = "Uncalibrated";
		break;
	default:
		return snprintf(buf, PAGE_SIZE, "unknown status\n");
	}

	return snprintf(buf, PAGE_SIZE, "%s:cali_times:%d cnt:%d width:%d cnt_var:%d width_var:%d\n",
		       status_str, result->q_cali_times, result->q_cali_cnt,
		       result->q_cali_width, result->q_cali_cnt_var, result->q_cali_width_var);
}

static ssize_t q_cali_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct mt5806_dev *chip = NULL;
	int val = 0;

	chip = (struct mt5806_dev *)dev_get_drvdata(dev);
	if (!chip || !chip->init_work_succ) {
		mt_wls_log(PR_ERROR, "chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		mt_wls_log(PR_ERROR, "buf error\n");
		return -EINVAL;
	}

	if ((val == 1) && (chip->q_cali_status != Q_CALI_IN_PROGRESS)) {
		memset(&chip->q_cali_result, 0, sizeof(chip->q_cali_result));
		cancel_delayed_work(&chip->equipment_work);
		schedule_delayed_work(&chip->equipment_work, 0);
	}

	return count;
}
static DEVICE_ATTR_RW(q_cali);

/*rw 0:read 1:write*/
static int addr = 0, length = 0, rw = 0, write_data = 0;

static ssize_t wireless_reg_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "unsupport operator\n");
}

static ssize_t wireless_reg_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct mt5806_dev *chip = NULL;

	chip = (struct mt5806_dev *)dev_get_drvdata(dev);
	if (!chip || !chip->init_work_succ) {
		mt_wls_log(PR_ERROR, "chip is NULL\n");
		return -EINVAL;
	}

	sscanf(buf, "%x %d %d %x", &addr, &length, &rw, &write_data);
	mt_wls_log(PR_ERROR, "[%s]:add=0x%x,len=%d,rw=%d,write_data=0x%x\n",
		__func__, addr, length, rw, write_data);
	return count;
}
static DEVICE_ATTR_RW(wireless_reg);

static ssize_t gpio_debug_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct mt5806_dev *chip = NULL;
	int gp0_val = 0;
	int gp1_val = 0;
	int gp2_val = 0;
	int gp3_val = 0;
	int gp4_val = 0;

	chip = (struct mt5806_dev *)dev_get_drvdata(dev);
	if (!chip || !chip->init_work_succ) {
		mt_wls_log(PR_ERROR, "chip is NULL\n");
		return -EINVAL;
	}

	gp0_val = gpio_get_value(chip->wls_sleep_gpio);
	gp1_val = gpio_get_value(chip->scan_mode_gpio);
	gp2_val = gpio_get_value(chip->wls_sw_en_gpio);
	gp3_val = gpio_get_value(chip->wls_off_state_gpio);
	gp4_val = gpio_get_value(chip->boost_on_gpio);
	return sprintf(buf, "gp0=%d, gp1=%d, gp2=%d, gp3=%d, gp4=%d\n",
		gp0_val, gp1_val, gp2_val, gp3_val, gp4_val);
}

static ssize_t gpio_debug_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int gpio = 0;
	int value = 0;
	struct mt5806_dev *chip = NULL;

	chip = (struct mt5806_dev *)dev_get_drvdata(dev);
	if (!chip || !chip->init_work_succ) {
		mt_wls_log(PR_ERROR, "chip is NULL\n");
		return -EINVAL;
	}

	sscanf(buf, "%d %d", &gpio, &value);
	mt_set_gpio_value(chip, gpio, value);

	return count;
}
static DEVICE_ATTR_RW(gpio_debug);

static ssize_t rx_mislocate_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct mt5806_dev *chip = NULL;

	chip = (struct mt5806_dev *)dev_get_drvdata(dev);
	if (!chip || !chip->init_work_succ) {
		mt_wls_log(PR_ERROR, "chip is NULL\n");
		return -EINVAL;
	}

	mt5806_send_mislocated_uevent(dev);

	return count;
}
static DEVICE_ATTR_WO(rx_mislocate);

static int tx_charge_disable_update_mask(struct mt5806_dev *chip, int disable, int callname)
{
	uint32_t callname_bit = 0;

	if (callname <= TX_CHARGE_DISABLE_CALLNAME_UNDEFINED || callname >= TX_CHARGE_DISABLE_CALLNAME_MAX) {
		mt_wls_log(PR_ERROR, "[%s] callname=%d is not a valid enum value, ignoring\n",
			__func__, callname);
		return -EINVAL;
	}

	/* Check if callname is a valid enum value (excluding MAX) */
	switch (callname) {
	case TX_CHARGE_DISABLE_CALLNAME_CAMERA:
		if (!chip->support_camera_tx_disable) {
			mt_wls_log(PR_ERROR, "[%s] CAMERA callname not supported, callname=%d\n",
				__func__, callname);
			return -EINVAL;
		}
		break;
	default:
		mt_wls_log(PR_ERROR, "[%s] callname=%d is not a valid enum value, ignoring\n",
			__func__, callname);
		return -EINVAL;
	}

	callname_bit = (1 << (callname - 1));

	mutex_lock(&chip->tx_disable_callname_lock);
	if (disable == 1) {
		chip->tx_disable_callname |= callname_bit;
		mt_wls_log(PR_INFO, "[%s] set callname bit %d, mask=0x%x\n",
			__func__, callname, chip->tx_disable_callname);
	} else {
		chip->tx_disable_callname &= ~callname_bit;
		mt_wls_log(PR_INFO, "[%s] clear callname bit %d, mask=0x%x\n",
			__func__, callname, chip->tx_disable_callname);
	}
	mutex_unlock(&chip->tx_disable_callname_lock);
	return 0;
}

static void tx_charge_disable_update_charge(struct mt5806_dev *chip)
{
	uint32_t tx_disable_callname;

	mutex_lock(&chip->tx_disable_callname_lock);
	tx_disable_callname = chip->tx_disable_callname;
	mutex_unlock(&chip->tx_disable_callname_lock);

	if (chip->pen_present == 1 && tx_disable_callname != 0) {
		msleep(100);
		mt_set_chg_enable(chip, false);
		mt_wls_log(PR_INFO, "[%s] disable charge, pen present, mask=0x%x\n",
			__func__, tx_disable_callname);
	} else if (chip->pen_present == 1 && chip->power_disable_reason == PEN_REASON_UNKNOWN &&
		tx_disable_callname == 0 && chip->charge_allow == false) {
		mt_wls_log(PR_INFO, "[%s] enable charge, pen present, mask=0x%x\n",
			__func__, tx_disable_callname);
		mt_set_chg_enable(chip, true);
	}
}

static ssize_t tx_charge_disable_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int disable = -1;
	int callname = 0;
	struct mt5806_dev *chip = NULL;

	chip = (struct mt5806_dev *)dev_get_drvdata(dev);
	if (!chip || !chip->init_work_succ) {
		mt_wls_log(PR_ERROR, "chip is NULL\n");
		return -EINVAL;
	}

	if (sscanf(buf, "disable=%dcallname=%d", &disable, &callname) != 2) {
		mt_wls_log(PR_ERROR, "Invalid format, expected: disable=<num>,callname=<num>\n");
		return -EINVAL;
	}

	mt_wls_log(PR_INFO, "[%s] disable=%d, callname=%d, pen_present=%d\n",
		__func__, disable, callname, chip->pen_present);

	if (tx_charge_disable_update_mask(chip, disable, callname) < 0)
		return -EINVAL;

	tx_charge_disable_update_charge(chip);

	return count;
}
static ssize_t tx_charge_disable_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mt5806_dev *chip = NULL;
	uint32_t tx_disable_callname;

	chip = (struct mt5806_dev *)dev_get_drvdata(dev);
	if (!chip || !chip->init_work_succ) {
		mt_wls_log(PR_ERROR, "chip is NULL\n");
		return -EINVAL;
	}

	mutex_lock(&chip->tx_disable_callname_lock);
	tx_disable_callname = chip->tx_disable_callname;
	mutex_unlock(&chip->tx_disable_callname_lock);

	return snprintf(buf, PAGE_SIZE, "tx_disable_callname=0x%x, charge_allow=%d, pen_present=%d\n",
		tx_disable_callname, chip->charge_allow, chip->pen_present);
}
static DEVICE_ATTR_RW(tx_charge_disable);

static struct device_attribute *mt5806_pencil_attributes[] = {
	&dev_attr_ble_mac_addr,
	&dev_attr_present,
	&dev_attr_fw_version,
	&dev_attr_q_value,
	&dev_attr_tx_current,
	&dev_attr_tx_voltage,
	&dev_attr_rx_soc,
	&dev_attr_wireless_debug,
	&dev_attr_wireless_ic_id,
	&dev_attr_ping_time,
	&dev_attr_tx_status,
	&dev_attr_q_cali,
	&dev_attr_wireless_reg,
	&dev_attr_gpio_debug,
	&dev_attr_rx_mislocate,
	&dev_attr_tx_charge_disable,
	NULL
};

static int mt5806_init_wireless_device(struct mt5806_dev *chip)
{
	int err = 0;
	int status = 0;
	dev_t devt;
	struct class *wireless_class = NULL;
	struct device_attribute **attrs, *attr;

	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s]: chip is null!\n", __func__);
		return -EINVAL;
	}

#if (KERNEL_VERSION(6, 3, 0) <= LINUX_VERSION_CODE)
	wireless_class = class_create("oplus_wireless");
#else
	wireless_class = class_create(THIS_MODULE, "oplus_wireless");
#endif

	status = alloc_chrdev_region(&devt, 0, 1, "tx_wireless");
	chip->wireless_dev = device_create(wireless_class, NULL,
		devt, NULL, "%s", "pencil");
	chip->wireless_dev->devt = devt;
	dev_set_drvdata(chip->wireless_dev, chip);

	attrs = mt5806_pencil_attributes;
	while ((attr = *attrs++)) {
		err = device_create_file(chip->wireless_dev, attr);
		if (err) {
			mt_wls_log(PR_ERROR, "device_create_file fail!\n");
			return err;
		}
	}

	return 0;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
static int mt5806_pm_resume(struct device *dev)
{
	struct mt5806_dev *chip = dev_get_drvdata(dev);

	if (chip && chip->init_work_succ) {
		chip->i2c_ready = true;
		mt_wls_log(PR_INFO, "[%s]mt5806_pm_resume.\n", __func__);
		wake_up_interruptible(&i2c_waiter);
		power_expired_do_check(chip);
	}

	return 0;
}

static int mt5806_pm_suspend(struct device *dev)
{
	struct mt5806_dev *chip = dev_get_drvdata(dev);
	if (chip && chip->init_work_succ)
		chip->i2c_ready = false;

	return 0;
}

static const struct dev_pm_ops mt5806_pm_ops = {
	.resume = mt5806_pm_resume,
	.suspend = mt5806_pm_suspend,
};
#else
static int mt5806_resume(struct i2c_client *client)
{
	return 0;
}

static int mt5806_suspend(struct i2c_client *client, pm_message_t mesg)
{
	return 0;
}
#endif

static void mt_set_gpio_sleep_value(struct mt5806_dev *chip, int value)
{
	if (IS_ERR_OR_NULL(chip->mt_pinctrl) ||
		IS_ERR_OR_NULL(chip->wls_sleep_gpio_active) ||
		IS_ERR_OR_NULL(chip->wls_sleep_gpio_sleep) ||
		IS_ERR_OR_NULL(chip->wls_sleep_gpio_default)) {
		mt_wls_log(PR_ERROR, "wls_sleep_gpio: gp_num = %d pinctrl null\n", GP_0);
		return;
	}

	if (value) {
		gpio_direction_output(chip->wls_sleep_gpio, 1);
		pinctrl_select_state(chip->mt_pinctrl, chip->wls_sleep_gpio_active);
	} else {
		gpio_direction_output(chip->wls_sleep_gpio, 0);
		pinctrl_select_state(chip->mt_pinctrl, chip->wls_sleep_gpio_sleep);
	}
	mt_wls_log(PR_INFO, "wls_sleep_gpio: gp_num:%d, set value:%d,"
	           "gpio_val:%d\n", GP_0, value, gpio_get_value(chip->wls_sleep_gpio));
}

static void mt_set_gpio_scan_mode_value(struct mt5806_dev *chip, int value)
{
	if (IS_ERR_OR_NULL(chip->mt_pinctrl) ||
		IS_ERR_OR_NULL(chip->scan_mode_gpio_active) ||
		IS_ERR_OR_NULL(chip->scan_mode_gpio_sleep) ||
		IS_ERR_OR_NULL(chip->scan_mode_gpio_default)) {
		mt_wls_log(PR_ERROR, "scan_mode_gpio: gp_num = %d pinctrl null\n", GP_1);
		return;
	}

	if (value) {
		gpio_direction_output(chip->scan_mode_gpio, 1);
		pinctrl_select_state(chip->mt_pinctrl, chip->scan_mode_gpio_active);
	} else {
		gpio_direction_output(chip->scan_mode_gpio, 0);
		pinctrl_select_state(chip->mt_pinctrl, chip->scan_mode_gpio_sleep);
	}
	mt_wls_log(PR_INFO, "scan_mode_gpio: gp_num:%d, set value:%d,"
	           "gpio_val:%d\n", GP_1, value, gpio_get_value(chip->scan_mode_gpio));
}

static void mt_set_gpio_sw_en_value(struct mt5806_dev *chip, int value)
{
	if (IS_ERR_OR_NULL(chip->mt_pinctrl) ||
	    IS_ERR_OR_NULL(chip->wls_sw_en_active) ||
	    IS_ERR_OR_NULL(chip->wls_sw_en_sleep) ||
	    IS_ERR_OR_NULL(chip->wls_sw_en_default)) {
		mt_wls_log(PR_ERROR, "wls_sw_en_gpio: gp_num = %d pinctrl null\n", GP_2);
		return;
	}

	if (value) {
		gpio_direction_output(chip->wls_sw_en_gpio, 1);
		pinctrl_select_state(chip->mt_pinctrl, chip->wls_sw_en_active);
	} else {
		gpio_direction_output(chip->wls_sw_en_gpio, 0);
		pinctrl_select_state(chip->mt_pinctrl, chip->wls_sw_en_sleep);
	}
	mt_wls_log(PR_INFO, "wls_sw_en_gpio: gp_num:%d, set value:%d,"
	           "gpio_val:%d\n", GP_2, value, gpio_get_value(chip->wls_sw_en_gpio));
}

static void mt_set_gpio_off_state_value(struct mt5806_dev *chip, int value)
{
	if (IS_ERR_OR_NULL(chip->mt_pinctrl) ||
	    IS_ERR_OR_NULL(chip->wls_off_state_gpio_active) ||
	    IS_ERR_OR_NULL(chip->wls_off_state_gpio_sleep) ||
	    IS_ERR_OR_NULL(chip->wls_off_state_gpio_default)) {
		mt_wls_log(PR_ERROR, "wls_off_state_gpio: gp_num = %d pinctrl null\n", GP_3);
		return;
	}

	if (value) {
		gpio_direction_output(chip->wls_off_state_gpio, 1);
		pinctrl_select_state(chip->mt_pinctrl, chip->wls_off_state_gpio_active);
	} else {
		gpio_direction_output(chip->wls_off_state_gpio, 0);
		pinctrl_select_state(chip->mt_pinctrl, chip->wls_off_state_gpio_sleep);
	}
	mt_wls_log(PR_INFO, "wls_off_state_gpio: gp_num:%d, set value:%d,"
	           "gpio_val:%d\n", GP_3, value, gpio_get_value(chip->wls_off_state_gpio));
}

static void mt_set_gpio_value(struct mt5806_dev *chip, int gp_num, int value)
{
	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip null \n", __func__);
		return;
	}

	switch (gp_num) {
	case GP_0:
		mt_set_gpio_sleep_value(chip, value);
		break;
	case GP_1:
		mt_set_gpio_scan_mode_value(chip, value);
		break;
	case GP_2:
		mt_set_gpio_sw_en_value(chip, value);
		break;
	case GP_3:
		mt_set_gpio_off_state_value(chip, value);
		break;
	default:
		break;
	}

	return;
}

static int mt5806_gpio_init(struct mt5806_dev *chip)
{
	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip is NULL\n", __func__);
		return -EINVAL;
	}

	mt_set_gpio_value(chip, GP_2, 1);
	mt_set_gpio_value(chip, GP_0, 1);
	mt_set_gpio_value(chip, GP_1, 1);
	mt_set_gpio_value(chip, GP_3, 1);

	mt_wls_log(PR_ERROR, "mt5806_gpio_init successful\n");

	return 0;
}

static void mt5806_free_gpio(struct mt5806_dev *chip)
{
	if (!chip)
		return;

	if (gpio_is_valid(chip->wls_irq_gpio))
		gpio_free(chip->wls_irq_gpio);

	if (gpio_is_valid(chip->wls_off_state_gpio))
		gpio_free(chip->wls_off_state_gpio);

	if (gpio_is_valid(chip->wls_sleep_gpio))
		gpio_free(chip->wls_sleep_gpio);

	if (gpio_is_valid(chip->scan_mode_gpio))
		gpio_free(chip->scan_mode_gpio);

	if (gpio_is_valid(chip->wls_sw_en_gpio))
		gpio_free(chip->wls_sw_en_gpio);

	if (gpio_is_valid(chip->boost_on_gpio))
		gpio_free(chip->boost_on_gpio);
}

static int mt_get_cali_status(struct mt5806_dev *chip)
{
	int ret;

	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip null \n", __func__);
		return MT_WLS_FAIL;
	}

	ret = mt5806_fw_q_mtp_get(chip);
	if (ret) {
		mt_wls_log(PR_ERROR, "mt5806_fw_q_cali_get failed\n");
		return ret;
	}

	chip->q_cali_result.q_cali_cnt = chip->q_mtp_data.q_info.mt5806_q_mtp;
	chip->q_cali_result.q_cali_width = chip->q_mtp_data.q_info.mt5806_f_mtp;
	chip->q_cali_result.q_cali_cnt_var = chip->q_mtp_data.q_info.mt5806_q_mtp_varnce;
	chip->q_cali_result.q_cali_width_var = chip->q_mtp_data.q_info.mt5806_f_mtp_varnce;

	if (chip->q_cali_result.q_cali_cnt < 0 ||
	    chip->q_cali_result.q_cali_width < 0 ||
	    chip->q_cali_result.q_cali_cnt_var < 0 ||
	    chip->q_cali_result.q_cali_width_var < 0)
		return MT_WLS_FAIL;

	if (chip->q_cali_result.q_cali_cnt > 0 && chip->q_cali_result.q_cali_width > 0)
		chip->q_cali_status = Q_CALI_CALIBRATED;
	else
		chip->q_cali_status = Q_CALI_UNCALIBRATED;

	return MT_WLS_SUCCESS;
}

static void mt_set_led_on(struct mt5806_dev *chip, bool led_on)
{
	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip null \n", __func__);
		return;
	}
	if (chip->led_on == led_on)
		return;

	mt_wls_log(PR_DBG, "[%s] set led_on = %d\n", __func__, led_on);
	if (led_on) {
		mt_set_gpio_value(chip, GP_1, 1);
		chip->led_on = led_on;
	} else {
		mt_set_gpio_value(chip, GP_1, 0);
		chip->led_on = led_on;
	}
}


#if IS_ENABLED(CONFIG_DRM_PANEL_NOTIFY) || IS_ENABLED(CONFIG_OPLUS_CHG_DRM_PANEL_NOTIFY)
static void mt_panel_notifier_callback(enum panel_event_notifier_tag tag,
			struct panel_event_notification *notification, void *client_data)
{
	struct mt5806_dev *chip = client_data;

	if (!notification) {
		mt_wls_log(PR_ERROR, "Invalid notification\n");
		return;
	}

	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip null \n", __func__);
		return;
	}

	mt_wls_log(PR_DBG, "Notification type:%d, early_trigger:%d",
		notification->notif_type, notification->notif_data.early_trigger);

	switch (notification->notif_type) {
	case DRM_PANEL_EVENT_UNBLANK:
		mt_wls_log(PR_ERROR, "received unblank event: %d\n",
			notification->notif_data.early_trigger);
		if (notification->notif_data.early_trigger)
			mt_set_led_on(chip, true);
		break;
	case DRM_PANEL_EVENT_BLANK:
		mt_wls_log(PR_ERROR, "received blank event: %d\n",
			notification->notif_data.early_trigger);
		if (notification->notif_data.early_trigger)
				mt_set_led_on(chip, false);
		break;
	case DRM_PANEL_EVENT_BLANK_LP:
		break;
	case DRM_PANEL_EVENT_FPS_CHANGE:
		break;
	default:
		break;
	}
}

#else /* CONFIG_DRM_PANEL_NOTIFY */

#if IS_ENABLED(CONFIG_OPLUS_MTK_DRM_GKI_NOTIFY_CHG)
static int mt_mtk_drm_notifier_callback(struct notifier_block *nb,
			unsigned long event, void *data)
{
	int *blank = (int *)data;
	struct mt5806_dev *chip =
		container_of(nb, struct mt5806_dev, mt_fb_notify);

	if (!blank) {
		mt_wls_log(PR_ERROR, "get disp statu err, blank is NULL!");
		return 0;
	}

	if (!chip)
		return 0;

	switch (event) {
	case MTK_DISP_EARLY_EVENT_BLANK:
		if (*blank == MTK_DISP_BLANK_UNBLANK)
			mt_set_led_on(chip, true);
		else if (*blank == MTK_DISP_BLANK_POWERDOWN)
			mt_set_led_on(chip, false);
		else
			mt_wls_log(PR_ERROR, "receives wrong data EARLY_BLANK:%d\n", *blank);
		break;
	case MTK_DISP_EVENT_BLANK:
		if (*blank == MTK_DISP_BLANK_UNBLANK)
			mt_set_led_on(chip, true);
		else if (*blank == MTK_DISP_BLANK_POWERDOWN)
			mt_set_led_on(chip, false);
		else
			mt_wls_log(PR_ERROR, "receives wrong data BLANK:%d\n", *blank);
		break;
	default:
		break;
	}

	return 0;
}

#endif /* CONFIG_OPLUS_MTK_DRM_GKI_NOTIFY_CHG */
#endif /* CONFIG_DRM_PANEL_NOTIFY */

static int mt_register_lcd_notify(struct mt5806_dev *chip)
{
	int rc = 0;

#if IS_ENABLED(CONFIG_DRM_PANEL_NOTIFY) || IS_ENABLED(CONFIG_OPLUS_CHG_DRM_PANEL_NOTIFY)
	int i;
	int count;
	struct device_node *node = NULL;
	struct drm_panel *panel = NULL;
	struct device_node *np = NULL;
	void *cookie = NULL;

	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip null \n", __func__);
		return -EINVAL;
	}
	np = chip->dev->of_node;

#if IS_ENABLED(CONFIG_DRM_PANEL_NOTIFY)
	count = of_count_phandle_with_args(np, "oplus,display_panel", NULL);
	if (count <= 0) {
		mt_wls_log(PR_ERROR, "oplus,display_panel not found\n");
		return 0;
	}

	for (i = 0; i < count; i++) {
		node = of_parse_phandle(np, "oplus,display_panel", i);
		panel = of_drm_find_panel(node);
		of_node_put(node);
		if (!IS_ERR(panel)) {
			chip->active_panel = panel;
			rc = 0;
			mt_wls_log(PR_ERROR, "find active panel\n");
			break;
		} else {
			rc = PTR_ERR(panel);
		}
	}
#else
	np = of_find_node_by_name(NULL, "oplus,dsi-display-dev");
	if (!np) {
		mt_wls_log(PR_ERROR, "device tree info. missing\n");
		return 0;
	}

	count = of_count_phandle_with_args(np, "oplus,dsi-panel-primary", NULL);
	if (count <= 0) {
		mt_wls_log(PR_ERROR, "primary panel no found\n");
		return 0;
	}

	for (i = 0; i < count; i++) {
		node = of_parse_phandle(np, "oplus,dsi-panel-primary", i);
		panel = of_drm_find_panel(node);
		of_node_put(node);
		if (!IS_ERR(panel)) {
			chip->active_panel = panel;
			rc = 0;
			mt_wls_log(PR_ERROR, "find active_panel rc\n");
			break;
		} else {
			rc = PTR_ERR(panel);
		}
	}
#endif/* CONFIG_DRM_PANEL_NOTIFY */

	if (chip->active_panel) {
		cookie = panel_event_notifier_register(
				PANEL_EVENT_NOTIFICATION_PRIMARY,
				PANEL_EVENT_NOTIFIER_CLIENT_WLS_PEN_CHG,
				chip->active_panel, &mt_panel_notifier_callback,
				chip);
		if (!cookie) {
			mt_wls_log(PR_ERROR, "Unable to register chg_panel_notifier\n");
			return -EINVAL;
		} else {
			mt_wls_log(PR_ERROR, "success register chg_panel_notifier\n");
			chip->notifier_cookie = cookie;
		}
	} else {
		mt_wls_log(PR_ERROR, "can't find active panel, rc=%d\n", rc);
		if (rc == -EPROBE_DEFER)
			return rc;
		else
			return -ENODEV;
	}
#else /* CONFIG_DRM_PANEL_NOTIFY */

#if IS_ENABLED(CONFIG_OPLUS_MTK_DRM_GKI_NOTIFY_CHG)
	chip->mt_fb_notify.notifier_call = mt_mtk_drm_notifier_callback;
	if (mtk_disp_notifier_register("Oplus_mt5806", &chip->mt_fb_notify)) {
		mt_wls_log(PR_ERROR, "Failed to register disp notifier client!!\n");
	}
#endif  /* CONFIG_OPLUS_MTK_DRM_GKI_NOTIFY_CHG */
#endif  /* CONFIG_DRM_PANEL_NOTIFY */

	return rc;
}

#define LCD_REG_RETRY_COUNT_MAX  20
#define LCD_REG_RETRY_DELAY_MS   100
static void lcd_notify_reg_work_func(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct mt5806_dev *chip =
		container_of(dwork, struct mt5806_dev, lcd_notify_reg_work);
	static int retry_count = 0;
	int rc;

	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip null\n", __func__);
		return;
	}

	if (retry_count >= LCD_REG_RETRY_COUNT_MAX)
		return;

	rc = mt_register_lcd_notify(chip);
	if (rc < 0) {
		if (rc != -EPROBE_DEFER) {
			mt_wls_log(PR_ERROR, "register lcd notify error, rc=%d\n", rc);
			return;
		}
		retry_count++;
		mt_wls_log(PR_ERROR, "lcd panel not ready, count=%d\n", retry_count);
		schedule_delayed_work(&chip->lcd_notify_reg_work,
					msecs_to_jiffies(LCD_REG_RETRY_DELAY_MS));
		return;
	}
}

static int mt5806_wls_chipid_check(struct mt5806_dev *chip)
{
	int retry = 0;
	int rc = 0;

	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip null \n", __func__);
		return -EINVAL;
	}

	for (retry = 0; retry < MAX_RETRY; retry++) {
		rc = mt5806_get_rx_chip_id(chip);
		if (chip->chip_id == MT5806_CHIP_ID)
			return MT_WLS_SUCCESS;
		msleep(100); /* wait for chip work*/
	}
	mt_wls_log(PR_ERROR, "[%s] chip id = 0x%x\n", __func__, chip->chip_id);
	return MT_WLS_FAIL;
}

static int mt_handle_boost_setup(struct mt5806_dev *chip)
{
	int boost_work_retry = 0;

	if (chip->is_supply_by_hboost) {
		schedule_work(&chip->set_boost_work);
		while (1) {
			msleep(BOOST_SET_DELAY_500MS);
			if (chip->hboost_status == HBOOST_SET_SUCCESS)
				break;
			else if (chip->hboost_status == HBOOST_SET_FAIL) {
				if (boost_work_retry < BOOST_WORK_MAX_RETRY) {
					if (chip->hboost_status != HBOOST_IS_SETTING_BOOST) {
						schedule_work(&chip->set_boost_work);
						boost_work_retry++;
					}
					continue;
				}
				mt_wls_log(PR_ERROR, "[%s]set boost fail\n", __func__);
				return MT_WLS_FAIL;
			}
		}
	}

	mt_wls_log(PR_INFO, "[%s]set boost success\n", __func__);
	return MT_WLS_SUCCESS;
}

static int mt_handle_fw_update(struct mt5806_dev *chip)
{
	int fw_update_retry = 0;
	int ret = MT_WLS_SUCCESS;
	unsigned long start_time = jiffies;
	const unsigned long max_timeout = msecs_to_jiffies(90000); /* 90 seconds */

	do {
		/* Check timeout */
		if (time_after(jiffies, start_time + max_timeout)) {
			mt_wls_log(PR_ERROR, "[%s]fw update timeout after 90 seconds, force exit\n", __func__);
			ret = MT_WLS_FAIL;
			break;
		}

		chip->wls_fw_update_result = WLS_FW_UPDATE_RESULT_ON_GOING;
		mt5806_program_fw(chip);
		if (chip->wls_fw_update_result == WLS_FW_UPDATE_RESULT_SUCCESS) {
			ret = MT_WLS_SUCCESS;
			break;
		} else if (chip->wls_fw_update_result == WLS_FW_UPDATE_RESULT_ERROR) {
			if (fw_update_retry < MAX_RETRY) {
				fw_update_retry++;
				mt_wls_log(PR_INFO, "[%s] retry fw update, retry=%d\n",
					   __func__, fw_update_retry);
				msleep(100);
				continue;
			}
			mt_wls_log(PR_ERROR, "[%s]fw update fail after %d retries\n",
				   __func__, fw_update_retry);
			ret = MT_WLS_FAIL;
			break;
		}
	} while (fw_update_retry < MAX_RETRY);

	return ret;
}

static int mt_init_chip_hardware(struct mt5806_dev *chip)
{
	int ret;

	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip is NULL\n", __func__);
		return -EINVAL;
	}

	mt_wls_log(PR_INFO, "[%s] mt_init_chip_hardware start\n", __func__);
	mt5806_gpio_init(chip);
	mt5806_reset_power(chip);
	msleep(TX_WAKEUP_WAIT_MS);

	ret = mt5806_wls_chipid_check(chip);
	if (ret) {
		mt_wls_log(PR_ERROR, "mt5806 read chip_id failed.\n");
		return ret;
	}

	ret = mt5806_get_app_fw_ver(chip);
	mt_wls_log(PR_INFO, "[%s] chip id = 0x%x, app_fw_ver = 0x%x\n",
		__func__, chip->chip_id, chip->app_fw_ver);

	return MT_WLS_SUCCESS;
}

static int mt_setup_irq_handler(struct mt5806_dev *chip)
{
	int ret;

	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip is NULL\n", __func__);
		return -EINVAL;
	}

	if (!chip->wls_irq)
		return 0;

	ret = devm_request_threaded_irq(chip->dev, chip->wls_irq, NULL,
		mt5806_wls_irq_handler, IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
		"wls_irq", chip);
	if (ret) {
		mt_wls_log(PR_ERROR, "request wls_irq failed.\n");
		return ret;
	}

	enable_irq_wake(chip->wls_irq);
	mt_wls_log(PR_INFO, "wls_irq init.\n");
	return MT_WLS_SUCCESS;
}

static void init_work_cleanup_on_failure(struct mt5806_dev *chip)
{
	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip is NULL\n", __func__);
		return;
	}

	mt_set_gpio_value(chip, GP_2, 0);
	mt_wls_log(PR_ERROR, "[%s]init error\n", __func__);

	mt5806_free_gpio(chip);
	mt5806_lock_work_destroy(chip);
}

static void init_work_func(struct work_struct *work)
{
	int ret;
	struct mt5806_dev *chip = container_of(work, struct mt5806_dev, init_work);

	if (!chip) {
		mt_wls_log(PR_ERROR, "[%s] chip null\n", __func__);
		return;
	}

	if (!chip->mt_wls_wake_lock) {
		mt_wls_log(PR_ERROR, "[%s] mt_wls_wake_lock is NULL\n", __func__);
		return;
	}

	__pm_stay_awake(chip->mt_wls_wake_lock);

	ret = mt_handle_boost_setup(chip);
	if (ret != MT_WLS_SUCCESS)
		goto init_fail;

	mt_set_gpio_value(chip, GP_2, 1);
	msleep(10);

	ret = mt_handle_fw_update(chip);
	if (ret != MT_WLS_SUCCESS)
		mt_wls_log(PR_ERROR, "[%s] mt_handle_fw_update fail, ret=%d\n", __func__, ret);

	ret = mt_init_chip_hardware(chip);
	if (ret != MT_WLS_SUCCESS)
		goto init_fail;

	mt_set_led_on(chip, true);
	mt_set_chg_enable(chip, false);
	mt_get_cali_status(chip);

	ret = mt_setup_irq_handler(chip);
	if (ret != MT_WLS_SUCCESS)
		goto init_fail;

	mt_set_gpio_value(chip, GP_0, 0);
	mt_set_chg_enable(chip, true);
	schedule_delayed_work(&chip->lcd_notify_reg_work,
		round_jiffies_relative(msecs_to_jiffies(LCD_REG_DELAY_SEC * MT_MSEC_PER_SEC)));
	__pm_relax(chip->mt_wls_wake_lock);
	chip->init_work_succ = true;
	return;

init_fail:
	__pm_relax(chip->mt_wls_wake_lock);
	cancel_work_sync(&chip->set_boost_work);
	cancel_work_sync(&chip->rechg_work);
	cancel_work_sync(&chip->error_attach_check_work);
	cancel_delayed_work_sync(&chip->max_chg_time_check_work);
	cancel_delayed_work_sync(&chip->lcd_notify_reg_work);
	cancel_delayed_work_sync(&chip->equipment_work);
	chip->init_work_succ = false;
	init_work_cleanup_on_failure(chip);
}


#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0))
static int mt5806_probe(struct i2c_client *client)
#else
static int mt5806_probe(struct i2c_client *client, const struct i2c_device_id *id)
#endif
{
	struct mt5806_dev *chip;
	int rc = 0;

	mt_wls_log(PR_INFO, "start\n");
	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip) {
		pr_err("mt5806_probe Couldn't allocate memory\n");
		return -ENOMEM;
	}

	chip->regmap = devm_regmap_init_i2c(client, &mt5806_regmap_config);
	if (IS_ERR(chip->regmap)) {
		pr_err("Failed to allocate regmap!\n");
		devm_kfree(&client->dev, chip);
		return PTR_ERR(chip->regmap);
	}

	chip->regmap32 = devm_regmap_init_i2c(client, &mt5806_regmap32_config);
	if (IS_ERR(chip->regmap32)) {
		pr_err("Failed to allocate regmap32!\n");
		devm_kfree(&client->dev, chip);
		return PTR_ERR(chip->regmap32);
	}

	chip->client = client;
	chip->dev = &client->dev;
	chip->name = "mt5806";
	i2c_set_clientdata(client, chip);
	dev_set_drvdata(&(client->dev), chip);

	chip->wireless_dev = NULL;
	chip->init_work_succ = false;
	chip->reverse_vin_ma = 0;
	chip->wls_fw_is_burning = false;
	chip->wls_fw_update_result = WLS_FW_UPDATE_RESULT_NONE;
	chip->equipment_mode = 0;
	chip->cali_done = false;
	chip->tx_disable_callname = 0;

	chip->bus.read = mt5806_read;
	chip->bus.write = mt5806_write;
	chip->bus.write_mask = mt5806_masked_write;
	chip->bus.read_buf = mt5806_read_buffer;
	chip->bus.write_buf = mt5806_write_buffer;
	chip->i2c_ready = true;

	memset(chip->q_mtp_data.q_mtp_data, 0, sizeof(chip->q_mtp_data.q_mtp_data));
	memset(chip->q_cali_data.q_mtp_data, 0, sizeof(chip->q_cali_data.q_mtp_data));

	rc = mt5806_parse_dt(chip);
	if (rc) {
		mt_wls_log(PR_ERROR, "Couldn't parse DT nodes rc=%d\n", rc);
		goto parse_dt_fail;
	}

	mt5806_lock_work_init(chip);

	rc = mt5806_wls_gpio_init(chip);
	if (rc < 0) {
		mt_wls_log(PR_ERROR, "[%s] mt5806_wls_gpio_init Fail rc = %d\n", __func__, rc);
		goto gpio_init_fail;
	}

	INIT_DELAYED_WORK(&chip->max_chg_time_check_work, mt_max_chg_time_check_work_func);
	INIT_DELAYED_WORK(&chip->ping_timeout_work, mt_ping_timeout_work_func);
	INIT_DELAYED_WORK(&chip->lcd_notify_reg_work, lcd_notify_reg_work_func);
	INIT_WORK(&chip->rechg_work, rechg_work_func);
	INIT_WORK(&chip->error_attach_check_work, error_attach_check_work_func);
	INIT_WORK(&chip->init_work, init_work_func);
	INIT_WORK(&chip->set_boost_work, set_boost_work_func);
	INIT_WORK(&chip->track_record_upload_work, mt_track_record_upload_work);
	INIT_DELAYED_WORK(&chip->equipment_work, mt5806_equipment_work);

	rc = mt5806_init_wireless_device(chip);
	if (rc < 0)
		mt_wls_log(PR_ERROR, "Create wireless charge device error.");

	if (chip->is_supply_by_hboost)
		wireless_pen_glink_init();

	schedule_work(&chip->init_work);

	mt_wls_log(PR_INFO, "[%s]successful!\n", __func__);
	return 0;

gpio_init_fail:
	mt5806_lock_work_destroy(chip);
parse_dt_fail:
	i2c_set_clientdata(client, NULL);
	dev_set_drvdata(&(client->dev), NULL);
	devm_kfree(&client->dev, chip);

	mt_wls_log(PR_ERROR, "[%s] error: free resource.\n", __func__);
	return rc;
}
static void mt5806_shutdown(struct i2c_client *client)
{
	struct mt5806_dev *chip = i2c_get_clientdata(client);

	if (chip) {
		cancel_delayed_work_sync(&chip->max_chg_time_check_work);
		cancel_delayed_work_sync(&chip->lcd_notify_reg_work);
		cancel_work_sync(&chip->rechg_work);
		cancel_work_sync(&chip->error_attach_check_work);
		cancel_work_sync(&chip->init_work);
		cancel_work_sync(&chip->set_boost_work);
		cancel_work_sync(&chip->track_record_upload_work);
		cancel_delayed_work_sync(&chip->equipment_work);

		if (chip->wls_irq)
				devm_free_irq(chip->dev, chip->wls_irq, chip);

		if (chip->firm_data_bin) {
				release_firmware(chip->firm_data_bin);
				chip->firm_data_bin = NULL;
		}
		if (chip->firmware_data) {
				kfree(chip->firmware_data);
				chip->firmware_data = NULL;
		}
#if IS_ENABLED(CONFIG_DRM_PANEL_NOTIFY) || IS_ENABLED(CONFIG_OPLUS_CHG_DRM_PANEL_NOTIFY)
		if (chip->active_panel && chip->notifier_cookie)
			panel_event_notifier_unregister(chip->notifier_cookie);
#endif /* CONFIG_DRM_PANEL_NOTIFY */
		mt5806_lock_work_destroy(chip);
		if (chip->is_supply_by_hboost)
			wireless_pen_glink_exit();
		mt_wls_log(PR_INFO, "wls power disable\n");
		mt_set_gpio_value(chip, GP_2, 0);
		/* chip is allocated by devm_kzalloc, will be freed by devres automatically */
	}
}

static const struct of_device_id match_table[] = {
	{.compatible = "oplus,wls-tx-mt5806", },
	{ },
};

static const struct i2c_device_id mt5806_dev_id[] = {
	{"wls-tx-mt5806", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, mt5806_dev_id);

static struct i2c_driver mt5806_driver = {
	.driver   = {
		.name           = "wls-tx-mt5806",
		.owner          = THIS_MODULE,
		.of_match_table = match_table,
		.pm             = &mt5806_pm_ops,
	},
	.probe    = mt5806_probe,
	.shutdown = mt5806_shutdown,
	.id_table = mt5806_dev_id,
};

static int __init mt5806_driver_init(void)
{
	return i2c_add_driver(&mt5806_driver);
}
module_init(mt5806_driver_init);

static void __exit mt5806_driver_exit(void)
{
	i2c_del_driver(&mt5806_driver);
}
module_exit(mt5806_driver_exit);

MODULE_DESCRIPTION("Wireless Pen Charger mt5806");
MODULE_LICENSE("GPL v2");
