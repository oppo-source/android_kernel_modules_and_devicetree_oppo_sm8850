// Copyright 2023 YourCompany. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//
// This file contains the implementation of the dual cells batt protection.
// Author: oplus
// Created: 2025-06-10
// Notes: This file is part of the dual cells batt protection

#ifndef __OPLUS_CHG_DUAL_CELLS_PROTECTION_H__
#define __OPLUS_CHG_DUAL_CELLS_PROTECTION_H__
#include <oplus_mms.h>

#define DCB_PROTECT_CUR_LIMIT_DEFAULT 2500
#define ERROR_COUNT_LIMIT  3
#define FCC_INTEGRAL_THR   20

#define INVALID_SOC (-1)
#define INVALID_FCC (-1)
#define INVALID_CC  (-1)

#define C_SOC_UNIT   100
#define INT_FCC_UNIT 100
#define DUAL_CELLS_PROTECT_TRACK_INFO_LEN   200
#define BATT_FULL_IBAT_MA   (-10)
#define DUAL_CELLS_TRACK_INTERVAL_MS 1000

#define BATT_HEALTH_MASK       0x1
#define XVDD_OCCUR_MASK        0x4
#define XVDD_OCCUR_SHIFT       2
#define PROTECT_REASON_MASK    0x2
#define PROTECT_REASON_SHIFT   1
#define SN_CHANGED_MASK        0xf
#define SN_CHANGED_SHIFT       3

#define ACTIVE_WORK_INTERVAL     5000
#define INACTIVE_WORK_INTERVAL   10000
#define FIRST_WORK_INTERVAL      100

struct dual_cells_protect_track_info {
	int reason;
	int batt_status;
	int batt_cc;
	int batt_fcc;
	int soc;
	int pre_soc;
	int c_soc;
	int pre_c_soc;
	int plugin_soc;
	int batt_intfcc;
	int xvdd_occur;
	int sn_change;
};

enum dual_cells_protect_reason {
	PROTECT_UNKNOWN,
	FCC_SMALL,
	INTFCC_SMALL,
	SOC_JUMP,
	FCC_RECOVER,
	FCC_RECOVERING,
	INTFCC_RECOVER,
	SN_CHANGE,
	XVDD_OCCUR,
	REASON_MAX,
};

struct oplus_dcb_protect_spec {
	int min_fcc;
	int soc_jump_thr;
	int min_oplus_fcc;
	int cur_recover_thr;
	int cur_limit;
};

struct oplus_dual_cells_protection {
	struct votable *vooc_curr_votable;
	struct votable *ufcs_curr_votable;
	struct votable *pps_curr_votable;
	struct votable *wls_fcc_votable;

	int batt_cc;
	int batt_fcc;
	int batt_integral_fcc;
	bool intfcc_valid;
	bool intfcc_checked;
	int pre_batt_soc;
	int batt_soc;
	int c_soc;
	int pre_c_soc;
	int ibat_ma;
	int plugin_soc;
	bool batt_health;
	bool pre_batt_health;
	bool is_temp_normal;
	int temp_region;
	bool ufcs_charging;
	bool vooc_charging;
	bool pps_charging;
	bool wls_fastchg_charging;
	bool fcc_check_support;
	bool intfcc_check_support;
	bool soc_check_support;
	bool fcc_track_support;
	bool intfcc_track_support;
	bool soc_track_support;
	int reason;
	bool rechging;
	bool xvdd_occur;
	bool sn_change_occur;
	int chg_cycle_status;
	int pre_chg_cycle_status;

	bool dcb_support;

	struct oplus_dcb_protect_spec spec;
};

void oplus_dcb_protect_check_start(struct oplus_mms_gauge *chip);
void oplus_mms_dcb_protect_init(struct oplus_mms_gauge *chip);
void oplus_dcb_protect_check(struct work_struct *work);
void oplus_dcb_protect_track(struct work_struct *work);

const char *get_protect_reason_str(enum dual_cells_protect_reason type);
#endif /* __OPLUS_CHG_DUAL_CELLS_PROTECTION_H__ */
