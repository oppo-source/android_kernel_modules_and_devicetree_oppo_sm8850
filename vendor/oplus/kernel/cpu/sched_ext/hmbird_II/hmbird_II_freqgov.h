/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Oplus. All rights reserved.
 */
#ifndef _HMBIRD_II_FREQGOV_H_
#define _HMBIRD_II_FREQGOV_H_

#include <linux/cpufreq.h>
#include <uapi/linux/sched/types.h>
#include "hmbird_II_bpf.h"
#include "hmbird_II.h"

#define MAX_MTL_STAGES		(1 << 5)
#define MAX_PERF_VAL		1024

struct hmbird_gov_tunables {
	struct gov_attr_set     attr_set;
	unsigned int            target_loads;
	int                     soft_freq_max;
	int                     soft_freq_min;
	int                     soft_freq_max_kern;
	int                     soft_freq_min_kern;
	bool                    apply_freq_immediately;
	struct {
		int enable;
		int cnt;
		struct {
			unsigned int stage;
			unsigned int tl;
		} tls[MAX_MTL_STAGES];
		u16 tls_tab[MAX_PERF_VAL + 1];
	} mul_tl;
};

struct hmbird_gov_policy {
	struct cpufreq_policy	*policy;

	struct hmbird_gov_tunables	*tunables;
	struct list_head	tunables_hook;

	raw_spinlock_t		update_lock;	/* For shared policies */
	unsigned int		next_freq;
	unsigned int		next_raw_freq;
	/* The next fields are only needed if fast switch cannot be used: */
	struct kthread_work	work;
	struct mutex		work_lock;
	struct kthread_worker	worker;
	struct task_struct	*thread;
	bool			work_in_progress;
	unsigned int	target_load;
	bool		backup_efficiencies_available;
};

int hmbird_freqgov_init(void);
void gov_switch_state_systrace_c(void);
unsigned int get_tl_from_perf(unsigned int cpu, u32 perf);
void hmbird_gov_update_soft_limit_cpufreq(struct hmbird_gov_policy *hg_policy);
bool is_hmbird_gov_running(unsigned int cluster_id);
void update_softlimit_systrace_c(struct hmbird_gov_policy *hg_policy);

struct heavy_boost_params{
	int type;
	int bottom_perf;
	int boost_weight;
};

#endif /* _HMBIRD_II_FREQGOV_H_ */
