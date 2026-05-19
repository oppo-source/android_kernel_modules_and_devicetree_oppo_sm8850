/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Oplus. All rights reserved.
 *
 * oplus_sc83107_bcl.h - Dynamic BCL configuration for SC83107
 */

#ifndef _OPLUS_SC83107_BCL_H_
#define _OPLUS_SC83107_BCL_H_

#include <linux/device.h>
#include <linux/proc_fs.h>

struct sc83107_chip;

/* Dynamic BCL configuration data structure */
struct sc83107_dynamic_bcl_data {
	int temp;		/* Temperature threshold (0.1°C unit) */
	int lv0_mv;		/* LV0 threshold voltage (mV) */
	int lv1_mv;		/* LV1 threshold voltage (mV) */
	int lv2_mv;		/* LV2 threshold voltage (mV) */
};

/* BCL compensation configuration data structure */
struct sc83107_dynamic_bcl_compensation {
	int temp;		/* Temperature threshold (0.1°C unit) */
	int compensation_200_500;	/* Compensation for cycle count [200, 500) in mV */
	int compensation_500_1000;	/* Compensation for cycle count [500, 1000) in mV */
	int compensation_over_1000;	/* Compensation for cycle count [1000, ∞) in mV */
};

#define SC83107_BCL_CYCLE_THRESH_LOW	200
#define SC83107_BCL_CYCLE_THRESH_MID	500
#define SC83107_BCL_CYCLE_THRESH_HIGH	1000

/* Function declarations */
void sc83107_dynamic_bcl_init(struct sc83107_chip *chip, struct device *dev);
void sc83107_dynamic_bcl_cleanup(struct sc83107_chip *chip);

#endif /* _OPLUS_SC83107_BCL_H_ */
