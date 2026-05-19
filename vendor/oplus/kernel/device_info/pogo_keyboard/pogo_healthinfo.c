// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/err.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/ktime.h>
#include <linux/list.h>
#include <linux/uaccess.h>
#include <linux/seq_file.h>
#include <asm/stack_pointer.h>
#include <asm/current.h>
#include <linux/version.h>

#include "pogo_healthinfo.h"
#include "pogo_keyboard.h"

int update_value_count_list(struct list_head *list, void *value)
{
	struct list_head *pos = NULL;
	struct health_value_count *vc = NULL;
	char *value_str = (char *)value;
	size_t value_len;

	if (!list || !value) {
		kb_err("%s: invalid parameters\\n", __func__);
		return -EINVAL;
	}
	value_len = strlen(value_str);

	list_for_each(pos, list) {
		vc = list_entry(pos, struct health_value_count, head);
		if (!vc->value) {
			kb_err("Invalid NULL value in health list\n");
			continue;
		}
		if (!strncmp((char *)vc->value, value_str, value_len)) {
			vc->count++;
			kb_debug("%s str=%s, count=%d\n", __func__, (char *)vc->value, vc->count);
			return vc->count;
		}
	}

	vc = kzalloc(sizeof(struct health_value_count), GFP_KERNEL);
	if (!vc) {
		kb_err("kzalloc failed.\n");
		return -ENOMEM;
	}

	vc->value = kzalloc(sizeof(char) * (value_len + 1), GFP_KERNEL);
	if (!vc->value) {
		kb_err("vc->value kzalloc failed.\n");
		kfree(vc);
		return -ENOMEM;
	}

	strncpy((char *)vc->value, value_str, value_len + 1);
	((char *)vc->value)[value_len] = 0;
	vc->count = 1;
	list_add_tail(&vc->head, list);
	kb_debug("%s str=%s, count=%d\n", __func__, (char *)vc->value, vc->count);
	return vc->count;
}

int clear_value_count_list(struct list_head *list)
{
	struct list_head *pos = NULL;
	struct health_value_count *vc = NULL;

	while (!list_empty(list)) {
		pos = list->next;
		list_del(pos);
		vc = list_entry(pos, struct health_value_count, head);
		kfree(vc->value);
		kfree(vc);
	}
	WARN_ON(!list_empty(list));
	kb_info("list cleared success.\n");
	return 0;
}

int print_value_count_list(char *buf, size_t size, struct list_head *list, char *prefix)
{
	int cnt = 0;
	struct list_head *pos = NULL;
	struct health_value_count *vc = NULL;

	list_for_each(pos, list) {
		vc = list_entry(pos, struct health_value_count, head);

		cnt += scnprintf(buf + cnt, size - cnt, "%s%s:%d\n", prefix ? prefix : "", (char *)vc->value, vc->count);

		kb_debug("%s%s:%d\n", prefix ? prefix : "", (char *)vc->value, vc->count);
	}

	return cnt;
}

int pogo_healthinfo_report(struct monitor_data *monitor_data, char *report)
{
	int ret = 0;

	if (!monitor_data) {
		return 0;
	}

	ret = update_value_count_list(&monitor_data->health_report_list, report);

	return ret;
}

int pogo_healthinfo_read(char __user *buf, size_t size, struct monitor_data *monitor_data)
{
	char *info = NULL;

	if (!monitor_data) {
		return 0;
	}

	if (!buf) {
		kb_err("health buf is Null.\n");
		return -EINVAL;
	}

	if (size <= 0) {
		kb_err("health buf size invalid.\n");
		return -EINVAL;
	}
    if (size > MAX_HEALTH_INFO_SIZE) {
        kb_err("health buf size too large: %zu\\n", size);
        return -EINVAL;
    }
	info = kzalloc(size, GFP_KERNEL);
	if (!info) {
		kb_err("health info alloc failed.\n");
		return -ENOMEM;
	}

	/*debug info*/
	print_value_count_list(info, size, &monitor_data->health_report_list, PREFIX_HEALTH_REPORT);

	if (copy_to_user(buf, info, size)) {
		kb_err("copy to user error");
		kfree(info);
		return -EFAULT;
	}
	kfree(info);

	return 0;
}

int pogo_healthinfo_read_seq(struct seq_file *s, struct monitor_data *monitor_data)
{
	int cnt = 0;
	struct list_head *pos = NULL;
	struct health_value_count *vc = NULL;

	if (!monitor_data) {
		return 0;
	}

	list_for_each(pos, &monitor_data->health_report_list) {
		vc = list_entry(pos, struct health_value_count, head);
		seq_printf(s, "%s%s:%d\n", PREFIX_HEALTH_REPORT, (char *)vc->value, vc->count);
		kb_debug("%s%s:%d\n", PREFIX_HEALTH_REPORT, (char *)vc->value, vc->count);
	}

	return cnt;
}

int pogo_healthinfo_clear(struct monitor_data *monitor_data)
{
	if (!monitor_data) {
		return 0;
	}

	kb_info("Clear health info Now!\n");

	/*debug info*/
	clear_value_count_list(&monitor_data->health_report_list);

	kb_info("Clear health info Finish!\n");

	return 0;
}

int pogo_healthinfo_init(struct monitor_data *monitor_data)
{
	if (!monitor_data) {
		kb_info("monitor_data is NULL.\n");
		return -1;
	}

	INIT_LIST_HEAD(&monitor_data->health_report_list);
	return 0;
}
