// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025-2025 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "[UNISOC_GAUGE]([%s][%d]): " fmt, __func__, __LINE__

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/atomic.h>
#include <linux/power_supply.h>

#include <oplus_chg_ic.h>
#include <oplus_chg_module.h>
#include <oplus_chg_comm.h>
#include <oplus_mms_gauge.h>

struct chip_unisoc_gauge {
	struct device *dev;
	struct oplus_chg_ic_dev *ic_dev;

	struct power_supply *fg_psy;
	const char *fg_psy_name;

	atomic_t suspended;

	int batt_num;
};

#define UNISOC_GAUGE_SOC_0P1PCT_MIN		0
#define UNISOC_GAUGE_SOC_0P1PCT_MAX		1000
#define UNISOC_GAUGE_UA_PER_MA		1000
#define UNISOC_GAUGE_UV_PER_MV		1000
#define UNISOC_GAUGE_SOC_DECI_PER_PCT	10
#define UNISOC_GAUGE_SOH_PCT_MIN		0
#define UNISOC_GAUGE_SOH_PCT_MAX		100

static inline struct chip_unisoc_gauge *unisoc_gauge_get_chip(struct oplus_chg_ic_dev *ic_dev, bool need_fgu)
{
	struct chip_unisoc_gauge *chip;

	chip = ic_dev ? oplus_chg_ic_get_drvdata(ic_dev) : NULL;
	if (!chip)
		return NULL;

	if (need_fgu && !chip->fg_psy)
		return NULL;

	return chip;
}

static struct power_supply *unisoc_gauge_get_psy(struct chip_unisoc_gauge *chip)
{
	struct power_supply *psy;

	if (!chip->fg_psy_name)
		chip->fg_psy_name = "sc27xx-fgu";

	psy = power_supply_get_by_name(chip->fg_psy_name);
	if (!psy)
		chg_err("fgu power_supply %s not ready\n", chip->fg_psy_name);
	else
		chg_info("fgu power_supply %s found\n", chip->fg_psy_name);

	return psy;
}

static int oplus_unisoc_gauge_init(struct oplus_chg_ic_dev *ic_dev)
{
	struct chip_unisoc_gauge *chip;

	chip = unisoc_gauge_get_chip(ic_dev, false);
	if (!chip)
		return -ENODEV;

	chip->fg_psy = unisoc_gauge_get_psy(chip);
	if (!chip->fg_psy) {
		ic_dev->online = false;
		return -EAGAIN;
	}

	ic_dev->online = true;

	return 0;
}

static int oplus_unisoc_gauge_exit(struct oplus_chg_ic_dev *ic_dev)
{
	if (!ic_dev)
		return 0;

	if (!ic_dev->online)
		return 0;

	ic_dev->online = false;
	chg_info("%s, ic_dev->online = %d\n", __func__, ic_dev->online);

	return 0;
}

static int oplus_unisoc_gauge_reg_dump(struct oplus_chg_ic_dev *ic_dev)
{
	if (!ic_dev) {
		chg_err("ic_dev is NULL\n");
		return -ENODEV;
	}

	return 0;
}

static int oplus_unisoc_gauge_get_batt_vol(struct oplus_chg_ic_dev *ic_dev, int index, int *vol_mv)
{
	struct chip_unisoc_gauge *chip;
	union power_supply_propval val = { 0 };
	int rc;

	if (!vol_mv) {
		chg_err("vol_mv is NULL\n");
		return -EINVAL;
	}

	chip = unisoc_gauge_get_chip(ic_dev, true);
	if (!chip)
		return -ENODEV;

	rc = power_supply_get_property(chip->fg_psy, POWER_SUPPLY_PROP_VOLTAGE_NOW, &val);
	if (rc < 0)
		return rc;

	*vol_mv = (int)div_s64((s64)val.intval, UNISOC_GAUGE_UV_PER_MV);

	return 0;
}

