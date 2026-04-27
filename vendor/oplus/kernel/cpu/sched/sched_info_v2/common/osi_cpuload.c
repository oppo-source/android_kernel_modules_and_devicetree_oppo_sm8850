/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Oplus. All rights reserved.
 */

#include "osi_cpuload.h"
#include <linux/tick.h>
#include <linux/kernel_stat.h>

#define TICK_IN_MS  (1000/HZ)

static s32 cputime_win_idx = -1;
static DEFINE_RWLOCK(cputime_stat_lock);
static struct cputime_info **cputime_stat;
static s32 __read_mostly cpu_nums;
/* cpu_loadmonitor_period, default: 1000ms */
static s32 cpu_lm_period = DEFAULT_CPU_LM_PERIOD;
static struct delayed_work cpu_lm_work;

/* At present, since only the freeze module modifies this parameter,
 * no concurrent processing is implemented.
*/
s32 cpu_lm_peroid_set(u32 time_in_ms)
{
	if (time_in_ms <= TICK_IN_MS || time_in_ms > DEFAULT_CPU_LM_PERIOD)
		return -EINVAL;

	cpu_lm_period = time_in_ms;
	return 0;
}

s32 cpuload_get_by_time(u32 time_in_ms, unsigned long cpumask_bits)
{
	u32 cpu, idx;
	u64 total_load = 0;
	u32 valid_cpus = 0;
	struct cputime_info *info;

	read_lock(&cputime_stat_lock);
	if (!cputime_stat || time_in_ms <= 0) {
		read_unlock(&cputime_stat_lock);
		return -EINVAL;
	}

	for_each_possible_cpu(cpu) {
		if (cpu >= cpu_nums || ((1 << cpu) & cpumask_bits) == 0 || !cputime_stat[cpu])
			continue;

		u64 cpu_total = 0;
		u32 valid_samples = 0;
		u64 accumulated_time = 0;

		for (u32 i = 0; i < JANK_WIN_CNT; i++) {
			idx =  winidx_sub(cputime_win_idx, i);
			info = &cputime_stat[cpu][idx];

			if (info->time_stamp == 0 || info->total_time == 0)
				continue;

			accumulated_time += info->period;
			cpu_total += info->cpu_load;
			valid_samples++;

			if (accumulated_time >= time_in_ms)
				break;
		}

		if (valid_samples > 0) {
			total_load += cpu_total / valid_samples;
			valid_cpus++;
		}
	}
	read_unlock(&cputime_stat_lock);

	return valid_cpus > 0 ? (s32)(total_load / valid_cpus) : -ENODATA;
}

s32 cpuload_get_by_window(u32 win_cnt, unsigned long cpumask_bits)
{
	u32 cpu, idx;
	u64 total_load = 0;
	u32 valid_cpus = 0;
	struct cputime_info *info;

	read_lock(&cputime_stat_lock);
	if (!cputime_stat || win_cnt == 0 || win_cnt > JANK_WIN_CNT) {
		read_unlock(&cputime_stat_lock);
		return -EINVAL;
	}

	for_each_possible_cpu(cpu) {
		if (cpu >= cpu_nums || ((1 << cpu) & cpumask_bits) == 0)
			continue;

		u64 cpu_total = 0;
		u32 valid_samples = 0;

		for (u32 i = 0; i < win_cnt; i++) {
			idx = winidx_sub(cputime_win_idx, i);
			info = &cputime_stat[cpu][idx];

			if (info->time_stamp == 0 || info->total_time == 0)
				continue;

			cpu_total += info->cpu_load;
			valid_samples++;
		}

		if (valid_samples > 0) {
			total_load += cpu_total / valid_samples;
			valid_cpus++;
		}
	}
	read_unlock(&cputime_stat_lock);

	return valid_cpus > 0 ? (s32)(total_load / valid_cpus) : -ENODATA;
}

