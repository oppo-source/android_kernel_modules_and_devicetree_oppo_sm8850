// Copyright 2023 YourCompany. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//
// This file contains the implementation of the dual cells batt protection.
// Author: oplus
// Created: 2025-06-10
// Notes: This file is part of the dual cells batt protection

#define pr_fmt(fmt) "[PROTECTION_CHG]([%s][%d]): " fmt, __func__, __LINE__

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/of_platform.h>
#include <linux/of.h>
#include <linux/mutex.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/list.h>
#include <linux/gfp.h>
#include <linux/power_supply.h>
#include <linux/timer.h>

#include <oplus_chg.h>
#include <oplus_chg_voter.h>
#include <oplus_chg_module.h>
#include <oplus_mms.h>
#include <oplus_mms_wired.h>
#include <oplus_mms_gauge.h>
#include <oplus_chg_comm.h>
#include <oplus_chg_ic.h>
#include <oplus_chg_cpa.h>
#include <oplus_chg_vooc.h>
#include <oplus_chg_ufcs.h>
#include <oplus_chg_pps.h>
#include <oplus_chg_monitor.h>
#include <oplus_chg_wls.h>
#include "oplus_gauge_common.h"

const char *const protect_reason_str[] = {
	[PROTECT_UNKNOWN]	= "UNKNOWN",
	[FCC_SMALL]	        = "FCC SMALL",
	[INTFCC_SMALL]	        = "INTFCC SMALL",
	[SOC_JUMP]	        = "SOC JUMP",
	[FCC_RECOVER]	        = "FCC RECOVER",
	[FCC_RECOVERING]        = "FCC_RECOVERING",
	[INTFCC_RECOVER]	= "INTFCC RECOVER",
	[SN_CHANGE]	        = "SN CHANGE",
	[XVDD_OCCUR]	        = "XVDD OCCUR",
};

const char *get_protect_reason_str(enum dual_cells_protect_reason type)
{
	if (type < 0 || type >= REASON_MAX)
		return "Unknown";
	return protect_reason_str[type];
}

__maybe_unused static bool
is_err_topic_available(struct oplus_mms_gauge *chip)
{
	if (!chip->err_topic)
		chip->err_topic = oplus_mms_get_by_name("error");
	return !!chip->err_topic;
}

__maybe_unused static bool
is_vooc_curr_votable_available(struct oplus_mms_gauge *chip)
{
	if (!chip->dcb_protect.vooc_curr_votable)
		chip->dcb_protect.vooc_curr_votable = find_votable("VOOC_CURR");
	return !!chip->dcb_protect.vooc_curr_votable;
}

__maybe_unused static bool
is_wls_fcc_votable_available(struct oplus_mms_gauge *chip)
{
	if (!chip->dcb_protect.wls_fcc_votable)
		chip->dcb_protect.wls_fcc_votable = find_votable("WLS_FCC");
	return !!chip->dcb_protect.wls_fcc_votable;
}

__maybe_unused static bool
is_ufcs_curr_votable_available(struct oplus_mms_gauge *chip)
{
	if (!chip->dcb_protect.ufcs_curr_votable)
		chip->dcb_protect.ufcs_curr_votable = find_votable("UFCS_CURR");
	return !!chip->dcb_protect.ufcs_curr_votable;
}

__maybe_unused static bool
is_pps_curr_votable_available(struct oplus_mms_gauge *chip)
{
	if (!chip->dcb_protect.pps_curr_votable)
		chip->dcb_protect.pps_curr_votable = find_votable("PPS_CURR");
	return !!chip->dcb_protect.pps_curr_votable;
}

static void oplus_protection_check_temp_region(struct oplus_mms_gauge *chip)
{
	int bat_temp_region = 0;
	if (!chip) {
		chg_err("chip is null");
		return;
	}

	bat_temp_region = chip->dcb_protect.temp_region;

	if (bat_temp_region > TEMP_REGION_PRE_NORMAL &&
		bat_temp_region < TEMP_REGION_WARM)
		chip->dcb_protect.is_temp_normal = true;
	else
		chip->dcb_protect.is_temp_normal = false;
}

