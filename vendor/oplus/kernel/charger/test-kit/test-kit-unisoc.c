// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026-2026 Oplus. All rights reserved.
 * Unisoc (Spreadtrum) GPIO check implementation
 */

#define pr_fmt(fmt) "[TEST-KIT-UNISOC]([%s][%d]): " fmt, __func__, __LINE__

#include <linux/module.h>
#include "test-kit.h"

#if IS_ENABLED(CONFIG_OPLUS_CHARGER_UNISOC)
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/limits.h>
#include <linux/string.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/stdarg.h>
#include "drivers/pinctrl/sprd/pinctrl-sprd-oplus.h"
#include "drivers/pinctrl/core.h"

static const char *const direction_str[] = { "in", "out" };
static const char *const level_str[] = { "low", "high" };
static const char *const pull_str[] = { "no pull", "pull down", "pull up" };
static const unsigned long drive_map[] = {
	2, 4, 6, 8, 10, 12, 14, 16, 20, 21, 24, 25, 27, 29, 31, 33
};

#define CHECK_TYPE_DIRECTION "direction"
#define CHECK_TYPE_LEVEL "level"
#define CHECK_TYPE_PULL "pull"
#define CHECK_TYPE_FUNC "func"
#define CHECK_TYPE_DRIVE "drive"

struct unisoc_check_ctx {
	struct test_kit_soc_gpio_info *gpio_info;
	struct pinctrl_dev *pctldev;
	struct gpio_desc *gdesc;
	unsigned int pin_common;
	unsigned int pin_misc;
	char *buf;
	size_t len;
	size_t *use_size;
	bool pass;
};

static unsigned int unisoc_find_pin_id(struct pinctrl_dev *pctldev, const char *pin_name)
{
	unsigned int i;
	const struct pinctrl_pin_desc *pins;

	if (!pctldev || !pin_name || !pctldev->desc) {
		pr_err("pctldev, pin name or desc is NULL\n");
		return INT_MAX;
	}

	pins = pctldev->desc->pins;
	if (!pins) {
		pr_err("pins array is NULL\n");
		return INT_MAX;
	}

	for (i = 0; i < pctldev->desc->npins; i++) {
		if (pins[i].name && !strcmp(pins[i].name, pin_name))
			return pins[i].number;
	}

	pr_err("pin name '%s' not found\n", pin_name);
	return INT_MAX;
}

static int unisoc_get_config(struct pinctrl_dev *pctldev, unsigned pin,
			      unsigned long param, unsigned long *value)
{
	unsigned long config = pinconf_to_config_packed(param, 0);
	const struct pinconf_ops *ops;
	int ret;

	if (!pctldev || !pctldev->desc) {
		return -EINVAL;
	}

	ops = pctldev->desc->confops;
	if (!ops || !ops->pin_config_get) {
		return -ENOTSUPP;
	}

	ret = ops->pin_config_get(pctldev, pin, &config);
	if (ret < 0)
		return ret;

	if (pinconf_to_config_param(config) == param) {
		*value = pinconf_to_config_argument(config);
		return 0;
	}

	return -EINVAL;
}

static int unisoc_buf_append(struct unisoc_check_ctx *ctx, bool is_error,
			     const char *fmt, ...)
{
	va_list args;
	size_t used, left;
	int prefix_len = 0, body_len = 0;

	if (!ctx || !ctx->buf || !ctx->use_size || !ctx->gpio_info || ctx->len == 0)
		return 0;

	used = *ctx->use_size;
	if (used >= ctx->len)
		used = ctx->len - 1;

	left = ctx->len - used;
	prefix_len = scnprintf(ctx->buf + used, left,
			       "[%s][gpio%u]",
			       ctx->gpio_info->name ? ctx->gpio_info->name : "null",
			       ctx->gpio_info->num);
	used = min_t(size_t, used + prefix_len, ctx->len - 1);

	left = ctx->len - used;
	if (left > 1) {
		va_start(args, fmt);
		body_len = vscnprintf(ctx->buf + used, left, fmt, args);
		va_end(args);
		used = min_t(size_t, used + body_len, ctx->len - 1);
	}
	*ctx->use_size = used;

	if (is_error)
		ctx->pass = false;

	return prefix_len + body_len;
}