static s32 cputime_stat_init(void)
{
	cpu_nums = cpu_nums_get();

	if (cpu_nums <= 0) {
		pr_err("Invalid number of CPUs: %d\n", cpu_nums);
		return -EINVAL;
	}

	cputime_stat = kcalloc(cpu_nums, sizeof(struct cputime_info *), GFP_KERNEL);
	if (!cputime_stat) {
		pr_err("Failed to allocate memory for cputime_stat\n");
		return -ENOMEM;
	}

	for (s32 i = 0; i < cpu_nums; i++) {
		cputime_stat[i] = kcalloc(JANK_WIN_CNT, sizeof(struct cputime_info), GFP_KERNEL);
		if (!cputime_stat[i]) {
			while (i-- > 0)
				kfree(cputime_stat[i]);
			kfree(cputime_stat);
			cputime_stat = NULL;
			return -ENOMEM;
		}
	}
	return 0;
}

static void cputime_stat_exit(void)
{
	cpu_nums = num_possible_cpus();

	if (!cputime_stat)
		return;

	for (s32 i = 0; i < cpu_nums; i++)
		kfree(cputime_stat[i]);

	kfree(cputime_stat);
	cputime_stat = NULL;
}

static s32 proc_cpu_lm_period_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", cpu_lm_period);
	return 0;
}

static s32 proc_cpu_lm_period_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_cpu_lm_period_show, inode);
}

static s32 proc_cpuload_show(struct seq_file *m, void *v)
{
	s32 cpu, idx;
	struct cputime_info *info;

	if (!cputime_stat) {
		seq_puts(m, "CPU load monitoring not initialized\n");
		return 0;
	}

	idx = cputime_win_idx;
	seq_printf(m, "CPU load statistics (window size: %dms):\n", cpu_lm_period);

	seq_puts(m, "CPU\tWindow\tLoad(%)\tIO Wait(%)\tTimestamp\n");

	read_lock(&cputime_stat_lock);
	for_each_possible_cpu(cpu) {
		if (cpu >= cpu_nums)
			continue;
		if (idx < 0)
			continue;
		info = &cputime_stat[cpu][idx];
		if (info->total_time == 0)
			continue;
		seq_printf(m, "%d\t%d\t%d\t%llu\t%llu\n", cpu, idx, info->cpu_load,
			(info->io_wait_time * 100) / info->total_time,
			info->time_stamp);
	}
	read_unlock(&cputime_stat_lock);
	return 0;
}

static s32 proc_cpuload_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_cpuload_show, inode);
}
static const struct proc_ops proc_cpu_lm_period_ops = {
	.proc_open	=	proc_cpu_lm_period_open,
	.proc_read	=	seq_read,
	.proc_lseek	=	seq_lseek,
	.proc_release	=	single_release,
};

static const struct proc_ops proc_cpuload_ops = {
	.proc_open	=	proc_cpuload_open,
	.proc_read	=	seq_read,
	.proc_lseek	=	seq_lseek,
	.proc_release	=	single_release,
};

u64 get_idle_time(struct kernel_cpustat *kcs, s32 cpu)
{
	u64 idle, idle_usecs = -1ULL;

	if (cpu_online(cpu))
		idle_usecs = get_cpu_idle_time_us(cpu, NULL);

	if (idle_usecs == -1ULL)
		/* !NO_HZ or cpu offline so we can rely on cpustat.idle */
		idle = kcs->cpustat[CPUTIME_IDLE];
	else
		idle = idle_usecs * NSEC_PER_USEC;

	return idle;
}

u64 get_iowait_time(struct kernel_cpustat *kcs, s32 cpu)
{
	u64 iowait, iowait_usecs = -1ULL;

	if (cpu_online(cpu))
		iowait_usecs = get_cpu_iowait_time_us(cpu, NULL);

	if (iowait_usecs == -1ULL)
		/* !NO_HZ or cpu offline so we can rely on cpustat.iowait */
		iowait = kcs->cpustat[CPUTIME_IOWAIT];
	else
		iowait = iowait_usecs * NSEC_PER_USEC;

	return iowait;
}