static void oplus_check_charging_type(struct oplus_mms_gauge *chip)
{
	union mms_msg_data data = { 0 };
	int rc;
	enum oplus_chg_protocol_type chg_type;

	rc = oplus_mms_get_item_data(chip->cpa_topic, CPA_ITEM_ALLOW, &data, false);
	chg_type = data.intval;
	if (chg_type == CHG_PROTOCOL_PPS) {
		chip->dcb_protect.pps_charging = true;
		chip->dcb_protect.vooc_charging = false;
		chip->dcb_protect.ufcs_charging = false;
	} else if (chg_type == CHG_PROTOCOL_VOOC) {
		chip->dcb_protect.vooc_charging = true;
		chip->dcb_protect.pps_charging = false;
		chip->dcb_protect.ufcs_charging = false;
	} else if (chg_type == CHG_PROTOCOL_UFCS) {
		chip->dcb_protect.ufcs_charging = true;
		chip->dcb_protect.vooc_charging = false;
		chip->dcb_protect.pps_charging = false;
	}

	chip->dcb_protect.wls_fastchg_charging = false;

	rc = oplus_mms_get_item_data(chip->wls_topic, WLS_ITEM_FASTCHG_STATUS, &data, false);
	if (rc < 0)
		chip->dcb_protect.wls_fastchg_charging = false;
	else
		chip->dcb_protect.wls_fastchg_charging = !!data.intval;
}

static void cancel_limit_cur(struct oplus_mms_gauge *chip)
{
	if (is_vooc_curr_votable_available(chip))
		vote(chip->dcb_protect.vooc_curr_votable, DCB_PROTECT_VOTER,
			 false, 0, false);

	if (is_ufcs_curr_votable_available(chip))
		vote(chip->dcb_protect.ufcs_curr_votable, DCB_PROTECT_VOTER,
			 false, 0, false);

	if (is_pps_curr_votable_available(chip))
		vote(chip->dcb_protect.pps_curr_votable, DCB_PROTECT_VOTER,
			 false, 0, false);

	if (is_wls_fcc_votable_available(chip))
		vote(chip->dcb_protect.wls_fcc_votable, DCB_PROTECT_VOTER,
			 false, 0, false);
}

static void execute_limit_cur(struct oplus_mms_gauge *chip, int curr)
{
	oplus_check_charging_type(chip);
	if (chip->dcb_protect.ufcs_charging &&
	    is_ufcs_curr_votable_available(chip))
		vote(chip->dcb_protect.ufcs_curr_votable, DCB_PROTECT_VOTER,
			true, curr, false);

	if (chip->dcb_protect.pps_charging &&
	    is_pps_curr_votable_available(chip))
		vote(chip->dcb_protect.pps_curr_votable, DCB_PROTECT_VOTER,
			 true, curr, false);

	if (chip->dcb_protect.vooc_charging &&
	    is_vooc_curr_votable_available(chip))
		vote(chip->dcb_protect.vooc_curr_votable, DCB_PROTECT_VOTER,
			 true, curr, false);

	if (chip->dcb_protect.wls_fastchg_charging &&
	    is_wls_fcc_votable_available(chip))
		vote(chip->dcb_protect.wls_fcc_votable, DCB_PROTECT_VOTER,
			 true, curr, false);
}

static void oplus_protection_limit_curr(struct oplus_mms_gauge *chip)
{
	int curr = 0;
	if (!chip || !chip->wired_online)
		return;

	curr = chip->dcb_protect.spec.cur_limit;

	if (chip->dcb_protect.batt_health) {
		cancel_limit_cur(chip);
	} else {
		execute_limit_cur(chip, curr);
	}
}

static void oplus_protection_fcc_check(struct oplus_mms_gauge *chip)
{
	static int fcc_reduce_count = 0;
	static int batt_cc = INVALID_CC;
	static int pre_batt_cc = INVALID_CC;

	if (!chip->dcb_protect.is_temp_normal)
		return;

	if (!chip->batt_full)
		return;

	batt_cc = chip->dcb_protect.batt_cc;

	if (batt_cc == pre_batt_cc)
		return;

	if (chip->dcb_protect.batt_fcc < chip->dcb_protect.spec.min_fcc) {
		if (fcc_reduce_count < ERROR_COUNT_LIMIT)
			fcc_reduce_count++;
		if (chip->dcb_protect.fcc_track_support ||
			chip->dcb_protect.fcc_check_support) {
			chip->dcb_protect.reason = FCC_SMALL;
			schedule_delayed_work(&chip->dcb_protect_track_work, 0);
			msleep(DUAL_CELLS_TRACK_INTERVAL_MS);
		}
		chg_info("fcc is too small, count = %d, batt_fcc %d, batt_cc %d",
			fcc_reduce_count, chip->dcb_protect.batt_fcc, batt_cc);
	} else {
		fcc_reduce_count = 0;
		chg_info("fcc %d is more than min_fcc, cancel count", chip->dcb_protect.batt_fcc);
	}

	pre_batt_cc = batt_cc;

	if (fcc_reduce_count >= ERROR_COUNT_LIMIT) {
		fcc_reduce_count = 0;
		chip->dcb_protect.reason = FCC_SMALL;
		if (chip->dcb_protect.fcc_check_support)
			chip->dcb_protect.batt_health = false;
		chg_err("fcc is too small, batt is not health");
	}
}

