/***********************************************************
** Copyright (C), 2008-2025 Oplus. All rights reserved.
** File: oplus_sc83107_bcl.c
** Description: Dynamic BCL configuration for SC83107 boost IC
** Date: 2025-11-01
** -----------Revision History: -------------------------------
** <author>        <data>    <version >       <desc>
****************************************************************/

#define pr_fmt(fmt) "[SC83107_BCL]([%s][%d]): " fmt, __func__, __LINE__

#include <linux/device.h>
#include <linux/proc_fs.h>
#include <linux/power_supply.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>

#include "oplus_sc83107.h"
#include "oplus_sc83107_bcl.h"
#include <oplus_chg_module.h>
#include <oplus_chg.h>

/* Global proc directory for SC83107 dynamic BCL (independent from oplus_bcl_pmic5) */
static struct proc_dir_entry *sc83107_bcl_dir;

/* Maximum number of elements for DTS property parsing */
#define SC83107_BCL_DTS_MAX_ELEMENTS	64

/* Forward declarations for functions in oplus_sc83107.c */
extern int sc83107_lv0_comp_falling_threshold_set(struct sc83107_chip *chip, uint32_t mv);
extern int sc83107_lv1_comp_falling_threshold_set(struct sc83107_chip *chip, uint32_t mv);
extern int sc83107_lv2_comp_input_vol_threshold_set(struct sc83107_chip *chip, uint32_t mv);

/* Read battery temperature */
static int sc83107_read_battery_temp(struct sc83107_chip *chip, int *val)
{
	struct power_supply *batt_psy;
	union power_supply_propval prop_val = {0};
	int rc;

	*val = 250; /* Default 25.0°C */

	/* Get battery power supply */
	batt_psy = power_supply_get_by_name("battery");
	if (!batt_psy) {
		chg_err("Failed to get battery power supply, will retry later\n");
		return -ENODEV;
	}

	rc = power_supply_get_property(batt_psy, POWER_SUPPLY_PROP_TEMP, &prop_val);
	power_supply_put(batt_psy);
	if (rc) {
		chg_err("Battery temp read error: %d, will retry later\n", rc);
		return rc;
	}

	*val = prop_val.intval;
	/* Display temperature: val is in 0.1°C unit, e.g., 250 = 25.0°C, -50 = -5.0°C */
	chg_info("Read battery temp: %d (0.1°C unit)\n", *val);
	return 0;
}

/* Read battery cycle count */
static int sc83107_read_battery_cycle_count(struct sc83107_chip *chip, unsigned int *val)
{
	static struct power_supply *batt_psy;
	union power_supply_propval prop_val = {0};
	int rc;

	*val = 0; /* Default cycle count */

	/* Try to get battery power supply, retry if not available */
	if (!batt_psy)
		batt_psy = power_supply_get_by_name("battery");

	if (!batt_psy) {
		chg_err("Failed to get battery power supply for cycle count\n");
		return -ENODEV;
	}

	rc = power_supply_get_property(batt_psy, POWER_SUPPLY_PROP_CYCLE_COUNT, &prop_val);
	if (rc) {
		chg_err("Battery cycle count read error: %d\n", rc);
		/* Clear cached batt_psy to force retry next time */
		batt_psy = NULL;
		return rc;
	}

	*val = prop_val.intval;
	return 0;
}

/* Get temperature compensation range */
static int sc83107_get_temp_compensation_range(struct sc83107_chip *chip, int batt_temp)
{
	int i;
	int temp_compensation_range = 0;

	if (chip->dynamic_bcl_compensation_config_count > 0 &&
	    chip->dynamic_bcl_compensation_config != NULL) {
		for (i = 0; i < chip->dynamic_bcl_compensation_config_count; i++) {
			if (batt_temp <= chip->dynamic_bcl_compensation_config[i].temp) {
				temp_compensation_range = i;
				break;
			}
		}

		if (i == chip->dynamic_bcl_compensation_config_count)
			temp_compensation_range = chip->dynamic_bcl_compensation_config_count - 1;
	} else {
		chg_info("dynamic_bcl_compensation_config is NULL or count=0, using default\n");
	}

	return temp_compensation_range;
}

/* Get cycle count compensation range */
static int sc83107_get_cycle_compensation_range(unsigned int cycle_count)
{
	if (cycle_count >= SC83107_BCL_CYCLE_THRESH_HIGH) {
		return 3; /* [1000, ∞) */
	} else if (cycle_count >= SC83107_BCL_CYCLE_THRESH_MID) {
		return 2; /* [500, 1000) */
	} else if (cycle_count >= SC83107_BCL_CYCLE_THRESH_LOW) {
		return 1; /* [200, 500) */
	} else {
		return 0; /* [0, 200) */
	}
}