static int oplus_unisoc_gauge_get_batt_max(struct oplus_chg_ic_dev *ic_dev, int *vol_mv)
{
	struct chip_unisoc_gauge *chip;
	union power_supply_propval val = { 0 };
	int rc;

	if (!vol_mv) {
		chg_err("vol_mv is NULL\n");
		return -EINVAL;
	}

	chip = unisoc_gauge_get_chip(ic_dev, true);
	if (!chip)
		return -ENODEV;

	rc = power_supply_get_property(chip->fg_psy, POWER_SUPPLY_PROP_VOLTAGE_NOW, &val);
	if (rc < 0)
		return rc;

	*vol_mv = (int)div_s64((s64)val.intval, UNISOC_GAUGE_UV_PER_MV);

	return 0;
}

static int oplus_unisoc_gauge_get_batt_min(struct oplus_chg_ic_dev *ic_dev, int *vol_mv)
{
	struct chip_unisoc_gauge *chip;
	union power_supply_propval val = { 0 };
	int rc;

	if (!vol_mv) {
		chg_err("vol_mv is NULL\n");
		return -EINVAL;
	}

	chip = unisoc_gauge_get_chip(ic_dev, true);
	if (!chip)
		return -ENODEV;

	rc = power_supply_get_property(chip->fg_psy, POWER_SUPPLY_PROP_VOLTAGE_NOW, &val);
	if (rc < 0)
		return rc;

	*vol_mv = (int)div_s64((s64)val.intval, UNISOC_GAUGE_UV_PER_MV);

	return 0;
}

static int oplus_unisoc_gauge_get_batt_curr(struct oplus_chg_ic_dev *ic_dev, int *curr_ma)
{
	struct chip_unisoc_gauge *chip;
	union power_supply_propval val = { 0 };
	int rc;

	if (!curr_ma) {
		chg_err("curr_ma is NULL\n");
		return -EINVAL;
	}

	chip = unisoc_gauge_get_chip(ic_dev, true);
	if (!chip)
		return -ENODEV;

	rc = power_supply_get_property(chip->fg_psy, POWER_SUPPLY_PROP_CURRENT_NOW, &val);
	if (rc < 0)
		return rc;

	*curr_ma = -(int)div_s64((s64)val.intval, UNISOC_GAUGE_UA_PER_MA);

	return 0;
}

static int oplus_unisoc_gauge_get_batt_temp(struct oplus_chg_ic_dev *ic_dev, int *temp)
{
	struct chip_unisoc_gauge *chip;
	union power_supply_propval val = { 0 };
	int rc;

	if (!temp) {
		chg_err("temp is NULL\n");
		return -EINVAL;
	}

	chip = unisoc_gauge_get_chip(ic_dev, true);
	if (!chip)
		return -ENODEV;

	rc = power_supply_get_property(chip->fg_psy, POWER_SUPPLY_PROP_TEMP, &val);
	if (rc < 0)
		return rc;

	*temp = val.intval;

	return 0;
}

static int oplus_unisoc_gauge_get_batt_soc(struct oplus_chg_ic_dev *ic_dev, int *soc)
{
	struct chip_unisoc_gauge *chip;
	union power_supply_propval val = { 0 };
	int rc;

	if (!soc) {
		chg_err("soc is NULL\n");
		return -EINVAL;
	}

	chip = unisoc_gauge_get_chip(ic_dev, true);
	if (!chip)
		return -ENODEV;

	rc = power_supply_get_property(chip->fg_psy, POWER_SUPPLY_PROP_CAPACITY, &val);
	if (rc < 0)
		return rc;

	*soc = (int)div_s64((s64)val.intval, UNISOC_GAUGE_SOC_DECI_PER_PCT);

	return 0;
}

