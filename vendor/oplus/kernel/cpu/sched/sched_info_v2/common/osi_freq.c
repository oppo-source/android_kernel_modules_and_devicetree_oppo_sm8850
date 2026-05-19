// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022, 2025 Oplus. All rights reserved.
 */

#include "osi_topology.h"
#include "osi_freq.h"
#include "../osi_base.h"
#include "../osi_api.h"


static inline int cpufreq_table_find_index(struct cpufreq_policy *policy,
					     unsigned int target_freq)
{
	if (policy->freq_table_sorted == CPUFREQ_TABLE_SORTED_ASCENDING)
		return cpufreq_table_find_index_al(policy, target_freq, 1);
	else
		return cpufreq_table_find_index_dl(policy, target_freq, 1);
}

void cpufreq_info_get(bool *is_sample)
{
	int cls;
	int start_cpu;
	int cur_idx, max_idx, min_idx, orig_max_id, orig_min_id;
	struct cpufreq_policy *pol;

	for (cls = 0; cls < cluster_num; cls++) {
		start_cpu = cluster[cls].start_cpu;
		pol = cpufreq_cpu_get(start_cpu);
		if (likely(pol)) {
			if (!pol->freq_table) {
				cpufreq_cpu_put(pol);
				return;
			}
			cur_idx = cpufreq_table_find_index(pol, pol->cur);
			min_idx = cpufreq_table_find_index(pol, pol->min);
			max_idx = cpufreq_table_find_index(pol, pol->max);
			orig_max_id = cpufreq_table_find_index(pol, pol->cpuinfo.max_freq);
			orig_min_id = cpufreq_table_find_index(pol, pol->cpuinfo.min_freq);
			osi_debug("cpu:%d,pol->cur:%d,idx:%d min_idx:%d,max_idx:%d,orig_min_idx:%d,orig_max_idx:%d",
				start_cpu, pol->cur, cur_idx, min_idx, max_idx, orig_min_id, orig_max_id);
			if (pol->freq_table_sorted == CPUFREQ_TABLE_SORTED_DESCENDING) {
				if ((max_idx <= orig_min_id - 4) && (cur_idx <= max_idx + 2))
					*is_sample = true;
			} else {
				if ((max_idx >= orig_min_id + 4) && (cur_idx >= max_idx - 2))
					*is_sample = true;
			}
			cpufreq_cpu_put(pol);
		}
	}
}

static __maybe_unused int osi_freq_policy_notifier_callback(struct notifier_block *nb,
						unsigned long val, void *data)
{
	struct cpufreq_policy *policy = (struct cpufreq_policy *)data;

	if (IS_ERR_OR_NULL(policy)) {
		pr_err("%s:null cpu policy\n", __func__);
		return NOTIFY_DONE;
	}
	if (val != CPUFREQ_CREATE_POLICY)
		return NOTIFY_DONE;
	return NOTIFY_OK;
}

static __maybe_unused struct notifier_block osi_freq_cpufreq_policy_notifier = {
	.notifier_call = osi_freq_policy_notifier_callback,
};