/* Calculate adjusted BCL threshold with compensation */
static int sc83107_calculate_adjusted_bcl_threshold(struct sc83107_chip *chip,
			int current_range,
			int temp_compensation_range,
			unsigned int cycle_count,
			unsigned int *adjust_lv0,
			unsigned int *adjust_lv1,
			unsigned int *adjust_lv2)
{
	unsigned int compensation_value = 0;

	if (chip->dynamic_bcl_compensation_config != NULL) {
		/* Validate temp_compensation_range */
		if (temp_compensation_range < 0 ||
		    temp_compensation_range >= chip->dynamic_bcl_compensation_config_count) {
			chg_err("Invalid temp_compensation_range=%d, comp_count=%d, using base values\n",
				temp_compensation_range, chip->dynamic_bcl_compensation_config_count);
			goto use_base;
		}

		/* Determine compensation value based on cycle_count */
		if (cycle_count >= SC83107_BCL_CYCLE_THRESH_HIGH) {
			compensation_value = chip->dynamic_bcl_compensation_config[temp_compensation_range].compensation_over_1000;
		} else if (cycle_count >= SC83107_BCL_CYCLE_THRESH_MID) {
			compensation_value = chip->dynamic_bcl_compensation_config[temp_compensation_range].compensation_500_1000;
		} else if (cycle_count >= SC83107_BCL_CYCLE_THRESH_LOW) {
			compensation_value = chip->dynamic_bcl_compensation_config[temp_compensation_range].compensation_200_500;
		}
	}

use_base:
	/* Apply compensation value to base values */
	*adjust_lv0 = chip->dynamic_bcl_config[current_range].lv0_mv + compensation_value;
	*adjust_lv1 = chip->dynamic_bcl_config[current_range].lv1_mv + compensation_value;
	*adjust_lv2 = chip->dynamic_bcl_config[current_range].lv2_mv + compensation_value;

	return 0;
}

/* Apply dynamic BCL thresholds based on temperature and cycle count */
static int sc83107_apply_dynamic_bcl_thresholds(struct sc83107_chip *chip, int batt_temp, bool force_write)
{
	int i;
	int current_range = 0;
	int temp_compensation_range = 0;
	int cycle_compensation_range = 0;
	unsigned int cycle_count = 0;
	unsigned int adjust_lv0 = 0;
	unsigned int adjust_lv1 = 0;
	unsigned int adjust_lv2 = 0;
	int ret = 0;

	if (!chip || !chip->support_dynamic_bcl || chip->dynamic_bcl_config_count <= 0) {
		chg_err("Invalid params: chip=%p, support_dynamic_bcl=%d, config_count=%d\n",
			chip, chip ? chip->support_dynamic_bcl : 0,
			chip ? chip->dynamic_bcl_config_count : 0);
		return -EINVAL;
	}

	mutex_lock(&chip->dynamic_bcl_lock);

	/* Find the appropriate temperature range */
	for (i = 0; i < chip->dynamic_bcl_config_count; i++) {
		if (batt_temp <= chip->dynamic_bcl_config[i].temp) {
			current_range = i;
			break;
		}
	}
	if (i == chip->dynamic_bcl_config_count)
		current_range = chip->dynamic_bcl_config_count - 1;

	/* Get temperature compensation range and cycle count if compensation is supported */
	if (chip->support_dynamic_bcl_compensation) {
		if (sc83107_read_battery_cycle_count(chip, &cycle_count) >= 0) {
			temp_compensation_range = sc83107_get_temp_compensation_range(chip, batt_temp);
			cycle_compensation_range = sc83107_get_cycle_compensation_range(cycle_count);

			/* Calculate adjusted thresholds with compensation */
			if (sc83107_calculate_adjusted_bcl_threshold(chip, current_range,
						temp_compensation_range, cycle_count,
						&adjust_lv0, &adjust_lv1, &adjust_lv2) < 0) {
				chg_err("Failed to calculate adjusted threshold, using base values\n");
				/* Fallback to base values if calculation fails */
				adjust_lv0 = chip->dynamic_bcl_config[current_range].lv0_mv;
				adjust_lv1 = chip->dynamic_bcl_config[current_range].lv1_mv;
				adjust_lv2 = chip->dynamic_bcl_config[current_range].lv2_mv;
			}
		} else {
			chg_err("Failed to read cycle count, using base values\n");
			/* Use base values if cycle count read fails */
			adjust_lv0 = chip->dynamic_bcl_config[current_range].lv0_mv;
			adjust_lv1 = chip->dynamic_bcl_config[current_range].lv1_mv;
			adjust_lv2 = chip->dynamic_bcl_config[current_range].lv2_mv;
		}
	} else {
		/* No compensation, use base values */
		adjust_lv0 = chip->dynamic_bcl_config[current_range].lv0_mv;
		adjust_lv1 = chip->dynamic_bcl_config[current_range].lv1_mv;
		adjust_lv2 = chip->dynamic_bcl_config[current_range].lv2_mv;
	}

	/* Apply thresholds if range changed or force write */
	if (force_write || chip->dynamic_bcl_pre_range != current_range ||
	    chip->prev_temp_compensation_range != temp_compensation_range ||
	    chip->prev_cycle_compensation_range != cycle_compensation_range) {
		ret = sc83107_lv0_comp_falling_threshold_set(chip, adjust_lv0);
		if (ret < 0) {
			chg_err("Failed to set LV0 threshold=%d, ret=%d\n", adjust_lv0, ret);
			goto exit;
		}

		ret = sc83107_lv1_comp_falling_threshold_set(chip, adjust_lv1);
		if (ret < 0) {
			chg_err("Failed to set LV1 threshold=%d, ret=%d\n", adjust_lv1, ret);
			goto exit;
		}

		ret = sc83107_lv2_comp_input_vol_threshold_set(chip, adjust_lv2);
		if (ret < 0) {
			chg_err("Failed to set LV2 threshold=%d, ret=%d\n", adjust_lv2, ret);
			goto exit;
		}

		chip->dynamic_bcl_pre_range = current_range;
		chip->prev_temp_compensation_range = temp_compensation_range;
		chip->prev_cycle_compensation_range = cycle_compensation_range;

		chg_info("Dynamic BCL update: temp=%d, cycle=%u, lv0=%d, lv1=%d, lv2=%d\n",
				batt_temp, cycle_count, adjust_lv0, adjust_lv1, adjust_lv2);
	}

exit:
	mutex_unlock(&chip->dynamic_bcl_lock);
	return ret;
}

