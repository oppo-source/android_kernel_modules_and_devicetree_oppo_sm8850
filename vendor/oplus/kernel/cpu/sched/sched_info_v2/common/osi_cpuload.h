
/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Oplus. All rights reserved.
 */

#ifndef _OSI_CPULOAD_H_
#define _OSI_CPULOAD_H_

#include <linux/sched.h>
#include <linux/tick.h>
#include "osi_topology.h"
#include "../osi_base.h"
#include "../osi_api.h"

struct cputime_info {
	u32 period;
	u32 cpu_load;
	u64 io_wait_time;
	u64 busy_time;
	u64 total_time;
	u64 time_stamp;
};

u64 get_idle_time(struct kernel_cpustat *kcs, s32 cpu);
u64 get_iowait_time(struct kernel_cpustat *kcs, s32 cpu);
struct proc_dir_entry *cpuload_proc_init(struct proc_dir_entry *pde);
void cpuload_proc_deinit(struct proc_dir_entry *pde);
void cpuload_init(void);
void cpuload_exit(void);

#endif/* _OSI_CPULOAD_H_ */