#define SECOND_TO_HOUR 3600
#define CHECK_INTERVAL 5
#define INTER_FCC_LOW_THR 100

static void oplus_protection_integral_fcc_cal(struct oplus_mms_gauge *chip)
{
	int capacity = 0;
	int ibat = 0;
	if (!chip) {
		chg_err("chip is null");
		return;
	}

	if (!chip->wired_online)
		return;

	if (chip->dcb_protect.plugin_soc != INVALID_SOC &&
		chip->dcb_protect.plugin_soc > FCC_INTEGRAL_THR)
		return;

	if (!chip->dcb_protect.is_temp_normal) {
		chip->dcb_protect.intfcc_valid = false;
		return;
	}

	if (chip->dcb_protect.intfcc_checked)
		return;

	if (chip->dcb_protect.rechging)
		return;

	ibat = chip->dcb_protect.ibat_ma;

	capacity = ((-ibat) * CHECK_INTERVAL * INT_FCC_UNIT) / SECOND_TO_HOUR;
	chip->dcb_protect.batt_integral_fcc += capacity;

	if (chip->batt_full && ibat > BATT_FULL_IBAT_MA &&
		chip->dcb_protect.intfcc_valid &&
		chip->dcb_protect.batt_integral_fcc > INTER_FCC_LOW_THR)
		chip->dcb_protect.intfcc_checked = true;
}

static void oplus_protection_integral_fcc_check(struct oplus_mms_gauge *chip)
{
	static int intfcc_reduce_count = 0;
	static int batt_cc = INVALID_CC;
	static int pre_batt_cc = INVALID_CC;
	int intfcc = 0;

	if (!chip->dcb_protect.intfcc_checked)
		return;

	if (chip->dcb_protect.rechging || !chip->wired_online)
		return;

	batt_cc = chip->dcb_protect.batt_cc;

	if (batt_cc == pre_batt_cc)
		return;

	intfcc = chip->dcb_protect.batt_integral_fcc / (100 - chip->dcb_protect.plugin_soc);

	if (intfcc < chip->dcb_protect.spec.min_oplus_fcc) {
		if (intfcc_reduce_count < ERROR_COUNT_LIMIT)
			intfcc_reduce_count++;
		chg_info("integral fcc is too small, count = %d, intfcc %d, batt_cc %d",
			intfcc_reduce_count, intfcc, batt_cc);
		if (chip->dcb_protect.intfcc_check_support ||
		    chip->dcb_protect.intfcc_track_support) {
			chip->dcb_protect.reason = INTFCC_SMALL;
			schedule_delayed_work(&chip->dcb_protect_track_work, 0);
			msleep(DUAL_CELLS_TRACK_INTERVAL_MS);
		}
	} else {
		intfcc_reduce_count = 0;
		chg_info("intfcc %d is more than min_fcc, cancel count", intfcc);
	}

	pre_batt_cc = batt_cc;

	if (intfcc_reduce_count >= ERROR_COUNT_LIMIT) {
		if (chip->dcb_protect.intfcc_check_support)
		    chip->dcb_protect.batt_health = false;
		intfcc_reduce_count = 0;
		chg_err("integral fcc is too small, batt is not health");
	}
}

static bool dcb_soc_jump_occur(struct oplus_mms_gauge *chip)
{
	int soc_delta = 0;
	int cur_soc = 0;
	int c_soc_delta = 0;
	int final_soc_delta = 0;
	bool full_soc_low = false;
	bool ret = false;
	int jump_thr = 0;

	if (!chip) {
		chg_err("chip is null, return");
		return ret;
	}

	jump_thr = chip->dcb_protect.spec.soc_jump_thr;
	cur_soc = chip->dcb_protect.batt_soc;
	soc_delta = cur_soc - chip->dcb_protect.pre_batt_soc;
	soc_delta = soc_delta >= 0 ? soc_delta : -soc_delta;

	c_soc_delta = (chip->dcb_protect.c_soc - chip->dcb_protect.pre_c_soc) / C_SOC_UNIT;
	c_soc_delta = c_soc_delta >= 0 ? c_soc_delta : -c_soc_delta;

	final_soc_delta = soc_delta > c_soc_delta ? soc_delta : c_soc_delta;

	if (chip->batt_full) {
		if (chip->dcb_protect.batt_soc < 100 - jump_thr ||
		   ((chip->dcb_protect.c_soc / C_SOC_UNIT) < 100 - jump_thr))
			full_soc_low = true;
	}

	if (soc_delta > jump_thr || full_soc_low)
		ret = true;

	return ret;
}