static int oplus_unisoc_gauge_get_batt_fcc(struct oplus_chg_ic_dev *ic_dev, int *fcc)
{
	struct chip_unisoc_gauge *chip;
	union power_supply_propval val = { 0 };
	int rc;

	if (!fcc) {
		chg_err("fcc is NULL\n");
		return -EINVAL;
	}

	chip = unisoc_gauge_get_chip(ic_dev, true);
	if (!chip)
		return -ENODEV;

	rc = power_supply_get_property(chip->fg_psy, POWER_SUPPLY_PROP_CHARGE_FULL, &val);
	if (rc < 0)
		return rc;

	*fcc = (int)div_s64((s64)val.intval, UNISOC_GAUGE_UA_PER_MA);

	return 0;
}

static int oplus_unisoc_gauge_get_batt_cc(struct oplus_chg_ic_dev *ic_dev, int *cc)
{
	struct chip_unisoc_gauge *chip;
	union power_supply_propval val = { 0 };
	int rc;

	if (!cc) {
		chg_err("cc is NULL\n");
		return -EINVAL;
	}

	chip = unisoc_gauge_get_chip(ic_dev, true);
	if (!chip)
		return -ENODEV;

	rc = power_supply_get_property(chip->fg_psy, POWER_SUPPLY_PROP_CYCLE_COUNT, &val);
	if (rc < 0)
		return rc;

	*cc = val.intval;

	return 0;
}

static int oplus_unisoc_gauge_get_batt_rm(struct oplus_chg_ic_dev *ic_dev, int *rm)
{
	struct chip_unisoc_gauge *chip;
	union power_supply_propval soc_val = { 0 };
	union power_supply_propval fcc_val = { 0 };
	int rc;
	int soc;
	int fcc_mah;

	if (!rm) {
		chg_err("rm is NULL\n");
		return -EINVAL;
	}

	chip = unisoc_gauge_get_chip(ic_dev, true);
	if (!chip)
		return -ENODEV;

	rc = power_supply_get_property(chip->fg_psy, POWER_SUPPLY_PROP_CAPACITY, &soc_val);
	if (rc < 0)
		return rc;

	rc = power_supply_get_property(chip->fg_psy, POWER_SUPPLY_PROP_CHARGE_FULL, &fcc_val);
	if (rc < 0)
		return rc;

	soc = soc_val.intval;
	soc = max_t(int, soc, UNISOC_GAUGE_SOC_0P1PCT_MIN);
	soc = min_t(int, soc, UNISOC_GAUGE_SOC_0P1PCT_MAX);

	fcc_mah = (int)div_s64((s64)fcc_val.intval, UNISOC_GAUGE_UA_PER_MA);
	*rm = (int)div_s64((s64)fcc_mah * soc, UNISOC_GAUGE_SOC_0P1PCT_MAX);

	return 0;
}

static int oplus_unisoc_gauge_get_batt_soh(struct oplus_chg_ic_dev *ic_dev, int *soh)
{
	struct chip_unisoc_gauge *chip;
	union power_supply_propval design = { 0 };
	union power_supply_propval full = { 0 };
	int rc;
	int soh_calc = 100;

	if (!soh) {
		chg_err("soh is NULL\n");
		return -EINVAL;
	}

	chip = unisoc_gauge_get_chip(ic_dev, true);
	if (!chip)
		goto out_default;

	rc = power_supply_get_property(chip->fg_psy, POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN, &design);
	if (rc < 0 || design.intval <= 0)
		goto out_default;

	rc = power_supply_get_property(chip->fg_psy, POWER_SUPPLY_PROP_CHARGE_FULL, &full);
	if (rc < 0 || full.intval <= 0)
		goto out_default;

	soh_calc = (int)div_s64((s64)full.intval * UNISOC_GAUGE_SOH_PCT_MAX, design.intval);
	soh_calc = max_t(int, soh_calc, UNISOC_GAUGE_SOH_PCT_MIN);
	soh_calc = min_t(int, soh_calc, UNISOC_GAUGE_SOH_PCT_MAX);

	*soh = soh_calc;
	return 0;

out_default:
	*soh = UNISOC_GAUGE_SOH_PCT_MAX;
	return 0;
}