static void unisoc_report_error(struct unisoc_check_ctx *ctx, const char *type,
				const char *expected, const char *actual)
{
	unisoc_buf_append(ctx, true, "[%s error]:expected:%s, actually:%s\n",
			  type, expected, actual);
}

static void unisoc_report_get_failed(struct unisoc_check_ctx *ctx, const char *type, int err)
{
	unisoc_buf_append(ctx, true, "[%s error]:failed to get config, err=%d\n",
			  type, err);
}

static void unisoc_check_direction(struct unisoc_check_ctx *ctx)
{
	unsigned long value;
	int ret;

	ret = unisoc_get_config(ctx->pctldev, ctx->pin_misc, PIN_CONFIG_OUTPUT_ENABLE, &value);
	if (ret != 0) {
		unisoc_report_get_failed(ctx, CHECK_TYPE_DIRECTION, ret);
		return;
	}

	if (!!value == ctx->gpio_info->is_out)
		return;

	unisoc_report_error(ctx, CHECK_TYPE_DIRECTION,
			   direction_str[ctx->gpio_info->is_out],
			   direction_str[!!value]);
}

static void unisoc_check_level(struct unisoc_check_ctx *ctx)
{
	bool is_high;
	int val;
	val = gpiod_get_value_cansleep(ctx->gdesc);
	if (val < 0) {
		unisoc_buf_append(ctx, true, "[%s error]:failed to get value, err=%d\n",
				  CHECK_TYPE_LEVEL, val);
		return;
	}
	is_high = !!val;

	if (is_high == ctx->gpio_info->is_high)
		return;

	unisoc_report_error(ctx, CHECK_TYPE_LEVEL,
			   level_str[ctx->gpio_info->is_high],
			   level_str[is_high]);
}

static void unisoc_check_pull(struct unisoc_check_ctx *ctx)
{
	unsigned long actual_pull = 0, value;
	const char *expected_str, *actual_str;
	int ret_up, ret_down;

	if (ctx->gpio_info->is_out || ctx->gpio_info->pull < 0)
		return;

	ret_up = unisoc_get_config(ctx->pctldev, ctx->pin_misc, PIN_CONFIG_BIAS_PULL_UP, &value);
	if (ret_up == 0 && value) {
		actual_pull = 2;
	} else {
		ret_down = unisoc_get_config(ctx->pctldev, ctx->pin_misc, PIN_CONFIG_BIAS_PULL_DOWN, &value);
		if (ret_down == 0 && value) {
			actual_pull = 1;
		} else if (ret_up != 0 && ret_down != 0) {
			unisoc_report_get_failed(ctx, CHECK_TYPE_PULL, ret_up);
			return;
		}
	}

	if (actual_pull == ctx->gpio_info->pull)
		return;

	if (ctx->gpio_info->pull < ARRAY_SIZE(pull_str))
		expected_str = pull_str[ctx->gpio_info->pull];
	else
		expected_str = "unknown";
	if (actual_pull < ARRAY_SIZE(pull_str))
		actual_str = pull_str[actual_pull];
	else
		actual_str = "unknown";
	unisoc_report_error(ctx, CHECK_TYPE_PULL, expected_str, actual_str);
}

static void unisoc_check_func(struct unisoc_check_ctx *ctx)
{
	unsigned long func_value;
	char expected_str[16], actual_str[16];
	int ret;

	if (ctx->gpio_info->func < 0)
		return;

	ret = unisoc_get_config(ctx->pctldev, ctx->pin_common, 0, &func_value);
	if (ret != 0) {
		unisoc_report_get_failed(ctx, CHECK_TYPE_FUNC, ret);
		return;
	}

	if (func_value == ctx->gpio_info->func)
		return;

	scnprintf(expected_str, sizeof(expected_str), "%d", ctx->gpio_info->func);
	scnprintf(actual_str, sizeof(actual_str), "%lu", func_value);
	unisoc_report_error(ctx, CHECK_TYPE_FUNC, expected_str, actual_str);
}