/**
 * sc83107_update_dynamic_bcl_config - Update dynamic BCL configuration values
 * @chip: SC83107 chip pointer
 * @index: Index of the configuration to update
 * @temp: Temperature threshold
 * @lv0_mv: LV0 threshold in mV
 * @lv1_mv: LV1 threshold in mV
 * @lv2_mv: LV2 threshold in mV
 *
 * This function only updates the configuration values in memory.
 * To apply the configuration, call sc83107_apply_dynamic_bcl_thresholds separately.
 *
 * Return: 0 on success, negative error code on failure
 */
static int sc83107_update_dynamic_bcl_config(struct sc83107_chip *chip, int index,
				  int temp, int lv0_mv,
				  int lv1_mv, int lv2_mv)
{
	if (!chip) {
		chg_err("SC83107 chip is NULL in %s\n", __func__);
		return -EINVAL;
	}

	if (!chip->support_dynamic_bcl) {
		chg_err("dynamic_bcl is not supported\n");
		return -ENOTSUPP;
	}

	if (!chip->dynamic_bcl_config) {
		chg_err("dynamic_bcl_config is NULL\n");
		return -EINVAL;
	}

	if (index < 0 || index >= chip->dynamic_bcl_config_count) {
		chg_err("Invalid index %d, valid range: 0-%d\n",
				index, chip->dynamic_bcl_config_count - 1);
		return -EINVAL;
	}

	mutex_lock(&chip->dynamic_bcl_lock);
	chip->dynamic_bcl_config[index].temp = temp;
	chip->dynamic_bcl_config[index].lv0_mv = lv0_mv;
	chip->dynamic_bcl_config[index].lv1_mv = lv1_mv;
	chip->dynamic_bcl_config[index].lv2_mv = lv2_mv;
	mutex_unlock(&chip->dynamic_bcl_lock);

	return 0;
}

/* Forward declaration */
static int sc83107_force_apply_dynamic_bcl_threshold(struct sc83107_chip *chip);

/**
 * sc83107_do_restore_bcl_from_backup - Restore BCL config from backup data
 * @chip: SC83107 chip pointer
 * @is_auto: true if this is auto restore, false if manual restore
 *
 * Common function to restore dynamic_bcl_config from dynamic_bcl_data_backup
 *
 * Return: 0 on success, negative error code on failure
 */
static int sc83107_do_restore_bcl_from_backup(struct sc83107_chip *chip, bool is_auto)
{
	int i, ret;

	if (!chip) {
		chg_err("SC83107 chip is NULL in %s\n", __func__);
		return -ENOTSUPP;
	}
	if (!chip->support_dynamic_bcl) {
		chg_err("dynamic_bcl not supported\n");
		return -ENOTSUPP;
	}

	if (!chip->dynamic_bcl_data_backup) {
		chg_err("backup data not available\n");
		return -EINVAL;
	}

	chg_info("%s restore from backup\n", is_auto ? "Auto" : "Manual");

	/* Update config with values from backup */
	for (i = 0; i < chip->dynamic_bcl_config_count &&
	     i < chip->dynamic_bcl_data_backup_count; i++) {
		ret = sc83107_update_dynamic_bcl_config(chip, i,
				chip->dynamic_bcl_data_backup[i].temp,
				chip->dynamic_bcl_data_backup[i].lv0_mv,
				chip->dynamic_bcl_data_backup[i].lv1_mv,
				chip->dynamic_bcl_data_backup[i].lv2_mv);
		if (ret) {
			chg_err("Failed to %s restore from backup at index %d\n",
					is_auto ? "auto" : "manual", i);
			return ret;
		}
	}

	/* Apply the restored configuration based on current battery temperature */
	ret = sc83107_force_apply_dynamic_bcl_threshold(chip);
	if (ret) {
		chg_err("Failed to apply restored bcl config, err:%d\n", ret);
		return ret;
	}

	chg_info("%s update from backup\n", is_auto ? "Auto" : "Manual");
	return 0;
}