static int oplus_unisoc_gauge_get_batt_auth(struct oplus_chg_ic_dev *ic_dev, bool *pass)
{
	if (!pass) {
		chg_err("pass is NULL\n");
		return -EINVAL;
	}

	*pass = true;

	return 0;
}

static int oplus_unisoc_gauge_get_batt_hmac(struct oplus_chg_ic_dev *ic_dev, bool *pass)
{
	if (!pass) {
		chg_err("pass is NULL\n");
		return -EINVAL;
	}

	*pass = true;

	return 0;
}

static int oplus_unisoc_gauge_set_batt_full(struct oplus_chg_ic_dev *ic_dev, bool full)
{
	if (!ic_dev) {
		chg_err("ic_dev is NULL\n");
		return -ENODEV;
	}

	chg_info("set batt full = %d\n", full);

	return 0;
}

static int oplus_unisoc_gauge_get_batt_num(struct oplus_chg_ic_dev *ic_dev, int *num)
{
	struct chip_unisoc_gauge *chip;

	if (!num) {
		chg_err("num is NULL\n");
		return -EINVAL;
	}

	chip = unisoc_gauge_get_chip(ic_dev, false);
	if (!chip)
		return -ENODEV;

	*num = chip->batt_num;

	return 0;
}

static int oplus_unisoc_gauge_get_qmax(struct oplus_chg_ic_dev *ic_dev, int batt_id, int *qmax)
{
	struct chip_unisoc_gauge *chip;
	union power_supply_propval full = { 0 };
	int rc;

	if (!qmax) {
		chg_err("qmax is NULL\n");
		return -EINVAL;
	}

	chip = unisoc_gauge_get_chip(ic_dev, true);
	if (!chip) {
		return -ENODEV;
	}

	rc = power_supply_get_property(chip->fg_psy, POWER_SUPPLY_PROP_CHARGE_FULL, &full);
	if (rc < 0)
		return rc;

	*qmax = (int)div_s64((s64)full.intval, UNISOC_GAUGE_UA_PER_MA);
	return 0;
}

static int oplus_unisoc_gauge_get_batt_exist(struct oplus_chg_ic_dev *ic_dev, bool *exist)
{
	if (!exist) {
		chg_err("exist is NULL\n");
		return -EINVAL;
	}

	*exist = true;

	return 0;
}

static int oplus_unisoc_get_dec_fg_type(struct oplus_chg_ic_dev *ic_dev, int *fg_type)
{
	if (!fg_type) {
		chg_err("fg_type is NULL\n");
		return -EINVAL;
	}

	*fg_type = DEC_CV_PACK_UNKNOWN;

	return 0;
}

static int oplus_unisoc_get_dec_cv_soh(struct oplus_chg_ic_dev *ic_dev, int *dec_soh)
{
	struct chip_unisoc_gauge *chip;
	union power_supply_propval val = { 0 };
	int rc;

	if (!dec_soh) {
		chg_err("dec_soh is NULL\n");
		return -EINVAL;
	}

	chip = unisoc_gauge_get_chip(ic_dev, true);
	if (!chip)
		return -ENODEV;

	rc = power_supply_get_property(chip->fg_psy, POWER_SUPPLY_PROP_CYCLE_COUNT, &val);
	if (rc < 0)
		return rc;

	*dec_soh = val.intval;
	return 0;
}