static void oplus_protection_soc_check(struct oplus_mms_gauge *chip)
{
	static int soc_jump_count = 0;
	static int batt_cc = 0;
	static int jump_occur_cc = -1;

	if (!chip->dcb_protect.is_temp_normal)
		goto cycle_out;

	batt_cc = chip->dcb_protect.batt_cc;

	if (dcb_soc_jump_occur(chip)) {
		if (batt_cc == jump_occur_cc)
			goto cycle_out;

		if (jump_occur_cc != -1 && (batt_cc - jump_occur_cc > 5))
			soc_jump_count = 0;

		jump_occur_cc = batt_cc;
		soc_jump_count++;

		chg_info("soc jump occur, soc_jump_count %d, jump_occur_cc %d, "
		"pre_soc %d, soc %d, c_soc %d, pre_c_soc %d ",
		soc_jump_count, jump_occur_cc, chip->dcb_protect.pre_batt_soc,
		chip->dcb_protect.batt_soc, chip->dcb_protect.c_soc, chip->dcb_protect.pre_c_soc);

		if (soc_jump_count >= ERROR_COUNT_LIMIT) {
			if (chip->dcb_protect.soc_check_support)
				chip->dcb_protect.batt_health = false;
			soc_jump_count = 0;
			jump_occur_cc = -1;
			chg_err("batt not health by soc jump");
		}
		if (chip->dcb_protect.soc_check_support ||
			chip->dcb_protect.soc_track_support) {
			chip->dcb_protect.reason = SOC_JUMP;
			schedule_delayed_work(&chip->dcb_protect_track_work, 0);
			msleep(DUAL_CELLS_TRACK_INTERVAL_MS);
		}
	}

cycle_out:
	chip->dcb_protect.pre_batt_soc = chip->dcb_protect.batt_soc;
	chip->dcb_protect.pre_c_soc = chip->dcb_protect.c_soc;
}

static void oplus_protection_fcc_recover_check(struct oplus_mms_gauge *chip)
{
	static int fcc_recover_count = 0;
	static int batt_cc = INVALID_CC;
	static int pre_batt_cc = INVALID_CC;
	int recover_fcc = 0;
	int batt_fcc = 0;

	if (!chip->batt_full)
		return;

	if (chip->dcb_protect.reason != FCC_SMALL &&
	    chip->dcb_protect.reason != FCC_RECOVERING)
		return;

	batt_cc = chip->dcb_protect.batt_cc;
	batt_fcc = chip->dcb_protect.batt_fcc;

	if (batt_cc == pre_batt_cc)
		return;

	recover_fcc =
		chip->dcb_protect.spec.cur_recover_thr + chip->dcb_protect.spec.min_fcc;

	if (batt_fcc > recover_fcc) {
		if (fcc_recover_count < ERROR_COUNT_LIMIT) {
			fcc_recover_count++;
			chg_info("batt intfcc is recovering, fcc_recover_count = %d, cc = %d, fcc %d \n",
				fcc_recover_count, batt_cc, batt_fcc);
		}
		if (chip->dcb_protect.fcc_check_support ||
			chip->dcb_protect.fcc_track_support) {
			chip->dcb_protect.reason = FCC_RECOVERING;
			schedule_delayed_work(&chip->dcb_protect_track_work, 0);
			msleep(DUAL_CELLS_TRACK_INTERVAL_MS);
		}
	} else {
		fcc_recover_count = 0;
		chg_info("batt intfcc is not recovering, cc = %d, fcc %d \n",
				batt_cc, batt_fcc);
	}

	pre_batt_cc = batt_cc;

	if (fcc_recover_count >= ERROR_COUNT_LIMIT) {
		if (chip->dcb_protect.fcc_check_support)
			chip->dcb_protect.batt_health = true;
		fcc_recover_count = 0;
		if (chip->dcb_protect.fcc_check_support ||
			chip->dcb_protect.fcc_track_support) {
			chip->dcb_protect.reason = FCC_RECOVER;
			schedule_delayed_work(&chip->dcb_protect_track_work, 0);
		}
		chg_info("batt fcc is recover\n");
	}
}

