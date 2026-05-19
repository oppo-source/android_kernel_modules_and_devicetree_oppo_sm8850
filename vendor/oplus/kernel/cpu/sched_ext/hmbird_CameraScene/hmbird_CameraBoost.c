/** Copyright (C), 2019-2024, OPLUS Mobile Comm Corp., Ltd.
* Description: Hmbird camera freq boost
* Author: Gao ZhiFeng 80407967
* Create: 2025-11-17
* Notes: Hmbird camera freq boost
*/
#include <linux/kernel.h>
#include <linux/cpufreq.h>
#include <linux/cpu.h>
#include <linux/slab.h>
#include <linux/errno.h>

#include "hmbird_CameraBoost.h"
#include "../hmbird_II/hmbird_II_freqgov.h"

static inline int cpufreq_find_index_l(struct cpufreq_policy *policy, unsigned int target_freq, int relation)
{
	bool efficiencies = policy->efficiencies_available && (relation & CPUFREQ_RELATION_E);

	if (policy->freq_table_sorted == CPUFREQ_TABLE_SORTED_ASCENDING)
		return cpufreq_table_find_index_al(policy, target_freq, efficiencies);
	else
		return cpufreq_table_find_index_dl(policy, target_freq, efficiencies);
}

static inline int cpufreq_find_index_h(struct cpufreq_policy *policy, unsigned int target_freq, int relation)
{
	bool efficiencies = policy->efficiencies_available && (relation & CPUFREQ_RELATION_E);

	if (policy->freq_table_sorted == CPUFREQ_TABLE_SORTED_ASCENDING)
		return cpufreq_table_find_index_ah(policy, target_freq, efficiencies);
	else
		return cpufreq_table_find_index_dh(policy, target_freq, efficiencies);
}

static struct cached_freq_idx cached_freq_idx[MAX_NR_CPUS];

static void cache_freq_idx(struct cpufreq_policy *policy, int cpu)
{
	cached_freq_idx[cpu].idx_min = cpufreq_find_index_l(policy, policy->cpuinfo.min_freq, CPUFREQ_RELATION_L);
	cached_freq_idx[cpu].idx_max = cpufreq_find_index_h(policy, policy->cpuinfo.max_freq, CPUFREQ_RELATION_H);
	cached_freq_idx[cpu].idx_cached = true;
}

static int get_order_freq_from_table(struct cpufreq_policy *policy,
				int order, unsigned int *freq_out, int cpu)
{
	struct cpufreq_frequency_table *table;
	int idx;
	int cur_freq;

	if (!policy || !freq_out)
		return -EINVAL;

	if (!cached_freq_idx[cpu].idx_cached)
		cache_freq_idx(policy, cpu);

	table = policy->freq_table;
	if (!table)
		return -EINVAL;

	cur_freq = policy->cur;
	idx = cpufreq_frequency_table_target(policy, cur_freq, CPUFREQ_RELATION_L);

	if (idx < 0)
		return -EINVAL;

	idx = idx + order;
	if (idx < cached_freq_idx[cpu].idx_min)
		idx = cached_freq_idx[cpu].idx_min;
	if (idx > cached_freq_idx[cpu].idx_max)
		idx = cached_freq_idx[cpu].idx_max;

	*freq_out = table[idx].frequency;
	return 0;
}

void boost_soft_min_freq(int order)
{
	unsigned int cpu = 0;
	struct cpufreq_policy *policy = NULL;
	struct hmbird_gov_policy *hg_policy = NULL;
	struct hmbird_gov_tunables *tunables = NULL;
	unsigned int new_soft_freq_min = -1;

	for_each_possible_cpu(cpu) {
		policy = cpufreq_cpu_get_raw(cpu);
		if (!policy)
			continue;

		if (cpu != cpumask_first(policy->related_cpus))
			continue;

		if (order) {
			if (get_order_freq_from_table(policy, order,
						      &new_soft_freq_min, cpu))
				continue;
		}

		rcu_read_lock();
		hg_policy = rcu_dereference(policy->governor_data);
		if (!hg_policy) {
			goto unlock;
		}

		if (!is_hmbird_gov_running(topology_cluster_id(cpu))) {
			goto unlock;
		}

		tunables = hg_policy->tunables;

		if (tunables->soft_freq_min_kern == new_soft_freq_min) {
			goto unlock;
		}

		tunables->soft_freq_min_kern = new_soft_freq_min;
		hmbird_gov_update_soft_limit_cpufreq(hg_policy);
		update_softlimit_systrace_c(hg_policy);

unlock:
		rcu_read_unlock();
	}
}

