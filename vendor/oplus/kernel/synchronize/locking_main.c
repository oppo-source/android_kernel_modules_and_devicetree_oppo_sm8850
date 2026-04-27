// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2022 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "oplus_locking_strategy:" fmt

#include <linux/sched.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/sa_common.h>

#include "locking_main.h"

#define FEATURE_ENABLE 0xFFFFFFFF

unsigned int g_opt_enable;
unsigned int g_opt_debug;
EXPORT_SYMBOL(g_opt_enable);
unsigned int dynamic_switch;

void lock_ux_state_systrace(struct task_struct *from, struct task_struct *waiter,
	struct task_struct *holder, int ux_state, int systrace_lvl)
{
	bool lvl0_enable = false;
	bool lvl1_enable = false;

	if (g_opt_debug & LOG_LOCK_SYSTRACE_LVL0) {
		lvl0_enable = true;
	}
	if (g_opt_debug & LOG_LOCK_SYSTRACE_LVL1) {
		lvl1_enable = true;
	}
	if (!lvl0_enable && !lvl1_enable) {
		return;
	} else if ((systrace_lvl == LOG_LOCK_SYSTRACE_LVL1) && !lvl1_enable) {
		return;
	} else {
		char buf[128] = {0};
		int from_pid = 0;
		int waiter_pid = 0;
		int holder_pid = 0;
		if (from) {
			from_pid = from->pid;
		}
		if (waiter) {
			waiter_pid = waiter->pid;
		}
		if (holder) {
			holder_pid = holder->pid;
		}

		snprintf(buf, sizeof(buf), "C|9999|y_lock_from|%d\n", from_pid);
		tracing_mark_write(buf);

		memset(buf, 0, sizeof(buf));
		snprintf(buf, sizeof(buf), "C|9999|y_lock_waiter|%d\n", waiter_pid);
		tracing_mark_write(buf);

		memset(buf, 0, sizeof(buf));
		snprintf(buf, sizeof(buf), "C|9999|y_lock_holder|%d\n", holder_pid);
		tracing_mark_write(buf);

		memset(buf, 0, sizeof(buf));
		snprintf(buf, sizeof(buf), "C|9999|y_lock_ux_state|%d\n", ux_state);
		tracing_mark_write(buf);
	}
}

void parse_dts_switch(void)
{
	struct device_node *np = NULL;
	int ret;

	np = of_find_node_by_name(NULL, "oplus_sync_ipc");

	if(np) {
		ret = of_property_read_u32(np, "disable", &dynamic_switch);
		if(ret) {
			pr_err("no oplus_sync_ipc disable!");
		} else {
			pr_info("oplus_sync_ipc : %d", dynamic_switch);
			return;
		}
	}

	pr_err("no oplus_sync_ipc node!");
	dynamic_switch = FEATURE_ENABLE;
}

static int __init locking_opt_init(void)
{
	int ret = 0;

	parse_dts_switch();

	g_opt_enable |= LK_MUTEX_ENABLE;
	g_opt_enable |= LK_RWSEM_ENABLE;
	g_opt_enable |= LK_FUTEX_ENABLE;
#ifdef CONFIG_OPLUS_LOCKING_OSQ
	g_opt_enable |= LK_OSQ_ENABLE;
#endif

#ifdef CONFIG_LOCKING_PROTECT
	g_opt_enable |= LK_PROTECT_ENABLE;
	g_opt_enable |= LK_SCHED_ENABLE;
	g_opt_enable &= dynamic_switch;
	sched_assist_locking_init();
#endif
	lk_sysfs_init();
	register_rwsem_vendor_hooks();
	register_mutex_vendor_hooks();
	register_futex_vendor_hooks();
#ifdef CONFIG_OPLUS_LOCKING_MONITOR
	g_opt_enable |= LK_MONITOR_ENABLE;
	kern_lstat_init();
#endif
	krn_reliab_init();
	pr_info("g_opt_enable : %d\n", g_opt_enable);
	return ret;
}

static void __exit locking_opt_exit(void)
{
	g_opt_enable = 0;

	unregister_rwsem_vendor_hooks();
	unregister_mutex_vendor_hooks();
	unregister_futex_vendor_hooks();
	lk_sysfs_exit();
#ifdef CONFIG_OPLUS_LOCKING_MONITOR
	kern_lstat_exit();
#endif
}

module_init(locking_opt_init);
module_exit(locking_opt_exit);
module_param_named(locking_enable, g_opt_enable, uint, 0660);
module_param_named(locking_debug, g_opt_debug, uint, 0660);
MODULE_DESCRIPTION("Oplus Locking Strategy Vender Hooks Driver");
MODULE_LICENSE("GPL v2");