static void *oplus_unisoc_chg_get_func(struct oplus_chg_ic_dev *ic_dev, enum oplus_chg_ic_func func_id)
{
	void *func = NULL;

	if (!ic_dev) {
		chg_err("ic_dev is NULL\n");
		return NULL;
	}

	if (!ic_dev->online && (func_id != OPLUS_IC_FUNC_INIT) && (func_id != OPLUS_IC_FUNC_EXIT)) {
		chg_err("%s is offline\n", ic_dev->name);
		return NULL;
	}

	switch (func_id) {
	case OPLUS_IC_FUNC_INIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_INIT,
					       oplus_unisoc_gauge_init);
		break;
	case OPLUS_IC_FUNC_EXIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_EXIT,
					       oplus_unisoc_gauge_exit);
		break;
	case OPLUS_IC_FUNC_REG_DUMP:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_REG_DUMP,
					       oplus_unisoc_gauge_reg_dump);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_VOL:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_VOL,
					       oplus_unisoc_gauge_get_batt_vol);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_MAX:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_MAX,
					       oplus_unisoc_gauge_get_batt_max);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_MIN:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_MIN,
					       oplus_unisoc_gauge_get_batt_min);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_CURR:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_CURR,
					       oplus_unisoc_gauge_get_batt_curr);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_TEMP:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_TEMP,
					       oplus_unisoc_gauge_get_batt_temp);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_SOC:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_SOC,
					       oplus_unisoc_gauge_get_batt_soc);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_FCC:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_FCC,
					       oplus_unisoc_gauge_get_batt_fcc);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_CC:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_CC,
					       oplus_unisoc_gauge_get_batt_cc);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_RM:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_RM,
					       oplus_unisoc_gauge_get_batt_rm);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_SOH:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_SOH,
					       oplus_unisoc_gauge_get_batt_soh);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_AUTH:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_AUTH,
					       oplus_unisoc_gauge_get_batt_auth);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_HMAC:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_HMAC,
					       oplus_unisoc_gauge_get_batt_hmac);
		break;
	case OPLUS_IC_FUNC_GAUGE_SET_BATT_FULL:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_SET_BATT_FULL,
					       oplus_unisoc_gauge_set_batt_full);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_NUM:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_NUM,
					       oplus_unisoc_gauge_get_batt_num);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_QMAX:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_QMAX,
					       oplus_unisoc_gauge_get_qmax);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_EXIST:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_EXIST,
					       oplus_unisoc_gauge_get_batt_exist);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_DEC_FG_TYPE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_DEC_FG_TYPE,
					       oplus_unisoc_get_dec_fg_type);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_DEC_CV_SOH:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_DEC_CV_SOH,
					       oplus_unisoc_get_dec_cv_soh);
		break;
	default:
		chg_err("this func_id(%d) is not supported\n", func_id);
		func = NULL;
		break;
	}

	return func;
}

static struct oplus_chg_ic_virq unisoc_gauge_virq_table[] = {
	{.virq_id = OPLUS_IC_VIRQ_ERR },
	{.virq_id = OPLUS_IC_VIRQ_ONLINE },
	{.virq_id = OPLUS_IC_VIRQ_OFFLINE },
	{.virq_id = OPLUS_IC_VIRQ_RESUME },
};

static void oplus_unisoc_gauge_parse_dt(struct chip_unisoc_gauge *chip)
{
	int rc;

	atomic_set(&chip->suspended, 0);

	rc = of_property_read_u32(chip->dev->of_node, "oplus,batt_num",
				  &chip->batt_num);
	if (rc < 0) {
		chg_err("can't get oplus,batt_num, rc=%d\n", rc);
		chip->batt_num = 1;
	}

	rc = of_property_read_string(chip->dev->of_node,
				     "oplus,fg_psy_name",
				     &chip->fg_psy_name);
	if (rc < 0 || !chip->fg_psy_name) {
		chip->fg_psy_name = "sc27xx-fgu";
		chg_info("use default fg_psy_name: %s\n", chip->fg_psy_name);
	} else {
		chg_info("fg_psy_name from dts: %s\n", chip->fg_psy_name);
	}
}