static void cpu_lm_work_func(struct work_struct *work)
{
	s32 cpu;
	struct kernel_cpustat kcs;
	u64 busy_time, total_time, io_wait_time;
	u64 now = jiffies_to_nsecs(jiffies);
	s32 prev_idx;

	write_lock(&cputime_stat_lock);
	if (!cputime_stat) {
		write_unlock(&cputime_stat_lock);
		return;
	}
	prev_idx = winidx_sub(cputime_win_idx, 1);
	cputime_win_idx = winidx_add(cputime_win_idx, 1);

	for_each_possible_cpu(cpu) {
		if (cpu >= cpu_nums)
			continue;
		kcs = kcpustat_cpu(cpu);

		u64 curr_busy = kcs.cpustat[CPUTIME_USER] +
				kcs.cpustat[CPUTIME_NICE] +
				kcs.cpustat[CPUTIME_SYSTEM];
		u64 curr_io_wait = get_iowait_time(&kcs, cpu);
		u64 curr_idle = get_idle_time(&kcs, cpu);
		u64 curr_total = curr_busy + curr_idle + curr_io_wait +
				kcs.cpustat[CPUTIME_IRQ] +
				kcs.cpustat[CPUTIME_SOFTIRQ] +
				kcs.cpustat[CPUTIME_STEAL] +
				kcs.cpustat[CPUTIME_GUEST] +
				kcs.cpustat[CPUTIME_GUEST_NICE];

		if (cputime_stat[cpu][prev_idx].time_stamp) {
			busy_time = curr_busy - cputime_stat[cpu][prev_idx].busy_time;
			io_wait_time = curr_io_wait - cputime_stat[cpu][prev_idx].io_wait_time;
			total_time = curr_total - cputime_stat[cpu][prev_idx].total_time;
		} else {
			busy_time = curr_busy;
			io_wait_time = curr_io_wait;
			total_time = curr_total;
		}

		cputime_stat[cpu][cputime_win_idx].busy_time = curr_busy;
		cputime_stat[cpu][cputime_win_idx].io_wait_time = curr_io_wait;
		cputime_stat[cpu][cputime_win_idx].total_time = curr_total;

		if (total_time > 0) {
			cputime_stat[cpu][cputime_win_idx].cpu_load =
				(busy_time * 100) / total_time;
		} else {
			cputime_stat[cpu][cputime_win_idx].cpu_load = 0;
		}

		cputime_stat[cpu][cputime_win_idx].period = cpu_lm_period;
		cputime_stat[cpu][cputime_win_idx].time_stamp = now;
	}
	write_unlock(&cputime_stat_lock);

	schedule_delayed_work(&cpu_lm_work, msecs_to_jiffies(cpu_lm_period));
}

void cpuload_init(void)
{
	if (cputime_stat_init()) {
		pr_err("Failed to initialize cputime_stat\n");
		return;
	}
	INIT_DELAYED_WORK(&cpu_lm_work, cpu_lm_work_func);
	schedule_delayed_work(&cpu_lm_work, msecs_to_jiffies(cpu_lm_period));
}

struct proc_dir_entry *cpuload_proc_init(struct proc_dir_entry *pde)
{
	struct proc_dir_entry *proc_entry;

	proc_entry = proc_create("cpuload", 0444, pde, &proc_cpuload_ops);
	if (!proc_entry) {
		pr_err("Failed to create cpuload proc entry\n");
		return NULL;
	}

	proc_entry = proc_create("cpu_lm_period", 0644, pde, &proc_cpu_lm_period_ops);
	if (!proc_entry) {
		pr_err("Failed to create cpu_lm_period proc entry\n");
		return NULL;
	}

	return proc_entry;
}

void cpuload_exit(void)
{
	cancel_delayed_work_sync(&cpu_lm_work);
	cputime_stat_exit();
}

void cpuload_proc_deinit(struct proc_dir_entry *pde)
{
	remove_proc_entry("cpuload", pde);
	remove_proc_entry("cpu_lm_period", pde);
}

