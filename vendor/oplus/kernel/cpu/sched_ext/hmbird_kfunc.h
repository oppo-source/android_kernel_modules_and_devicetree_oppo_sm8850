/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Oplus. All rights reserved.
 */
#ifndef _HMBIRD_KFUNC_H_
#define _HMBIRD_KFUNC_H_

void hmbird_kfunc_register(void);
void pre_hmbird_kfunc_register(void);

extern atomic_t __hb_ops_enabled;

bool sched_hook_executed(int cpu);
bool hb_bpf_is_ux(struct task_struct *p);
bool hb_bpf_is_vip_mvp(struct task_struct *p);

#endif /* _HMBIRD_KFUNC_H_ */
