// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include "pogo_exception.h"

#include <linux/err.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/delay.h>
#include <linux/ktime.h>
#include <asm/current.h>
#include <linux/version.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>

#include "pogo_keyboard.h"
#include "pogo_healthinfo.h"

#if IS_ENABLED(CONFIG_OPLUS_FEATURE_LOG_CORE)

#include <soc/oplus/system/olc.h>

#define POGO_MODULE_NAME         "pogo_keyboard"
#define POGO_LOG_PATH            "kernel/pogo_debug_info"
#define POGO_MODULE_ID           0x34
#define POGO_RESERVED_ID         256

static int pogo_olc_raise_exception(pogo_excep_type excep_tpye, void *summary, unsigned int summary_size)
{
	struct exception_info *exp_info = NULL;
	int ret = -1;

	kb_info("%s:enter,type:%d\n", __func__ , excep_tpye);
    if (excep_tpye > 0xfff) {
        kb_err("%s: excep_tpye:%d is beyond 0xfff\n", __func__ , excep_tpye);
        return -EINVAL;
    }
	exp_info = kmalloc(sizeof(struct exception_info), GFP_KERNEL);

	if (!exp_info) {
		POGO_HEALTH_REPORT(POGO_HEALTH_REPORT_ALLOC_FAIL);
		return -ENOMEM;
	}

	exp_info->time = 0;
	exp_info->id = (POGO_RESERVED_ID << 20) | (POGO_MODULE_ID << 12) | excep_tpye;
	exp_info->pid = 0;
	exp_info->exceptionType = EXCEPTION_KERNEL;
	exp_info->faultLevel = 0;
	exp_info->logOption = LOG_KERNEL | LOG_MAIN;
	memcpy(exp_info->module, POGO_MODULE_NAME, sizeof(POGO_MODULE_NAME));

	memcpy(exp_info->logPath, POGO_LOG_PATH, sizeof(POGO_LOG_PATH));

	memcpy(exp_info->summary, summary, summary_size);

	ret = olc_raise_exception(exp_info);
	if (ret) {
		kb_err("%s: raise fail, ret:%d\n", __func__ , ret);
	}

	kfree(exp_info);
	return ret;
}

#elif IS_ENABLED(CONFIG_OPLUS_FEATURE_OLC)
#include <soc/oplus/dft/olc.h>

#define POGO_LOG_PATH            "kernel/pogo_debug_info"
#define POGO_MODULE_ID           1
#define POGO_RESERVED_ID         256

static int pogo_olc_raise_exception(pogo_excep_type excep_tpye, void *summary, unsigned int summary_size)
{
	struct exception_info *exp_info = NULL;
	int ret = -1;
	struct timespec64 time;

	kb_info("%s:enter,type:%d\n", __func__ , excep_tpye);
    if (excep_tpye > 0xfff) {
        kb_err("%s: excep_tpye:%d is beyond 0xfff\n", __func__ , excep_tpye);
        return -EINVAL;
    }
	exp_info = kmalloc(sizeof(struct exception_info), GFP_KERNEL);

	if (!exp_info) {
		POGO_HEALTH_REPORT(POGO_HEALTH_REPORT_ALLOC_FAIL);
		return -ENOMEM;
	}

	ktime_get_ts64(&time);
	exp_info->time = time.tv_sec;
	exp_info->exceptionId = (POGO_RESERVED_ID << 20) | (POGO_MODULE_ID << 12) | excep_tpye;
	exp_info->exceptionType = EXCEPTION_KERNEL;
	exp_info->level = 0;
	exp_info->atomicLogs = LOG_KERNEL | LOG_MAIN | LOG_SYSTRACE;

	memcpy(exp_info->logParams, POGO_LOG_PATH, sizeof(POGO_LOG_PATH));

	ret = olc_raise_exception(exp_info);
	if (ret) {
		kb_err("%s: raise fail, ret:%d\n", __func__ , ret);
	}

	kfree(exp_info);
	return ret;
}

#else
static  int pogo_olc_raise_exception(pogo_excep_type excep_tpye, void *summary, unsigned int summary_size)
{
	return 0;
}
#endif /* CONFIG_OPLUS_KEVENT_UPLOAD_DELETE */

int pogo_exception_report(void *tp_exception_data, pogo_excep_type excep_tpye, void *summary, unsigned int summary_size)
{
	int ret = -1;

	struct exception_data *exception_data = (struct exception_data *)tp_exception_data;

	if (!exception_data) {
		return 0;
	}
	switch (excep_tpye) {
	case EXCEP_BUS:
		/*bus error and upload  times*/
		exception_data->bus_error_count++;
		if (exception_data->bus_error_count > MAX_BUS_ERROR_COUNT
				&& exception_data->bus_error_upload_count < MAX_BUS_UPDATE_COUNT) {
			ret = pogo_olc_raise_exception(excep_tpye, summary, summary_size);
			exception_data->bus_error_count = 0;
			exception_data->bus_error_upload_count++;
		}
		break;
	case EXCEP_BUS_READY:
		/*upload times*/
		if (exception_data->bus_not_ready_upload_count < MAX_BUS_NOT_READY_UPDATE_COUNT) {
			ret = pogo_olc_raise_exception(excep_tpye, summary, summary_size);
			exception_data->bus_not_ready_upload_count++;
		}
		break;
	default:
		ret = pogo_olc_raise_exception(excep_tpye, summary, summary_size);
		break;
	}

	return ret;
}
EXPORT_SYMBOL(pogo_exception_report);