/* Force apply dynamic BCL threshold */
static int sc83107_force_apply_dynamic_bcl_threshold(struct sc83107_chip *chip)
{
	int batt_temp;

	if (sc83107_read_battery_temp(chip, &batt_temp) < 0) {
		chg_err("Failed to read battery temperature\n");
		return -EIO;
	}
	if (chip->dynamic_bcl_config_count <= 0) {
		chg_err("dynamic_bcl_config_count is 0, skip bcl check\n");
		return -EINVAL;
	}

	return sc83107_apply_dynamic_bcl_thresholds(chip, batt_temp, true);
}

/* Work handler for checking temperature and updating BCL */
static void sc83107_bcl_check(struct work_struct *work)
{
	struct sc83107_chip *chip = container_of(work, struct sc83107_chip, bcl_check_work);
	int batt_temp;

	if (!chip->support_dynamic_bcl) {
		return;
	}

	if (sc83107_read_battery_temp(chip, &batt_temp) < 0) {
		chg_err("Failed to read battery temperature\n");
		return;
	}

	if (chip->dynamic_bcl_config_count <= 0) {
		chg_err("dynamic_bcl_config_count is 0, skip bcl check\n");
		return;
	}

	sc83107_apply_dynamic_bcl_thresholds(chip, batt_temp, false);
}

/* Battery supply notifier callback */
static int battery_supply_callback(struct notifier_block *nb,
			unsigned long event, void *data)
{
	struct power_supply *psy = data;
	struct sc83107_chip *chip =
			container_of(nb, struct sc83107_chip, psy_nb);

	if (strncmp(psy->desc->name, "battery", strlen("battery")) != 0) {
		return NOTIFY_OK;
	}

	if (chip->support_dynamic_bcl) {
		schedule_work(&chip->bcl_check_work);
	}

	return NOTIFY_OK;
}

/* Manual restore work handler */
static void sc83107_manual_restore_bcl_from_backup(struct work_struct *work)
{
	struct delayed_work *delayed_work = to_delayed_work(work);
	struct sc83107_chip *chip = container_of(delayed_work, struct sc83107_chip, dynamic_bcl_manual_restore_work);

	/* Check if already triggered automatically */
	if (chip->dynamic_bcl_auto_restore_triggered) {
		chg_info("auto restore already triggered, skip manual restore\n");
		return;
	}
	chip->dynamic_bcl_manual_restore_triggered = true;
	sc83107_do_restore_bcl_from_backup(chip, false);
}

/* Auto restore work handler */
static void sc83107_auto_restore_bcl_from_backup(struct work_struct *work)
{
	struct delayed_work *delayed_work = to_delayed_work(work);
	struct sc83107_chip *chip = container_of(delayed_work, struct sc83107_chip, dynamic_bcl_auto_restore_work);

	/* Check if already triggered manually */
	if (chip->dynamic_bcl_manual_restore_triggered) {
		chg_info("Manual restore already triggered, skip auto restore\n");
		return;
	}
	chip->dynamic_bcl_auto_restore_triggered = true;
	sc83107_do_restore_bcl_from_backup(chip, true);
}