static void oplus_protection_int_fcc_recover_check(struct oplus_mms_gauge *chip)
{
	static int intfcc_recover_count = 0;
	static int batt_cc = INVALID_CC;
	static int pre_batt_cc = INVALID_CC;
	int recover_fcc = 0;
	int intfcc = 0;

	if (!chip || !chip->dcb_protect.intfcc_checked || !chip->wired_online)
		return;

	batt_cc = chip->dcb_protect.batt_cc;

	if (batt_cc == pre_batt_cc)
		return;

	recover_fcc =
		chip->dcb_protect.spec.cur_recover_thr + chip->dcb_protect.spec.min_oplus_fcc;
	intfcc = chip->dcb_protect.batt_integral_fcc / (100 - chip->dcb_protect.plugin_soc);

	if (intfcc >= recover_fcc) {
		if (intfcc_recover_count < ERROR_COUNT_LIMIT) {
			intfcc_recover_count++;
			chg_info("batt intfcc is recovering, intfcc = %d, intfcc_recover_count = %d, cc = %d \n",
				intfcc, intfcc_recover_count, batt_cc);
		}
		if (chip->dcb_protect.intfcc_check_support ||
			chip->dcb_protect.intfcc_track_support) {
			chip->dcb_protect.reason = INTFCC_RECOVER;
			schedule_delayed_work(&chip->dcb_protect_track_work, 0);
		}
	} else {
		intfcc_recover_count = 0;
		chg_info("intfcc is not recover, intfcc = %d, cc = %d \n",
				intfcc, batt_cc);
	}

	pre_batt_cc = batt_cc;

	if (intfcc_recover_count >= ERROR_COUNT_LIMIT) {
		if (chip->dcb_protect.intfcc_check_support)
			chip->dcb_protect.batt_health = true;
		intfcc_recover_count = 0;
		chip->dcb_protect.reason = INTFCC_RECOVER;
		chg_info("integral batt fcc is recover\n");
	}
}

void oplus_dcb_protect_check_start(struct oplus_mms_gauge *chip)
{
	int soc = 0;
	union mms_msg_data data = { 0 };
	if (!chip || !chip->dcb_protect.dcb_support) {
		chg_err("chip is null or not support, return\n");
		return;
	}
	oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_SOC, &data, false);
	soc = data.intval;
	chip->dcb_protect.pre_batt_soc = soc;
	chip->dcb_protect.batt_soc = soc;

	oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_C_SOC, &data, true);
	chip->dcb_protect.c_soc = data.intval;
	chip->dcb_protect.pre_c_soc = data.intval;

	if (chip->wired_online) {
		chip->dcb_protect.plugin_soc = soc;
		chip->dcb_protect.batt_integral_fcc = 0;
		chip->dcb_protect.intfcc_checked = false;
		oplus_protection_check_temp_region(chip);
		chg_info("wired_online, plugin_soc %d, batt temp normal %d\n",
			soc, chip->dcb_protect.is_temp_normal);
		if (chip->dcb_protect.is_temp_normal)
			chip->dcb_protect.intfcc_valid = true;
		else
			chip->dcb_protect.intfcc_valid = false;
	} else {
		/*wired offline, cancel all param*/
		chg_info("wired offline, cancel protection\n");
		chip->dcb_protect.plugin_soc = INVALID_SOC;
		chip->dcb_protect.batt_integral_fcc = 0;
		chip->dcb_protect.intfcc_checked = false;
		cancel_limit_cur(chip);
	}
	cancel_delayed_work_sync(&chip->dcb_protect_check_work);
	schedule_delayed_work(&chip->dcb_protect_check_work,
		msecs_to_jiffies(FIRST_WORK_INTERVAL));
}

static void oplus_dual_cells_protect_get_track_info(struct oplus_mms_gauge *chip,
	struct dual_cells_protect_track_info *info)
{
	int plugin_soc = 0;
	if (chip == NULL || info == NULL) {
		chg_err("chip or info is null, return\n");
		return;
	}

	plugin_soc = chip->dcb_protect.plugin_soc;

	info->reason = chip->dcb_protect.reason;
	info->batt_status = chip->dcb_protect.batt_health;
	info->batt_cc = chip->dcb_protect.batt_cc;
	info->batt_fcc = chip->dcb_protect.batt_fcc;
	info->soc = chip->dcb_protect.batt_soc;
	info->pre_soc = chip->dcb_protect.pre_batt_soc;
	info->c_soc = chip->dcb_protect.c_soc;
	info->pre_c_soc = chip->dcb_protect.pre_c_soc;
	info->plugin_soc = plugin_soc;
	if (plugin_soc > FCC_INTEGRAL_THR)
		info->batt_intfcc = 0;
	else
		info->batt_intfcc = chip->dcb_protect.batt_integral_fcc / (100 - plugin_soc);
	info->xvdd_occur = chip->dcb_protect.xvdd_occur;
	info->sn_change = chip->dcb_protect.sn_change_occur;
}

static int oplus_dual_cells_protect_pack_track_info(char *buf,
	struct dual_cells_protect_track_info *info)
{
	int index = 0;
	if (buf == NULL || info == NULL) {
		chg_err("buf or info is null, return\n");
		return -EINVAL;
	}

