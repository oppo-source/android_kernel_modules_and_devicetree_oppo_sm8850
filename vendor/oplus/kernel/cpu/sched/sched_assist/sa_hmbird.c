// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Oplus. All rights reserved.
 */

#include <linux/sched.h>
#include <kernel/sched/sched.h>
#include "sa_hmbird.h"

int (*hmbird_update_ux_state_cb)(struct task_struct *p, int ux_state);

void hmbird_update_ux_state_cb_init(
		int (*func)(struct task_struct *p, int ux_state))
{
	hmbird_update_ux_state_cb = func;
}
EXPORT_SYMBOL_GPL(hmbird_update_ux_state_cb_init);

void hmbird_update_ux_state_locked(struct task_struct *p, int ux_state)
{
	if (!hmbird_update_ux_state_cb)
		return;
	hmbird_update_ux_state_cb(p, ux_state);
}
#ifdef CONFIG_HMBIRD_SCHED_BPF
bool is_hmbird_enabled(void)
{
	return scx_enabled();
}
#else
bool is_hmbird_enabled(void) {return false;}
#endif