static void unisoc_check_drive(struct unisoc_check_ctx *ctx)
{
	unsigned long reg_val, drive_ma;
	char expected_str[16], actual_str[16];
	int ret;

	if (ctx->gpio_info->drive <= 0)
		return;

	ret = unisoc_get_config(ctx->pctldev, ctx->pin_misc, PIN_CONFIG_DRIVE_STRENGTH, &reg_val);
	if (ret != 0) {
		unisoc_report_get_failed(ctx, CHECK_TYPE_DRIVE, ret);
		return;
	}

	if (reg_val >= ARRAY_SIZE(drive_map)) {
		unisoc_buf_append(ctx, true, "[%s error]:invalid drive reg_val=%lu\n",
				  CHECK_TYPE_DRIVE, reg_val);
		return;
	}

	drive_ma = drive_map[reg_val];
	if (drive_ma == ctx->gpio_info->drive)
		return;

	scnprintf(expected_str, sizeof(expected_str), "%dmA", ctx->gpio_info->drive);
	scnprintf(actual_str, sizeof(actual_str), "%lumA", drive_ma);
	unisoc_report_error(ctx, CHECK_TYPE_DRIVE, expected_str, actual_str);
}

bool test_kit_unisoc_gpio_check(void *info, char *buf, size_t len, size_t *use_size)
{
	struct test_kit_soc_gpio_info *gpio_info = info;
	struct pinctrl_dev *pctldev = NULL;
	struct gpio_desc *gdesc = NULL;
	struct unisoc_check_ctx ctx;

	if (!info || !buf) {
		pr_err("invalid parameter\n");
		return false;
	}

	*use_size = 0;

	memset(&ctx, 0, sizeof(ctx));
	ctx.gpio_info = gpio_info;
	ctx.buf = buf;
	ctx.len = len;
	ctx.use_size = use_size;
	ctx.pass = true;

	if (!gpio_info->chip) {
		unisoc_buf_append(&ctx, true, ":gpio chip is NULL\n");
		return false;
	}

	gdesc = gpio_to_desc(gpio_info->chip->base + gpio_info->num);
	if (!gdesc) {
		unisoc_buf_append(&ctx, true, ":failed to get gpio desc\n");
		return false;
	}

	pctldev = oplus_get_sprd_pctldev();
	if (!pctldev) {
		unisoc_buf_append(&ctx, true, ":failed to get pctldev\n");
		return false;
	}

	ctx.pctldev = pctldev;
	ctx.gdesc = gdesc;
	ctx.pin_common = unisoc_find_pin_id(pctldev, gpio_info->pin_comm_name);
	ctx.pin_misc = unisoc_find_pin_id(pctldev, gpio_info->pin_misc_name);

	if (ctx.pin_common == INT_MAX) {
		unisoc_buf_append(&ctx, true, ":pin_comm_name '%s' not found\n",
				  gpio_info->pin_comm_name);
		return false;
	}
	if (ctx.pin_misc == INT_MAX) {
		unisoc_buf_append(&ctx, true, ":pin_misc_name '%s' not found\n",
				  gpio_info->pin_misc_name);
		return false;
	}

	unisoc_check_direction(&ctx);
	unisoc_check_level(&ctx);
	unisoc_check_pull(&ctx);
	unisoc_check_func(&ctx);
	unisoc_check_drive(&ctx);
	return ctx.pass;
}
EXPORT_SYMBOL(test_kit_unisoc_gpio_check);

#else /* !CONFIG_OPLUS_CHARGER_UNISOC */

bool test_kit_unisoc_gpio_check(void *info, char *buf, size_t len, size_t *use_size)
{
	return false;
}
EXPORT_SYMBOL(test_kit_unisoc_gpio_check);

#endif /* CONFIG_OPLUS_CHARGER_UNISOC */
