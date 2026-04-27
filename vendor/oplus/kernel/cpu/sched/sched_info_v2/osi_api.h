/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Oplus. All rights reserved.
 */
#ifndef _OSI_API_H_
#define _OSI_API_H_

#define DEFAULT_CPU_LM_PERIOD 1000 /* ms */

#include "osi_base.h"

s32 cpuload_get_by_time(u32 time_in_ms, unsigned long cpumask_bits);
s32 cpuload_get_by_window(u32 win_cnt, unsigned long cpumask_bits);
s32 cpu_lm_peroid_set(u32 time_in_ms);
u32 cpu_nums_get(void);
void cpufreq_info_get(bool *is_sample);
bool timestamp_is_valid(u64 timestamp, u64 now);
#endif /* _OSI_API_H_ */