	index += scnprintf(buf + index, DUAL_CELLS_PROTECT_TRACK_INFO_LEN - index,
		"$$track_reason@@%s", get_protect_reason_str(info->reason));
	index += scnprintf(buf + index, DUAL_CELLS_PROTECT_TRACK_INFO_LEN - index,
		"$$batt_status@@%d", info->batt_status);
	index += scnprintf(buf + index, DUAL_CELLS_PROTECT_TRACK_INFO_LEN - index,
		"$$batt_cycle@@%d", info->batt_cc);
	index += scnprintf(buf + index, DUAL_CELLS_PROTECT_TRACK_INFO_LEN - index,
		"$$batt_fcc@@%d", info->batt_fcc);
	index += scnprintf(buf + index, DUAL_CELLS_PROTECT_TRACK_INFO_LEN - index,
		"$$soc@@%d", info->soc);
	index += scnprintf(buf + index, DUAL_CELLS_PROTECT_TRACK_INFO_LEN - index,
		"$$pre_soc@@%d", info->pre_soc);
	index += scnprintf(buf + index, DUAL_CELLS_PROTECT_TRACK_INFO_LEN - index,
		"$$c_soc@@%d", info->c_soc);
	index += scnprintf(buf + index, DUAL_CELLS_PROTECT_TRACK_INFO_LEN - index,
		"$$pre_c_soc@@%d", info->pre_c_soc);
	index += scnprintf(buf + index, DUAL_CELLS_PROTECT_TRACK_INFO_LEN - index,
		"$$plugin_soc@@%d", info->plugin_soc);
	index += scnprintf(buf + index, DUAL_CELLS_PROTECT_TRACK_INFO_LEN - index,
		"$$batt_intfcc@@%d", info->batt_intfcc);
	index += scnprintf(buf + index, DUAL_CELLS_PROTECT_TRACK_INFO_LEN - index,
		"$$xvdd_occur@@%d", info->xvdd_occur);
	index += scnprintf(buf + index, DUAL_CELLS_PROTECT_TRACK_INFO_LEN - index,
		"$$sn_change@@%d", info->sn_change);

	if (index > DUAL_CELLS_PROTECT_TRACK_INFO_LEN) {
		chg_err("track info exceeds length limit.");
		return -EINVAL;
	}

	return index;
}

void oplus_dcb_protect_track(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_mms_gauge *chip =
		container_of(dwork, struct oplus_mms_gauge, dcb_protect_track_work);

	struct dual_cells_protect_track_info info;
	char *buf = NULL;
	int len = 0;
	struct mms_msg *topic_msg;
	int rc = 0;

	if (!chip) {
		chg_err("chip is null, return\n");
		return;
	}

	buf = kzalloc(DUAL_CELLS_PROTECT_TRACK_INFO_LEN * sizeof(char), GFP_KERNEL);
	if (buf == NULL) {
		chg_err("buf alloc error.\n");
		return;
	}

	/* get protection info */
	oplus_dual_cells_protect_get_track_info(chip, &info);
	/* pack info */
	len = oplus_dual_cells_protect_pack_track_info(buf, &info);
	/* creat err msg and trigger */
	if (is_err_topic_available(chip)) {
		topic_msg =
			oplus_mms_alloc_str_msg(MSG_TYPE_ITEM, MSG_PRIO_HIGH, ERR_ITEM_IC,
						"[%s]-[%d]-[%d]:%s", "dual_cells_protect",
						OPLUS_IC_ERR_GAUGE, TRACK_GAGUE_ERR_BATT_CELLS_DAMAGE, buf);
		if (topic_msg == NULL) {
			chg_err("alloc topic msg error\n");
		} else {
			rc = oplus_mms_publish_msg_sync(chip->err_topic, topic_msg);
			if (rc < 0) {
				chg_err("publish topic msg error, rc=%d\n", rc);
				kfree(topic_msg);
			}
		}
	}
	kfree(buf);
}

static void oplus_chg_cycle_status_check(struct oplus_mms_gauge *chip)
{
	union mms_msg_data data = { 0 };

	oplus_mms_get_item_data(chip->comm_topic, COMM_ITEM_CHG_CYCLE_STATUS,
		&data, false);
	chip->dcb_protect.chg_cycle_status = data.intval;

	if (chip->dcb_protect.chg_cycle_status != chip->dcb_protect.pre_chg_cycle_status) {
		chip->dcb_protect.plugin_soc = chip->dcb_protect.batt_soc;
		chip->dcb_protect.pre_chg_cycle_status = chip->dcb_protect.chg_cycle_status;
		chip->dcb_protect.batt_integral_fcc = 0;
		chip->dcb_protect.intfcc_valid = true;
		chip->dcb_protect.intfcc_checked = false;
		chg_info("chg_cycle_status change, plugin_soc %d\n", chip->dcb_protect.plugin_soc);
	}
}