/* Proc read handler */
static ssize_t sc83107_dynamic_bcl_proc_read(struct file *file, char __user *buf,
		      size_t count, loff_t *ppos)
{
	struct sc83107_chip *chip = pde_data(file_inode(file));
	char buffer[1024];
	size_t len = 0;
	int i;
	bool manual_restore_pending;
	bool auto_restore_pending;

	if (!chip || !chip->support_dynamic_bcl) {
		len = snprintf(buffer, sizeof(buffer), "dynamic_bcl not supported\n");
		return simple_read_from_buffer(buf, count, ppos, buffer, len);
	}

	manual_restore_pending = delayed_work_pending(&chip->dynamic_bcl_manual_restore_work);
	auto_restore_pending = delayed_work_pending(&chip->dynamic_bcl_auto_restore_work);

	len += snprintf(buffer + len, sizeof(buffer) - len, "Status:\n");
	if (chip->dynamic_bcl_data_backup && chip->dynamic_bcl_data_backup_count > 0) {
		len += snprintf(buffer + len, sizeof(buffer) - len, "  Manual restore triggered: %s\n",
				chip->dynamic_bcl_manual_restore_triggered ? "Yes" : "No");
		len += snprintf(buffer + len, sizeof(buffer) - len, "  Manual restore pending: %s\n",
				manual_restore_pending ? "Yes" : "No");
		len += snprintf(buffer + len, sizeof(buffer) - len, "  Auto restore pending: %s\n",
				auto_restore_pending ? "Yes" : "No");
		len += snprintf(buffer + len, sizeof(buffer) - len, "  Auto restore triggered: %s\n",
				chip->dynamic_bcl_auto_restore_triggered ? "Yes" : "No");
	}

	len += snprintf(buffer + len, sizeof(buffer) - len, "\nDefault Config:\n");
	for (i = 0; i < chip->dynamic_bcl_config_count; i++) {
		if (len >= sizeof(buffer))
			break;
		len += snprintf(buffer + len, sizeof(buffer) - len,
				"[%d] temp=%d, lv0=%d, lv1=%d, lv2=%d\n",
				i,
				chip->dynamic_bcl_config[i].temp,
				chip->dynamic_bcl_config[i].lv0_mv,
				chip->dynamic_bcl_config[i].lv1_mv,
				chip->dynamic_bcl_config[i].lv2_mv);
	}

	if (chip->dynamic_bcl_data_backup && chip->dynamic_bcl_data_backup_count > 0) {
		len += snprintf(buffer + len, sizeof(buffer) - len, "\nBackup Config:\n");
		for (i = 0; i < chip->dynamic_bcl_data_backup_count; i++) {
			if (len >= sizeof(buffer))
				break;
			len += snprintf(buffer + len, sizeof(buffer) - len,
					"[%d] temp=%d, lv0=%d, lv1=%d, lv2=%d\n",
					i,
					chip->dynamic_bcl_data_backup[i].temp,
					chip->dynamic_bcl_data_backup[i].lv0_mv,
					chip->dynamic_bcl_data_backup[i].lv1_mv,
					chip->dynamic_bcl_data_backup[i].lv2_mv);
		}
	}

	return simple_read_from_buffer(buf, count, ppos, buffer, len);
}

/* Proc write handler */
static ssize_t sc83107_dynamic_bcl_proc_write(struct file *file, const char __user *buf,
		       size_t count, loff_t *ppos)
{
	struct sc83107_chip *chip = pde_data(file_inode(file));
	char buffer[16];
	int val;

	if (!chip) {
		pr_err("SC83107 chip is NULL in %s\n", __func__);
		return -ENODEV;
	}
	if (!chip->support_dynamic_bcl) {
		chg_err("dynamic_bcl not supported\n");
		return -ENOTSUPP;
	}

	if (count >= sizeof(buffer))
		return -EINVAL;

	if (copy_from_user(buffer, buf, count))
		return -EFAULT;

	buffer[count] = 0;

	/* Remove newline character */
	if (count > 0 && buffer[count - 1] == '\n')
		buffer[count - 1] = 0;

	/* Only accept '1' */
	if (kstrtoint(buffer, 10, &val) != 0 || val != 1) {
		chg_err("Invalid input, only '1' is accepted\n");
		return -EINVAL;
	}

	/* Check if backup data is available */
	if (!chip->dynamic_bcl_data_backup) {
		chg_err("backup data not available\n");
		return -EINVAL;
	}

	/* Mark as manually triggered and cancel auto restore work */
	chip->dynamic_bcl_manual_restore_triggered = true;
	cancel_delayed_work_sync(&chip->dynamic_bcl_auto_restore_work);

	/* Cancel any pending restore work */
	cancel_delayed_work_sync(&chip->dynamic_bcl_manual_restore_work);
	chg_info("dynamic_bcl_proc_write: trigger manual restore from backup, delay=%d ms\n",
			chip->dynamic_bcl_manual_restore_delay_ms);
	/* Schedule delayed work to restore after delay */
	schedule_delayed_work(&chip->dynamic_bcl_manual_restore_work,
			msecs_to_jiffies(chip->dynamic_bcl_manual_restore_delay_ms));

	return count;
}

static const struct proc_ops sc83107_dynamic_bcl_proc_ops = {
	.proc_read = sc83107_dynamic_bcl_proc_read,
	.proc_write = sc83107_dynamic_bcl_proc_write,
};