static int unisoc_gauge_driver_probe(struct platform_device *pdev)
{
	struct chip_unisoc_gauge *chip;
	enum oplus_chg_ic_type ic_type;
	int ic_index;
	struct oplus_chg_ic_cfg ic_cfg = { 0 };
	int rc;

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip) {
		dev_err(&pdev->dev, "failed to allocate device info data\n");
		return -ENOMEM;
	}

	chip->dev = &pdev->dev;
	platform_set_drvdata(pdev, chip);

	oplus_unisoc_gauge_parse_dt(chip);

	rc = of_property_read_u32(chip->dev->of_node, "oplus,ic_type", &ic_type);
	if (rc < 0) {
		chg_err("can't get ic type, rc=%d\n", rc);
		return rc;
	}

	rc = of_property_read_u32(chip->dev->of_node, "oplus,ic_index", &ic_index);
	if (rc < 0) {
		chg_err("can't get ic index, rc=%d\n", rc);
		return rc;
	}

	ic_cfg.name = chip->dev->of_node->name;
	ic_cfg.index = ic_index;

	snprintf(ic_cfg.manu_name, OPLUS_CHG_IC_MANU_NAME_MAX - 1,
		 "gauge-unisoc:%d", ic_index);

	ic_cfg.type = ic_type;
	ic_cfg.get_func = oplus_unisoc_chg_get_func;
	ic_cfg.virq_data = unisoc_gauge_virq_table;
	ic_cfg.virq_num = ARRAY_SIZE(unisoc_gauge_virq_table);
	ic_cfg.of_node = chip->dev->of_node;

	chip->ic_dev = devm_oplus_chg_ic_register(chip->dev, &ic_cfg);
	if (!chip->ic_dev) {
		chg_err("register %s error\n", chip->dev->of_node->name);
		return -ENODEV;
	}

	chg_info("register %s success\n", chip->dev->of_node->name);

	oplus_unisoc_gauge_init(chip->ic_dev);

	return 0;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0))
static void unisoc_gauge_driver_remove(struct platform_device *pdev)
#else
static int unisoc_gauge_driver_remove(struct platform_device *pdev)
#endif
{
	struct chip_unisoc_gauge *chip = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);
	devm_kfree(&pdev->dev, chip);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0))
	return 0;
#endif
}

static int unisoc_gauge_pm_suspend(struct device *dev)
{
	struct chip_unisoc_gauge *chip = dev_get_drvdata(dev);

	if (!chip)
		return 0;

	atomic_set(&chip->suspended, 1);

	return 0;
}

static int unisoc_gauge_pm_resume(struct device *dev)
{
	struct chip_unisoc_gauge *chip = dev_get_drvdata(dev);

	if (!chip)
		return 0;

	atomic_set(&chip->suspended, 0);
	oplus_chg_ic_virq_trigger(chip->ic_dev, OPLUS_IC_VIRQ_RESUME);

	return 0;
}

static const struct dev_pm_ops unisoc_gauge_pm_ops = {
	.resume = unisoc_gauge_pm_resume,
	.suspend = unisoc_gauge_pm_suspend,
};

static const struct of_device_id unisoc_gauge_match[] = {
	{.compatible = "oplus,hal_unisoc_gauge" },
	{},
};

static struct platform_driver unisoc_gauge_driver = {
	.driver = {
		.name = "oplus_unisoc_gauge",
		.of_match_table = unisoc_gauge_match,
		.pm = &unisoc_gauge_pm_ops,
	},
	.probe = unisoc_gauge_driver_probe,
	.remove = unisoc_gauge_driver_remove,
};

static __init int oplus_unisoc_gauge_driver_init(void)
{
	int rc;

	rc = platform_driver_register(&unisoc_gauge_driver);
	if (rc < 0)
		chg_err("failed to register unisoc gauge driver, rc=%d\n", rc);

	return rc;
}

static __exit void oplus_unisoc_gauge_driver_exit(void)
{
	platform_driver_unregister(&unisoc_gauge_driver);
}

oplus_chg_module_register(oplus_unisoc_gauge_driver);

MODULE_DESCRIPTION("Driver for Unisoc platform gauge");
MODULE_LICENSE("GPL v2");