static void oplus_dcb_protect_get_common_info(struct oplus_mms_gauge *chip)
{
	union mms_msg_data data = { 0 };
	if (!chip) {
		chg_err("chip is null, return\n");
		return;
	}

	oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_CC, &data,
				false);
	chip->dcb_protect.batt_cc = data.intval;

	oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_FCC, &data,
				false);
	chip->dcb_protect.batt_fcc = data.intval;

	oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_SOC, &data,
				false);
	chip->dcb_protect.batt_soc = data.intval;

	oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_CURR, &data,
				false);
	chip->dcb_protect.ibat_ma = data.intval;

	oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_C_SOC, &data,
				false);
	chip->dcb_protect.c_soc = data.intval;

	oplus_mms_get_item_data(chip->comm_topic, COMM_ITEM_TEMP_REGION, &data,
				false);
	chip->dcb_protect.temp_region = data.intval;

	oplus_mms_get_item_data(chip->comm_topic, COMM_ITEM_RECHGING, &data,
				false);
	chip->dcb_protect.rechging = !!data.intval;

	oplus_chg_cycle_status_check(chip);
}

void oplus_dcb_protect_check(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_mms_gauge *chip =
		container_of(dwork, struct oplus_mms_gauge, dcb_protect_check_work);

	if (!chip->dcb_protect.dcb_support) {
		chg_err("dcb not support, return\n");
		return;
	}

	oplus_dcb_protect_get_common_info(chip);
	oplus_protection_check_temp_region(chip);
	oplus_protection_integral_fcc_cal(chip);

	/* batt is health, Check for damage */
	if (chip->dcb_protect.batt_health) {
		oplus_protection_soc_check(chip);
		oplus_protection_fcc_check(chip);
		oplus_protection_integral_fcc_check(chip);
	} else {
	/* batt is not health, Check whether it can be restored */
		oplus_protection_fcc_recover_check(chip);
		oplus_protection_int_fcc_recover_check(chip);
	}

	oplus_protection_limit_curr(chip);

	chg_info("batt_health %d, ibat %d, integral_fcc %d, "
		"batt fcc %d, batt_cc %d, batt_full %d, "
		"cur_soc %d, c_soc %d, reason %d ",
		chip->dcb_protect.batt_health, chip->dcb_protect.ibat_ma,
		chip->dcb_protect.batt_integral_fcc, chip->dcb_protect.batt_fcc,
		chip->dcb_protect.batt_cc, chip->batt_full,
		chip->dcb_protect.batt_soc, chip->dcb_protect.c_soc,
		chip->dcb_protect.reason);

	if (chip->wired_online)
		schedule_delayed_work(&chip->dcb_protect_check_work,
			msecs_to_jiffies(ACTIVE_WORK_INTERVAL));
	else
		schedule_delayed_work(&chip->dcb_protect_check_work,
			msecs_to_jiffies(INACTIVE_WORK_INTERVAL));
}

int oplus_gauge_get_dcb_protect_status(struct oplus_mms *topic,
				 int *status, int *reason)
{
	struct oplus_mms_gauge *chip;

	if (topic == NULL) {
		chg_err("topic is NULL\n");
		return -ENODEV;
	}
	chip = oplus_mms_get_drvdata(topic);

	if (!chip || !chip->dcb_protect.dcb_support)
		return -ENODEV;

	*status = chip->dcb_protect.batt_health;
	*reason = chip->dcb_protect.reason;
	return 0;
}

void oplus_gauge_set_dcb_protect_status(struct oplus_mms *topic, int val)
{
	struct oplus_mms_gauge *chip;
	int reason = 0;

	if (topic == NULL) {
		chg_err("mms is NULL");
		return;
	}

	chip = oplus_mms_get_drvdata(topic);
	if (!chip || !chip->dcb_protect.dcb_support)
		return;

	chip->dcb_protect.batt_health = !!(val & BATT_HEALTH_MASK);

	chip->dcb_protect.xvdd_occur = !!((val & XVDD_OCCUR_MASK) >> XVDD_OCCUR_SHIFT);
	if (chip->dcb_protect.xvdd_occur) {
		chip->dcb_protect.reason = XVDD_OCCUR;
		schedule_delayed_work(&chip->dcb_protect_track_work, 0);
		chg_info("xvdd occur, track\n");
		msleep(DUAL_CELLS_TRACK_INTERVAL_MS);
	}

	chip->dcb_protect.sn_change_occur = !!((val & PROTECT_REASON_MASK) >> PROTECT_REASON_SHIFT);
	if (chip->dcb_protect.sn_change_occur) {
		chip->dcb_protect.reason = SN_CHANGE;
		schedule_delayed_work(&chip->dcb_protect_track_work, 0);
		chg_info("sn change occur, track\n");
		msleep(DUAL_CELLS_TRACK_INTERVAL_MS);
	}

	reason = (val >> SN_CHANGED_SHIFT) & SN_CHANGED_MASK;
	if (reason >= PROTECT_UNKNOWN && reason < REASON_MAX)
		chip->dcb_protect.reason = reason;

	chg_info("batt_health and reason from hidl xvdd %d, sn %d, batt_health %d, reason %d\n",
		chip->dcb_protect.xvdd_occur, chip->dcb_protect.sn_change_occur,
		chip->dcb_protect.batt_health, chip->dcb_protect.reason);

	chip->dcb_protect.xvdd_occur = 0;
	chip->dcb_protect.sn_change_occur = 0;
}