/* Load dynamic BCL data from device tree */
static int sc83107_load_dynamic_bcl_data_from_dts(struct sc83107_chip *chip, struct device *dev)
{
	struct device_node *dev_node = dev->of_node;
	int num_elem;
	int buf[SC83107_BCL_DTS_MAX_ELEMENTS] = {0};
	int i;
	int ret;

	/* Load default config (dynamic_bcl_data) - max values, used initially */
	num_elem = of_property_count_elems_of_size(dev_node, "oplus,sc83107,dynamic_bcl_data", sizeof(int));
	if (num_elem <= 0) {
		chg_err("dynamic_bcl_data not found or empty\n");
		return -EINVAL;
	}

	if (num_elem % 4) {
		chg_err("invalid len for dynamic_bcl_data\n");
		return -EINVAL;
	}

	if (num_elem > SC83107_BCL_DTS_MAX_ELEMENTS) {
		chg_err("dynamic_bcl_data exceeds max elements %d\n", SC83107_BCL_DTS_MAX_ELEMENTS);
		return -EINVAL;
	}

	ret = of_property_read_u32_array(dev_node, "oplus,sc83107,dynamic_bcl_data", (u32 *)buf, num_elem);
	if (ret) {
		chg_err("dynamic_bcl_data read failed %d\n", ret);
		return ret;
	}

	chip->dynamic_bcl_config_count = num_elem / 4;
	chip->dynamic_bcl_config = devm_kcalloc(dev,
			chip->dynamic_bcl_config_count, sizeof(struct sc83107_dynamic_bcl_data), GFP_KERNEL);
	if (!chip->dynamic_bcl_config) {
		chg_err("fail to alloc dynamic_bcl_config memory\n");
		return -ENOMEM;
	}

	for (i = 0; i < chip->dynamic_bcl_config_count; i++) {
		chip->dynamic_bcl_config[i].temp = buf[i * 4 + 0];
		chip->dynamic_bcl_config[i].lv0_mv = buf[i * 4 + 1];
		chip->dynamic_bcl_config[i].lv1_mv = buf[i * 4 + 2];
		chip->dynamic_bcl_config[i].lv2_mv = buf[i * 4 + 3];
		chg_info("dynamic_bcl_config[%d]:temp=%d, lv0=%d, lv1=%d, lv2=%d\n", i,
			chip->dynamic_bcl_config[i].temp,
			chip->dynamic_bcl_config[i].lv0_mv,
			chip->dynamic_bcl_config[i].lv1_mv,
			chip->dynamic_bcl_config[i].lv2_mv);
	}

	return 0;
}

/**
 * sc83107_load_backup_bcl_data_from_dts - Read backup BCL data from dts
 * @chip: SC83107 chip pointer
 * @dev: Device pointer
 *
 * Read oplus,sc83107,dynamic_bcl_data_alt from dts, initialize dynamic_bcl_data_backup
 * This is an optional parameter. Returns -EINVAL if property doesn't exist (normal case,
 * caller will handle it as optional). Returns error code if property exists but fails
 * to parse/load.
 *
 * Return: 0 on success, -EINVAL if property doesn't exist (optional), negative error code on failure
 */
static int sc83107_load_backup_bcl_data_from_dts(struct sc83107_chip *chip, struct device *dev)
{
	struct device_node *dev_node = dev->of_node;
	int num_elem;
	int buf[SC83107_BCL_DTS_MAX_ELEMENTS] = {0};
	int i;
	int ret;

	num_elem = of_property_count_elems_of_size(dev_node, "oplus,sc83107,dynamic_bcl_data_alt", sizeof(int));
	if (num_elem < 0) {
		/* Property doesn't exist, which is fine for optional parameter */
		return -EINVAL;
	}
	if (num_elem == 0) {
		chg_err("dynamic_bcl_data_alt is empty\n");
		return -EINVAL;
	}

	if (num_elem % 4) {
		chg_err("invalid len for dynamic_bcl_data_alt\n");
		return -EINVAL;
	}

	if (num_elem > SC83107_BCL_DTS_MAX_ELEMENTS) {
		chg_err("dynamic_bcl_data_alt exceeds max elements %d\n", SC83107_BCL_DTS_MAX_ELEMENTS);
		return -EINVAL;
	}

	ret = of_property_read_u32_array(dev_node, "oplus,sc83107,dynamic_bcl_data_alt", (u32 *)buf, num_elem);
	if (ret) {
		chg_err("dynamic_bcl_data_alt read failed %d\n", ret);
		return ret;
	}

	chip->dynamic_bcl_data_backup_count = num_elem / 4;
	chip->dynamic_bcl_data_backup = devm_kcalloc(dev,
			chip->dynamic_bcl_data_backup_count, sizeof(struct sc83107_dynamic_bcl_data), GFP_KERNEL);
	if (!chip->dynamic_bcl_data_backup) {
		chg_err("fail to alloc dynamic_bcl_data_backup memory\n");
		return -ENOMEM;
	}

	for (i = 0; i < chip->dynamic_bcl_data_backup_count; i++) {
		chip->dynamic_bcl_data_backup[i].temp = buf[i * 4 + 0];
		chip->dynamic_bcl_data_backup[i].lv0_mv = buf[i * 4 + 1];
		chip->dynamic_bcl_data_backup[i].lv1_mv = buf[i * 4 + 2];
		chip->dynamic_bcl_data_backup[i].lv2_mv = buf[i * 4 + 3];
		chg_info("dynamic_bcl_data_backup[%d]:temp=%d, lv0=%d, lv1=%d, lv2=%d\n", i,
			chip->dynamic_bcl_data_backup[i].temp,
			chip->dynamic_bcl_data_backup[i].lv0_mv,
			chip->dynamic_bcl_data_backup[i].lv1_mv,
			chip->dynamic_bcl_data_backup[i].lv2_mv);
	}

	return 0;
}

/* Load compensation configuration from device tree */
static int sc83107_load_dynamic_bcl_compensation_from_dts(struct sc83107_chip *chip, struct device *dev)
{
	struct device_node *dev_node = dev->of_node;
	int num_elem;
	int buf[SC83107_BCL_DTS_MAX_ELEMENTS] = {0};
	int i;
	int ret;

	chip->support_dynamic_bcl_compensation = of_property_read_bool(dev_node, "oplus,sc83107,support_dynamic_bcl_compensation");

	if (!chip->support_dynamic_bcl_compensation)
		return 0;

	num_elem = of_property_count_elems_of_size(dev_node, "oplus,sc83107,dynamic_bcl_data_compensation", sizeof(int));
	if (num_elem > 0) {
		if (num_elem % 4) {
			chg_err("invalid len for dynamic_bcl_data_compensation\n");
			return -EINVAL;
		}

		if (num_elem > SC83107_BCL_DTS_MAX_ELEMENTS) {
			chg_err("dynamic_bcl_data_compensation exceeds max elements %d\n", SC83107_BCL_DTS_MAX_ELEMENTS);
			return -EINVAL;
		}

		ret = of_property_read_u32_array(dev_node, "oplus,sc83107,dynamic_bcl_data_compensation", (u32 *)buf, num_elem);
		if (ret) {
			chg_err("dynamic_bcl_data_compensation read failed %d\n", ret);
			return ret;
		}

		chip->dynamic_bcl_compensation_config_count = num_elem / 4;
		chip->dynamic_bcl_compensation_config = devm_kcalloc(dev,
				chip->dynamic_bcl_compensation_config_count, sizeof(struct sc83107_dynamic_bcl_compensation), GFP_KERNEL);
		if (!chip->dynamic_bcl_compensation_config) {
			chg_err("fail to alloc dynamic_bcl_compensation_config memory\n");
			return -ENOMEM;
		}

		for (i = 0; i < chip->dynamic_bcl_compensation_config_count; i++) {
			chip->dynamic_bcl_compensation_config[i].temp = buf[i * 4 + 0];
			chip->dynamic_bcl_compensation_config[i].compensation_200_500 = buf[i * 4 + 1];
			chip->dynamic_bcl_compensation_config[i].compensation_500_1000 = buf[i * 4 + 2];
			chip->dynamic_bcl_compensation_config[i].compensation_over_1000 = buf[i * 4 + 3];
			chg_info("dynamic_bcl_compensation_config[%d]:temp=%d, [200,500)=%d, [500,1000)=%d, [1000,∞)=%d\n", i,
				chip->dynamic_bcl_compensation_config[i].temp,
				chip->dynamic_bcl_compensation_config[i].compensation_200_500,
				chip->dynamic_bcl_compensation_config[i].compensation_500_1000,
				chip->dynamic_bcl_compensation_config[i].compensation_over_1000);
		}
		return 0;
	}

	return 0;
}