static void parse_common_param(struct oplus_dcb_protect_spec *spec,
	struct device_node *dcb_node)
{
	int rc = 0;
	rc = of_property_read_u32(dcb_node, "oplus_spec,cur_limit", &spec->cur_limit);
	if (rc < 0) {
		chg_err("read cur_limit failed, use default, rc %d\n", rc);
		spec->cur_limit = DCB_PROTECT_CUR_LIMIT_DEFAULT;
	}

	rc = of_property_read_u32(dcb_node, "oplus_spec,cur-recover-thr", &spec->cur_recover_thr);
	if (rc < 0) {
		spec->cur_recover_thr = 200;
		chg_err("read cur-recover-thr failed, use default, rc=%d\n", rc);
	}
}

static int parse_param(struct oplus_mms_gauge *chip,
	struct device_node *dcb_node)
{
	int rc = 0;
	struct oplus_dcb_protect_spec *spec = &chip->dcb_protect.spec;

	chip->dcb_protect.fcc_check_support =
		of_property_read_bool(dcb_node, "oplus,fcc_check_support");
	chip->dcb_protect.fcc_track_support =
		of_property_read_bool(dcb_node, "oplus,fcc_track_support");
	if (chip->dcb_protect.fcc_check_support ||
		chip->dcb_protect.fcc_track_support) {
		rc = of_property_read_u32(dcb_node, "oplus_spec,min-fcc", &spec->min_fcc);
		if (rc < 0)
			return -EINVAL;
	}

	chip->dcb_protect.soc_check_support =
		of_property_read_bool(dcb_node, "oplus,soc_check_support");
	chip->dcb_protect.soc_track_support =
		of_property_read_bool(dcb_node, "oplus,soc_track_support");
	if (chip->dcb_protect.soc_check_support ||
		chip->dcb_protect.soc_track_support) {
		rc = of_property_read_u32(dcb_node, "oplus_spec,soc-jump-thr", &spec->soc_jump_thr);
		if (rc < 0)
			return -EINVAL;
	}

	chip->dcb_protect.intfcc_check_support =
		of_property_read_bool(dcb_node, "oplus,intfcc_check_support");
	chip->dcb_protect.intfcc_track_support =
		of_property_read_bool(dcb_node, "oplus,intfcc_track_support");
	if (chip->dcb_protect.intfcc_check_support ||
		chip->dcb_protect.intfcc_track_support) {
		rc = of_property_read_u32(dcb_node, "oplus_spec,min-oplus-fcc", &spec->min_oplus_fcc);
		if (rc < 0)
			return -EINVAL;
	}

	parse_common_param(spec, dcb_node);

	return 0;
}

static int dcb_protect_parse_param(struct oplus_mms_gauge *chip)
{
	struct device_node *node;
	struct device_node *dcb_node;
	int rc = 0;
	node = oplus_get_node_by_type(chip->dev->of_node);
	dcb_node = of_get_child_by_name(node, "oplus,dual_cells_protection");

	if (!dcb_node) {
		chg_err("Can not find dual_cells_protection node\n");
		dcb_node = of_find_compatible_node(NULL, NULL, "oplus,dual_cells_protection");
		if (!dcb_node) {
			chg_err("dual_cells_protection compatible node not found\n");
			return -ENODEV;
		}
	}

	rc = parse_param(chip, dcb_node);
	return rc;
}

void oplus_mms_dcb_protect_init(struct oplus_mms_gauge *chip)
{
	int rc = 0;
	if (!chip)
		return;

	chg_info("start\n");
	chip->dcb_protect.batt_health = true;
	chip->dcb_protect.pre_batt_health = true;
	chip->dcb_protect.temp_region = TEMP_REGION_NORMAL;
	chip->dcb_protect.dcb_support = false;

	rc = dcb_protect_parse_param(chip);
	if (!rc) {
		chg_err("config right, support dcb");
		chip->dcb_protect.dcb_support = true;
	}
	chg_info("end\n");

	return;
}