/* Initialize dynamic BCL configuration */
void sc83107_dynamic_bcl_init(struct sc83107_chip *chip, struct device *dev)
{
	struct device_node *dev_node = dev->of_node;

	chip->support_dynamic_bcl = of_property_read_bool(dev_node, "oplus,sc83107,support_dynamic_bcl");

	if (!chip->support_dynamic_bcl)
		return;

	/* Parse manual restore delay */
	if (of_property_read_u32(dev_node, "oplus,sc83107,dynamic_bcl_manual_restore_delay_ms",
				 (u32 *)&chip->dynamic_bcl_manual_restore_delay_ms)) {
		chip->dynamic_bcl_manual_restore_delay_ms = 15000; /* 15s default */
	}
	if (chip->dynamic_bcl_manual_restore_delay_ms < 10000)
		chip->dynamic_bcl_manual_restore_delay_ms = 10000;
	if (chip->dynamic_bcl_manual_restore_delay_ms > 60000)
		chip->dynamic_bcl_manual_restore_delay_ms = 60000;

	/* Parse auto restore delay */
	if (of_property_read_u32(dev_node, "oplus,sc83107,dynamic_bcl_auto_restore_delay_ms",
				 (u32 *)&chip->dynamic_bcl_auto_restore_delay_ms)) {
		chip->dynamic_bcl_auto_restore_delay_ms = 100000; /* 100s default */
	}
	if (chip->dynamic_bcl_auto_restore_delay_ms < 60000)
		chip->dynamic_bcl_auto_restore_delay_ms = 60000;
	if (chip->dynamic_bcl_auto_restore_delay_ms > 120000)
		chip->dynamic_bcl_auto_restore_delay_ms = 120000;

	/* Load dynamic BCL data from dts */
	if (sc83107_load_dynamic_bcl_data_from_dts(chip, dev) < 0) {
		chip->support_dynamic_bcl = false;
		chg_err("Failed to load dynamic_bcl_data from dts\n");
		return;
	}

	/* Check if we have default config */
	if (!chip->dynamic_bcl_config || chip->dynamic_bcl_config_count <= 0) {
		chg_err("dynamic_bcl_data not found, disable dynamic BCL\n");
		chip->support_dynamic_bcl = false;
		return;
	}

	/* Load backup BCL data from dts (optional) */
	sc83107_load_backup_bcl_data_from_dts(chip, dev);

	/* Load compensation configuration */
	sc83107_load_dynamic_bcl_compensation_from_dts(chip, dev);

	mutex_init(&chip->dynamic_bcl_lock);
	chip->dynamic_bcl_pre_range = -1;
	chip->prev_temp_compensation_range = -1;
	chip->prev_cycle_compensation_range = -1;

	/* Initialize restore flows and proc only when backup table exists */
	if (chip->dynamic_bcl_data_backup && chip->dynamic_bcl_data_backup_count > 0) {
		/* init delayed works */
		INIT_DELAYED_WORK(&chip->dynamic_bcl_manual_restore_work, sc83107_manual_restore_bcl_from_backup);
		INIT_DELAYED_WORK(&chip->dynamic_bcl_auto_restore_work, sc83107_auto_restore_bcl_from_backup);
		chip->dynamic_bcl_auto_restore_triggered = false;
		chip->dynamic_bcl_manual_restore_triggered = false;

		/* Create independent proc directory for SC83107 BCL */
		/* Use separate directory to avoid coupling with oplus_bcl_pmic5.c */
		if (!sc83107_bcl_dir) {
			sc83107_bcl_dir = proc_mkdir("sc83107_bcl", NULL);
			if (!sc83107_bcl_dir) {
				chg_err("Failed to create sc83107_bcl proc directory\n");
			}
		}

		/* Create proc entry in sc83107_bcl subdirectory */
		if (sc83107_bcl_dir) {
			chip->dynamic_bcl_proc_entry = proc_create_data("dynamic_bcl", 0664,
					sc83107_bcl_dir, &sc83107_dynamic_bcl_proc_ops, chip);
			if (!chip->dynamic_bcl_proc_entry) {
				chg_err("Couldn't create dynamic_bcl proc entry\n");
			}
		} else {
			chg_err("Cannot create dynamic_bcl: sc83107_bcl directory not available\n");
		}

		/* schedule auto-restore */
		chg_info("Scheduled auto restore from backup in %d ms\n",
				chip->dynamic_bcl_auto_restore_delay_ms);
		schedule_delayed_work(&chip->dynamic_bcl_auto_restore_work,
				msecs_to_jiffies(chip->dynamic_bcl_auto_restore_delay_ms));
	} else {
		chg_info("Skip proc entry and auto restore: no backup data\n");
	}

	/* Initialize work for temperature monitoring */
	INIT_WORK(&chip->bcl_check_work, sc83107_bcl_check);
	chg_info("Initialized bcl_check_work for temperature monitoring\n");

	/* Register battery supply notifier */
	chip->psy_nb.notifier_call = battery_supply_callback;
	if (power_supply_reg_notifier(&chip->psy_nb) < 0) {
		chg_err("Failed to register power supply notifier\n");
		/* Continue even if notifier registration fails */
	} else {
		/* Try to trigger initial check if battery is already available */
		schedule_work(&chip->bcl_check_work);
	}
}

/* Cleanup dynamic BCL resources */
void sc83107_dynamic_bcl_cleanup(struct sc83107_chip *chip)
{
	if (!chip)
		return;

	/* Unregister power supply notifier */
	power_supply_unreg_notifier(&chip->psy_nb);

	/* Cancel work */
	cancel_work_sync(&chip->bcl_check_work);
	if (chip->dynamic_bcl_data_backup && chip->dynamic_bcl_data_backup_count > 0) {
		cancel_delayed_work_sync(&chip->dynamic_bcl_manual_restore_work);
		cancel_delayed_work_sync(&chip->dynamic_bcl_auto_restore_work);
	}

	/* Remove dynamic BCL proc entry */
	if (chip->dynamic_bcl_proc_entry) {
		proc_remove(chip->dynamic_bcl_proc_entry);
		chip->dynamic_bcl_proc_entry = NULL;
	}

	/* Destroy mutex */
	mutex_destroy(&chip->dynamic_bcl_lock);

	/* Remove global proc directory if no more entries */
	/* Note: We don't remove sc83107_bcl_dir here as it's shared and may be used by other instances */
	/* The kernel will clean it up when the module is unloaded */
}
