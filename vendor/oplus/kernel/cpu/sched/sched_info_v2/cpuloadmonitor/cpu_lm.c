// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022, 2025 Oplus. All rights reserved.
 */

#include <linux/version.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/cpufreq.h>
#include <linux/sysctl.h>
#include <linux/cpumask.h>
#include <linux/cpuset.h>
#include <linux/cred.h>
#include <trace/hooks/cpufreq.h>
#include "../osi_base.h"
#include "../common/osi_hotthread.h"
#include "cpu_lm.h"
#include "osi_netlink.h"

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0))
#include <linux/nodemask_types.h>
#endif

#define CPU_LOAD_TIMER_RATE			5
#define CPU_HIGH_LOAD_THRESHOLD			4
#define CPU_LOW_LOAD_THRESHOLD			0
/* the peroid of  high load detection */
#define DETECT_PERIOD				1000000
#define CPU_LOAD_BIG_HIGHCYCLE			6
#define CPU_LOAD_BIG_LOWCYCLE			3
#define CPUS_PROC_DURING			4000000
#define CPUS_PROC_PERIOD			400000
#define SAMPLE_LOAD_CNT				10

#define PID_LIST_MAX				20
#define HIGH_LOAD_MAX_PIDS			20
#define HIGH_LOAD_MAX_TIDS			8

#define HIGH_LOAD_PERCENT			100
#define PERCENT_HUNDRED				100

#define PROC_STATIC_MAX				1024
#define PROC_STATIC_CPUSET_LBG			4	/* FIXME */
#define PROC_STATIC_CPUSET_BG			5
#define PROC_STATIC_CPUSET_HBG			4
#define PROC_STATIC_CPUSET_HFG			5
#define PROC_STATIC_CLUSTER_BIG			5

#define MAX_LAST_RQCLOCK			5000000
#define MAX_BUF_LEN				10
#define MAX_THRESHOLD				100

static s32 cpu_nums;

enum {
	DEFAULT		= 0,
	LOW_LOAD	= 1,
	MID_LOAD	= 2,
	HIGH_LOAD	= 3,
};

enum {
	CPUSET_LBG	= 0,				/* cpu2,3 */
	CPUSET_BG	= 1,				/* cpu0,1,2,3 */
	CPUSET_HBG	= 2,				/* cpu0,1,2,3,4 */
	CPUSET_HFG	= 3,				/* cpu4,5,6,7 */
	CPUSET_ALL	= 4,				/* all cpus */
	CLUSTER_BIG	= 5,			/* diacard */
	HIGH_LOAD_MAX_TYPE
};

#define MASK_LBG			0x0c
#define MASK_BG				0x0f
#define MASK_HBG			0x1f
#define MASK_HFG			0xf0


enum {
	LOAD_SWITCH_LBG		= 0,
	LOAD_SWITCH_BG		= 1,
	LOAD_SWITCH_HBG		= 2,
	LOAD_SWITCH_HFG		= 3,
	LOAD_SWITCH_DEFAULT	= 4,
	LOAD_SWITCH_BIGCORE	= 5,
};

struct task_id {
	/* FIXME: uid & tgid only */
	/* s32 pid; */
	s32 uid;
	s32 tgid;
};

struct high_load_data {
	s32 cmd;
	struct task_id id[HIGH_LOAD_MAX_PIDS];
};

struct action_ctl {
	u64 bits_high;
	u64 bits_mid;
	u64 bits_type;
};

struct action_ctl_struct {
	u64 bits_high;
	u64 bits_mid;
	u64 bits_type;
	u64 last_bits_type;
	s32 usage_percent[HIGH_LOAD_MAX_TYPE];
};

struct cpuset_notify_struct {
	struct work_struct cpuset_notify_work;
	bool inital;
};
static struct cpuset_notify_struct cpuset_notify_struct;

/*the definition to grab load state*/
#define  FUNCTION_BITS				8
#define  ACTIVE_GRAB_BIT			0
#define  PEROID_GRAB_BIT			1
#define  PERSEC_REPORT_SWITCH			2
#define  NOTIFY_CPUSET_BIT			3
#define  SHORT_PEROID_GRAB_BIT			4
#define  MAX_SWITH_NUM				8

static u8 control_array[MAX_SWITH_NUM];

static s32 clm_mux_switch;
static struct delayed_work grab_hotthread_work;
static s32 check_intervals;
static s32 check_statistics;

static s32 check_proc_static;
static s32 high_load_cnt[HIGH_LOAD_MAX_TYPE] = { 0 };
static s32 mid_load_cnt[HIGH_LOAD_MAX_TYPE] = { 0 };
static s32 normal_load_cnt[HIGH_LOAD_MAX_TYPE] = { 0 };
static s32 last_status[HIGH_LOAD_MAX_TYPE] = { 0 };
static s32 cycle_big_high_cnt[HIGH_LOAD_MAX_TYPE] = { 0 };
static s32 cycle_big_normal_cnt[HIGH_LOAD_MAX_TYPE] = { 0 };
static s32 sample_load_array[60] = { 0 };
static s32 cycle_big_cycles[HIGH_LOAD_MAX_TYPE] = { 0 };

#ifdef JANK_DEBUG
u64 high_load_switch = 0x0F;
#else
u64 high_load_switch;
#endif
s32 g_over_load;
static struct delayed_work high_load_work;
static struct delayed_work short_high_load_work;
static struct delayed_work cpus_proc_static_work;

#define ACTIVE_GRABTHREAD_DURATION	2000000 /* us*/
#define SHORT_HIGH_LOAD_DURATION	100000

static unsigned long cpumask_lbg					= MASK_LBG;
static unsigned long cpumask_bg						= MASK_BG;
static unsigned long cpumask_hbg					= MASK_HBG;
static __maybe_unused unsigned long cpumask_hfg				= MASK_HFG;

static u64 cpumask_big = 0x000000c0;

/*define threshod of bg_dtsate_percent*/
static s32 bg_dtsate_percent = 30;

static struct action_ctl action_ctl_bits = { 0 };
static struct action_ctl_struct action_ctl_struct = { 0 };

/*load level define*/
static u32 highload_threshold = 98;			/* all cpus */

static u32 load_level[][3] = {
	{95, 80, 75},  /*CPUSET_LBG*/
	{95, 80, 75},  /*CPUSET_BG*/
	{95, 80, 75},  /*CPUSET_HBG*/
	{95, 80, 75},  /*CPUSET_HFG*/
};

struct pid_stat_node {
	struct rb_node node;
	pid_t key_pid;
	uid_t uid;
	pid_t tgid;
	s32 count;
};

struct pid_stat_mgr {
	s32 index_curr;
	s32 max_count;
	struct pid_stat_node *head;
	struct rb_root rb_root;
	struct high_load_data data;
};

struct pid_stat_mgr g_pid_stat_mgt[HIGH_LOAD_MAX_TYPE] = { 0 };


static inline void set_action_ctl(u32 type, bool high_load)
{
	action_ctl_bits.bits_type |= (1 << type);

	if (high_load)
		action_ctl_bits.bits_high |= (1 << type);
}
static inline void action_ctl_report(u32 type, s32 load_type)
{
	action_ctl_struct.last_bits_type = action_ctl_struct.bits_type;
	action_ctl_struct.bits_type |= (1 << type);
	if (load_type == MID_LOAD)
		action_ctl_struct.bits_mid |= (1 << type);
	if (load_type == HIGH_LOAD)
		action_ctl_struct.bits_high |= (1 << type);
}

void hysteresis_process(u32 type, u64 in, u64 del_time, s32 *out)
{
	if (in >= load_level[type][1] * del_time)
		out[type]++;
	else if (in <= load_level[type][2] * del_time)
		out[type] = 0;
}

static void send_to_user_high(u32 type, s32 size, s32 *data)
{
	s32 i = 0;
	s32 idx;
	struct high_load_data high_data;

	/*
	 * Modify BYHP
	 * CPUSET_HBG, CPUSET_BG, and CPUSET_LBG groups use the same CMD
	 */
	if (type >= CPUSET_LBG && type <= CPUSET_HBG) {
		type = CPUSET_BG;
	}

	memset(&high_data, 0, sizeof(high_data));
	high_data.cmd = (type << 1) - 1;

	/* Modify BYHP */
	for (i = 0; i < size; i++) {
		idx = (sizeof(struct task_id)/sizeof(s32)) * i;

		/* FIXME: uid & tgid only */
		/* high_data.id[i].pid = data[idx]; */
		high_data.id[i].uid = data[idx];
		high_data.id[i].tgid = data[idx+1];
	}

	send_to_user(PROC_LOAD, sizeof(high_data) / sizeof(s32),
		(s32 *)&high_data);
}

static __maybe_unused void send_to_user_low(u32 type)
{
	struct high_load_data high_data;

	/*
	 * Modify BYHP
	 * CPUSET_HBG, CPUSET_BG, and CPUSET_LBG groups use the same CMD
	 */
	if (type >= CPUSET_LBG && type <= CPUSET_HBG) {
		type = CPUSET_BG;
	}

	memset(&high_data, 0, sizeof(high_data));
	high_data.cmd = (type << 1);

	/* FIXME: uid & pid only */
	/* high_data.id[0].pid = 0; */
	high_data.id[0].uid = 0;
	high_data.id[0].tgid = 0;
	send_to_user(PROC_LOAD, sizeof(high_data) / sizeof(s32),
		(s32 *)&high_data);
}

static s32 pidstat_init(void)
{
	s32 i = 0;

	for (i = 0; i < HIGH_LOAD_MAX_TYPE; i++) {
		g_pid_stat_mgt[i].index_curr = 0;
		g_pid_stat_mgt[i].max_count =
			(CPUS_PROC_DURING / CPUS_PROC_PERIOD) * cpu_nums;
		g_pid_stat_mgt[i].head =
			kmalloc_array(g_pid_stat_mgt[i].max_count,
				sizeof(*(g_pid_stat_mgt[i].head)), GFP_KERNEL);

		if (g_pid_stat_mgt[i].head == NULL)
			return -ENOMEM;

		g_pid_stat_mgt[i].rb_root = RB_ROOT;
		memset(g_pid_stat_mgt[i].head, 0, g_pid_stat_mgt[i].max_count *
			sizeof(*(g_pid_stat_mgt[i].head)));
		memset(&g_pid_stat_mgt[i].data, 0,
			sizeof(g_pid_stat_mgt[i].data));
	}

	pr_info("cpuload: pidstat init ok!");
	return 0;
}

static struct pid_stat_node *pid_stat_getnode(u32 type)
{
	if (g_pid_stat_mgt[type].index_curr >=
		(g_pid_stat_mgt[type].max_count - 1))
		return NULL;

	g_pid_stat_mgt[type].index_curr++;
	return g_pid_stat_mgt[type].head + g_pid_stat_mgt[type].index_curr - 1;
}

static void pid_stat_reset(u32 type)
{
	if (type >= HIGH_LOAD_MAX_TYPE)
		return;

	memset(g_pid_stat_mgt[type].head,
		0,
		g_pid_stat_mgt[type].index_curr *
		sizeof(*(g_pid_stat_mgt[type].head)));
	memset(&g_pid_stat_mgt[type].data, 0, sizeof(g_pid_stat_mgt[type].data));
	g_pid_stat_mgt[type].index_curr = 0;
	g_pid_stat_mgt[type].rb_root = RB_ROOT;
}

static void pid_stat_clear(void)
{
	s32 i = 0;

	for (i = 0; i < HIGH_LOAD_MAX_TYPE; i++) {
		if (g_pid_stat_mgt[i].head != NULL)
			kfree(g_pid_stat_mgt[i].head);

		memset(&g_pid_stat_mgt[i], 0, sizeof(g_pid_stat_mgt[i]));
	}
}

static void pid_stat_search_insert(pid_t key_pid, pid_t pid, uid_t uid, pid_t tgid, u32 type)
{
	struct rb_node **curr = &(g_pid_stat_mgt[type].rb_root.rb_node);
	struct rb_node *parent = NULL;
	struct pid_stat_node *this = NULL;
	pid_t ret_pid;
	struct pid_stat_node *new_node = NULL;

	while (*curr) {
		this = container_of(*curr, struct pid_stat_node, node);
		parent = *curr;
		ret_pid = key_pid - this->key_pid;

		if (ret_pid < 0) {
			curr = &((*curr)->rb_left);
		} else if (ret_pid > 0) {
			curr = &((*curr)->rb_right);
		} else {
			this->count++;
			return;
		}
	}

	/* add new node and rebalance tree. */
	new_node = pid_stat_getnode(type);
	if (new_node == NULL)
		return;

	new_node->key_pid = key_pid;
	new_node->uid = uid;
	new_node->tgid = tgid;
	new_node->count = 1;

	rb_link_node(&new_node->node, parent, curr);
	rb_insert_color(&new_node->node, &g_pid_stat_mgt[type].rb_root);
}


s32 send_to_user_netlink(s32 data)
{
	s32 dt[] = { data };

	return send_to_user(CPU_HIGH_LOAD, 1, dt);
}

s32 send_to_user_with_usage(s32 sock_no, s32 *data, s32 cnt)
{
	u32 i;
	u32 *dt = kmalloc_array(cpu_nums+1, sizeof(u32), GFP_KERNEL);
	if (!dt)
		return -ENOMEM;

	dt[0] = sock_no;
	for (i = 0; i < cnt; i++)
		dt[i+1] = data[i];

	return send_to_user(CPU_HIGH_LOAD, cnt+1, dt);
}

/*
 * This function gets called by the timer code, with HZ frequency.
 * We call it with interrupts disabled.
 */

bool high_load_tick(void);
bool high_load_tick_mask(u64 bits, u32 type);

static u32 get_mask_bytype(u32 type)
{
	switch (type) {
	case CPUSET_LBG:
		return cpumask_lbg;
	case CPUSET_BG:
		return cpumask_bg;
	case CPUSET_HBG:
		return cpumask_hbg;
	case CPUSET_HFG:
		return 0xff;				/* cpumask_hfg */
	case CLUSTER_BIG:
		return cpumask_big;
	case CPUSET_ALL:
		return MASK_BG | MASK_HFG;
	default:
		return 0;
	}
}

static s32 get_threhold_bytype(u32 type)
{
	switch (type) {
	case CPUSET_LBG:
		return PROC_STATIC_CPUSET_LBG;
	case CPUSET_BG:
		return PROC_STATIC_CPUSET_BG;
	case CPUSET_HBG:
		return PROC_STATIC_CPUSET_HBG;
	case CPUSET_HFG:
		return PROC_STATIC_CPUSET_HFG;
	case CLUSTER_BIG:
		return PROC_STATIC_CLUSTER_BIG;
	case CPUSET_ALL:
		return 1;
	default:
		return PROC_STATIC_MAX;
	}
}

static void high_load_tickfn(struct work_struct *work);
static void cpus_proc_static_tickfn(struct work_struct *work);

static void high_load_count_reset(void)
{
	check_intervals = 0;

	high_load_cnt[CPUSET_ALL] = 0;
	high_load_cnt[CPUSET_LBG] = 0;
	high_load_cnt[CPUSET_BG] = 0;
	high_load_cnt[CPUSET_HBG] = 0;
	high_load_cnt[CPUSET_HFG] = 0;
	high_load_cnt[CLUSTER_BIG] = 0;

	mid_load_cnt[CPUSET_ALL] = 0;
	mid_load_cnt[CPUSET_LBG] = 0;
	mid_load_cnt[CPUSET_BG] = 0;
	mid_load_cnt[CPUSET_HBG] = 0;
	mid_load_cnt[CPUSET_HFG] = 0;
	mid_load_cnt[CLUSTER_BIG] = 0;

	normal_load_cnt[CPUSET_LBG] = 0;
	normal_load_cnt[CPUSET_BG] = 0;
	normal_load_cnt[CPUSET_HBG] = 0;
	normal_load_cnt[CPUSET_HFG] = 0;
	normal_load_cnt[CLUSTER_BIG] = 0;
}

/*this function report high load by netlink*/
void highload_report(void)
{
	u32 type, state;
	u32 i = 0;
	u32 data[HIGH_LOAD_MAX_TYPE*2] = {-1};
	if (!action_ctl_bits.bits_type)
		return;
	for (type = CPUSET_LBG; type < HIGH_LOAD_MAX_TYPE; type++) {
		if (!(high_load_switch & (1 << type)))
			continue;

		if (action_ctl_bits.bits_type & (1 << type)) {
			if (action_ctl_bits.bits_high & (1 << type))
				state = HIGH_LOAD;
			else
				state = LOW_LOAD;
		} else {
			if (last_status[type] == HIGH_LOAD)
				state = HIGH_LOAD;
			else
				state = DEFAULT;
		}

		data[i++] = 1 << type;
		data[i++] = state;
	}
	send_to_user(CPU_HIGH_LOAD, i, data);
}

/*this function report high load by netlink*/
void highload_report_sample(void)
{
	u32 type, state;
	u32 i = 0;
	u32 data[HIGH_LOAD_MAX_TYPE*2] = {-1};

	if (!action_ctl_struct.bits_type)
		return;
	for (type = CPUSET_LBG; type < HIGH_LOAD_MAX_TYPE; type++) {
		if (!(high_load_switch & (1 << type)))
			continue;

		if (action_ctl_struct.bits_type & (1 << type)) {
			if (action_ctl_struct.bits_high & (1 << type))
				state = HIGH_LOAD;
			else if (action_ctl_struct.bits_mid & (1 << type))
				state = MID_LOAD;
			else
				state = LOW_LOAD;
		} else {
			if (last_status[type] == HIGH_LOAD)
				state = HIGH_LOAD;
			else if (last_status[type] == MID_LOAD)
				state = MID_LOAD;
			else
				state = DEFAULT;
		}

		data[i++] = 1 << type;
		data[i++] = action_ctl_struct.usage_percent[type];
	}
	pr_info("highload_report_sample:data[0]:%d", data[0]);
	send_to_user(PERSEC_HIGH_LOAD, i, data);
}

static void reset_action_data(void)
{
	action_ctl_struct.last_bits_type = action_ctl_struct.bits_type;
	action_ctl_struct.bits_type = 0;

	action_ctl_struct.bits_high = 0;
	action_ctl_struct.bits_mid = 0;
	memset(action_ctl_struct.usage_percent, 0, ARRAY_SIZE(action_ctl_struct.usage_percent));
}

void check_lowload(void)
{
	static u8 check_times;
	static bool condition;
	u32 type;
	s32 data;

	for (type = CPUSET_LBG; type < HIGH_LOAD_MAX_TYPE; type++) {
		if (action_ctl_struct.last_bits_type & (1 << type)) {
			pr_info("check_lowload:last status ok");
			check_times = 0;
			condition = true;
		}
	}
	for (type = CPUSET_LBG; type < HIGH_LOAD_MAX_TYPE; type++) {
		if (action_ctl_struct.bits_type & (1 << type)) {
			condition = false;
			break;
		}
	}
	if (type == HIGH_LOAD_MAX_TYPE && condition) {
		pr_info("check_lowload:check_times:%d", check_times);
		check_times++;
		if (check_times == CPU_LOAD_TIMER_RATE) {
			condition = false;
			check_times = 0;
			data = 0;
			pr_info("check_lowload:dt[0]:%d", data);
			send_to_user(ALL_GROUP_BACK_TO_LOW, 1, &data);
			g_over_load = 0;/*stop get hot thread*/
			memset(hot_thread_top, 0, sizeof(hot_thread_top));
		}
	}
}

/*
we can not get root css, we can use kworker get
root css, because kworker is root cpuset, then
css_for_each_descendant_pre can inter childrens.
*/

static void osi_notify_cpuset(bool is_inital)
{
	struct cgroup_subsys_state *cpuset_css, *child_css;
	struct cpumask *grp_mask;
	struct cpumask hfg_mask;

	/*get current  cpuset subsys, current is kworker*/
	cpuset_css = task_css(current, cpuset_cgrp_id);
	if (!cpuset_css) {
		pr_info("No cpuset cgroup for current task.\n");
		return;
	}

	css_for_each_descendant_pre(child_css, cpuset_css) {
		grp_mask = (struct cpumask *)((u64)child_css + sizeof(struct cgroup_subsys_state)
			+ sizeof(u64) + sizeof(cpumask_var_t));
		if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0))
			grp_mask = (struct cpumask *)((unsigned long long)grp_mask + sizeof(nodemask_t));
		pr_info("child css id: %d, %s, mask:%*pbl, %lx", child_css->id, child_css->cgroup->kn->name,
			 cpumask_pr_args(grp_mask), cpumask_bits(grp_mask)[0]);
		if (!strcmp(child_css->cgroup->kn->name, "l-background"))
			cpumask_lbg = cpumask_bits(grp_mask)[0];
		if (!strcmp(child_css->cgroup->kn->name, "h-background"))
			cpumask_hbg = cpumask_bits(grp_mask)[0];
		if (!strcmp(child_css->cgroup->kn->name, "background")) {
			cpumask_xor(&hfg_mask, cpu_possible_mask, grp_mask);
			cpumask_hfg = cpumask_bits(&hfg_mask)[0];
			cpumask_bg = cpumask_bits(grp_mask)[0];
		}
	}
	pr_info("update cpumask_lbg:%lx, cpumask_hbg:%lx, cpumask_hfg:%lx, cpumask_bg:%lx",
			cpumask_lbg, cpumask_hbg, cpumask_hfg, cpumask_bg);
}

static void notify_cpuset_fn(struct work_struct *work)
{
	struct cpuset_notify_struct *cur_struct = container_of(work, struct cpuset_notify_struct, cpuset_notify_work);
	if (cur_struct)
		osi_notify_cpuset(cur_struct->inital);
}

static void short_high_load_workfn(struct work_struct *work)
{
	u32 load_percent;

	load_percent = cpuload_get_by_window(1, 0xffffffff);
	if (load_percent > load_level[0][1]) {
		send_to_user(SHORT_PERSEC_HIGH_LOAD, 1, &load_percent);
	}

	schedule_delayed_work(&short_high_load_work, usecs_to_jiffies(SHORT_HIGH_LOAD_DURATION));
}

static void high_load_tickfn(struct work_struct *work)
{
	bool is_sample = false;
	reset_action_data();
	if (high_load_tick())
		goto HIGHLOAD_END;
	cpufreq_info_get(&is_sample);
	if (!is_sample)
		goto HIGHLOAD_END;

	++check_intervals;

	if (check_intervals >= CPU_LOAD_TIMER_RATE) {
		action_ctl_bits.bits_type = 0;
		action_ctl_bits.bits_high = 0;
	}
	/* GRP_LBG: cpu2,3 */
	if ((high_load_switch & (1 << LOAD_SWITCH_LBG)) != 0)
		high_load_tick_mask(cpumask_lbg, CPUSET_LBG);

	/* GRP_BG: cpu0,1,2,3 */
	if ((high_load_switch & (1 << LOAD_SWITCH_BG)) != 0)
		high_load_tick_mask(cpumask_bg, CPUSET_BG);

	/* GRP_HBG: cpu0,1,2,3,4 */
	if ((high_load_switch & (1 << LOAD_SWITCH_HBG)) != 0)
		high_load_tick_mask(cpumask_hbg, CPUSET_HBG);

	/* GRP_HFG: cpu4,5,6,7 */
	if ((high_load_switch & (1 << LOAD_SWITCH_HFG)) != 0)
		high_load_tick_mask(cpumask_hfg, CPUSET_HFG);
	if (check_intervals >= CPU_LOAD_TIMER_RATE) {
		if (action_ctl_bits.bits_type) {
			highload_report();
		}
	}
	if (action_ctl_bits.bits_type) {
		highload_report();
	}
	if (control_array[PERSEC_REPORT_SWITCH] & 1) {
		if (action_ctl_struct.bits_type) {
			g_over_load = 1;/*start get hot thread*/
			highload_report_sample();
		}
	}

HIGHLOAD_END:
	if (check_intervals >= CPU_LOAD_TIMER_RATE) {
		high_load_count_reset();
	}
	if (control_array[PERSEC_REPORT_SWITCH] & 1)
		check_lowload();
	schedule_delayed_work(&high_load_work, usecs_to_jiffies(DETECT_PERIOD));
}

static struct task_id threhold_pid_list[HIGH_LOAD_MAX_TYPE][PID_LIST_MAX] = { 0 };
static s32 threhold_pid_size[HIGH_LOAD_MAX_TYPE] = { 0 };

static void cpus_procstatic_low(void)
{
	u32 type;
	s32 i;

	for (type = 1; type < HIGH_LOAD_MAX_TYPE; type++) {
		if (!(action_ctl_bits.bits_type & (1 << type)))
			continue;
		if (action_ctl_bits.bits_high & (1 << type))
			continue;
		if (action_ctl_bits.bits_mid & (1 << type))
			continue;
		pr_info("cpuload: delete size:%d type:%u",
			threhold_pid_size[type], type);
		/*
		 * Modify BYHP
		 * Do NOT reported when the load is low
		 */
		/* send_to_user_low(type); */
		for (i = 0; i < threhold_pid_size[type]; i++) {
			/* Modify BYHP */
			/* threhold_pid_list[type][i].pid = 0; */
			threhold_pid_list[type][i].uid = 0;
			threhold_pid_list[type][i].tgid = 0;
		}

		pid_stat_reset(type);
		threhold_pid_size[type] = 0;
	}
}

static void cpus_proc_static_high(u32 type)
{
	s32 i;
	struct pid_stat_node *curr = NULL;
	s32 index;

	for (i = 0, curr = g_pid_stat_mgt[type].head;
		i < g_pid_stat_mgt[type].index_curr;
		i++, curr++) {
		if (curr->count > get_threhold_bytype(type)) {
				pr_info("%s: key_pid:%d,count:%d,type:%u", __func__,
				curr->key_pid, curr->count, type);

			if (threhold_pid_size[type] < PID_LIST_MAX) {
				index = threhold_pid_size[type];

				/* Modify BYHP */
				/* threhold_pid_list[type][index].pid = curr->key_pid; */
				threhold_pid_list[type][index].uid = curr->uid;
				threhold_pid_list[type][index].tgid = curr->tgid;

				threhold_pid_size[type]++;
			}
		}
	}

	if (threhold_pid_size[type])
		send_to_user_high(type, threhold_pid_size[type], (s32 *)threhold_pid_list[type]);

	pid_stat_reset(type);
	for (i = 0; i < threhold_pid_size[type]; i++) {
		/* threhold_pid_list[type][i].pid = 0; */
		threhold_pid_list[type][i].uid = 0;
		threhold_pid_list[type][i].tgid = 0;
	}
	threhold_pid_size[type] = 0;
}

bool high_load_tick(void)
{
	bool ret = false;
	u32 cpu_load = 0;
	u32 i;
	u32 *usage_per;
	static s32 last_report_reason = LOW_LOAD;

	usage_per = kmalloc_array(cpu_nums, sizeof(u32), GFP_KERNEL);
	if (!usage_per)
		return ret;
	memset(usage_per, 0, cpu_nums * sizeof(u32));

	/*
	  echo 513 > /proc/jank_info/cpu_jank_info/clm_mux_switch
	  echo 512 > /proc/jank_info/cpu_jank_info/clm_mux_switch
	function_bits =  clm_mux_switch >> FUNCTION_BITS;
	if (function_bits & (1 << PEROID_GRAB_BIT))
	{
		if (!(clm_mux_switch & 1))
			return ret;
	}
	*/

	cpu_load = cpuload_get_by_window(1, 0xffffffff);
	if (cpu_load != 0) {
		sample_load_array[check_intervals] = cpu_load;
		if (cpu_load >= highload_threshold)
			high_load_cnt[CPUSET_ALL]++;
	}
	/* use send_to_user_with_usage send */
	if (check_statistics >= 60) {
		check_statistics = 0;
		send_to_user(HISTORY_TOTAL_LOADS, ARRAY_SIZE(sample_load_array), sample_load_array);
		memset(sample_load_array, 0,  ARRAY_SIZE(sample_load_array));
	}

	if (check_intervals >= CPU_LOAD_TIMER_RATE) {
		if (((high_load_switch & (1 << LOAD_SWITCH_DEFAULT)) != 0)
			&& high_load_cnt[CPUSET_ALL] >= CPU_HIGH_LOAD_THRESHOLD
			&& last_report_reason != HIGH_LOAD) {
			for_each_possible_cpu(i) {
				cpu_load = cpuload_get_by_window(1, i);
				if (!cpu_load)
					usage_per[i] = 0;
				else
					usage_per[i] = cpu_load;
			}
			send_to_user_with_usage(HIGH_LOAD, usage_per, cpu_nums);
			last_report_reason = HIGH_LOAD;
			ret = true;
			pr_info("cpuload: cpuload HIGH_LOAD!");
		} else if (high_load_cnt[CPUSET_ALL] == CPU_LOW_LOAD_THRESHOLD
				&& last_report_reason != LOW_LOAD) {
			send_to_user_netlink(LOW_LOAD);
			last_report_reason = LOW_LOAD;
			ret = true;
			pr_info("cpuload: cpuload LOW_LOAD!");
		}
	}
	return ret;
}

static void cycle_big_count_reset(u32 type)
{
	cycle_big_high_cnt[type] = 0;
	cycle_big_normal_cnt[type] = 0;
	cycle_big_cycles[type] = 0;
}

static bool cycle_big_highcycle(u32 type)
{
	bool ret = false;

	if (last_status[type] == HIGH_LOAD) {
		if (cycle_big_high_cnt[type] >
			(CPU_LOAD_BIG_HIGHCYCLE * CPU_HIGH_LOAD_THRESHOLD)) {
			pr_info("cpuload: type=%d, cycle big HIGH_LOAD!", type);
			set_action_ctl(type, true);
			ret = true;
		}
	}

	cycle_big_count_reset(type);
	return ret;
}

static bool cycle_big_lowcycle(u32 type)
{
	if (last_status[type] == HIGH_LOAD) {
		if (cycle_big_normal_cnt[type] >
			(CPU_LOAD_BIG_LOWCYCLE * CPU_HIGH_LOAD_THRESHOLD)) {
			pr_info("cpuload: type=%d, cycle big LOW_LOAD!", type);
			last_status[type] = LOW_LOAD;
			set_action_ctl(type, false);
			cycle_big_count_reset(type);
			return true;
		}
	}

	return false;
}

/* high load sampling
 *@bits: the mask of different grp
 *@type: grp num
*/
bool high_load_tick_mask(u64 bits, u32 type)
{
	u32 cpu_load_bits;

	cpu_load_bits = cpuload_get_by_window(1, bits);
	if (cpu_load_bits) {
		action_ctl_struct.usage_percent[type] = cpu_load_bits;
		if (cpu_load_bits >= (load_level[type][0])) {
			high_load_cnt[type]++;
			cycle_big_high_cnt[type]++;
		}
		/* is high load?*/
		if (cpu_load_bits >= (load_level[type][1])) {
			action_ctl_report(type, HIGH_LOAD);
		}
		if (cpu_load_bits < (load_level[type][2])) {
			normal_load_cnt[type]++;
			cycle_big_normal_cnt[type]++;
		}
	}

	if (check_intervals < CPU_LOAD_TIMER_RATE)
		return false;

	cycle_big_cycles[type]++;
	if (high_load_cnt[type] >= CPU_HIGH_LOAD_THRESHOLD &&
		last_status[type] != HIGH_LOAD) {
		pr_info("cpuload: cpuload mask HIGH_LOAD\n");
		last_status[type] = HIGH_LOAD;
		set_action_ctl(type, true);
		cycle_big_count_reset(type);
		return true;
	}
	if ((cycle_big_cycles[type] % CPU_LOAD_BIG_LOWCYCLE) == 0) {
		if (cycle_big_lowcycle(type))
			return true;
	}

	if (cycle_big_cycles[type] >= CPU_LOAD_BIG_HIGHCYCLE) {
		if (cycle_big_highcycle(type))
			return true;
	}

	return false;
}

static void get_current_task_or_thread_mask(u32 type, bool is_thread)
{
	u32 cpu = 0;
	pid_t curr_pid;
	pid_t local_pid = current->pid;
	uid_t uid;
	pid_t tgid;
	u64 cpumask = get_mask_bytype(type);
	const struct cred *tcred;

	for_each_possible_cpu(cpu) {
		struct rq *rq_cur = NULL;
		struct task_struct *p = NULL;

		if (((1 << cpu) & cpumask) == 0)
			continue;

		rq_cur = cpu_rq(cpu);
		if (!rq_cur) {
			osi_err("cpuload: cpu:%u rq_cur is NULL!", cpu);
			continue;
		}

		p = rq_cur->curr;
		if (!p) {
			osi_err("cpuload: cpu:%u p is NULL!", cpu);
			continue;
		}

		get_task_struct(p);
		curr_pid = p->pid;

		rcu_read_lock();
		tcred = __task_cred(p);
		if (!tcred && !is_thread) {
			rcu_read_unlock();
			put_task_struct(p);
			pr_info("cpuload: tcred is NULL!");
			continue;
		}

		if (tcred)
			uid = __kuid_val(tcred->uid);
		else
			uid = 0;

		rcu_read_unlock();

		tgid = p->tgid;
		put_task_struct(p);

		if (curr_pid == 0 || curr_pid == local_pid)
			continue;

		if (curr_pid != 0)
			pid_stat_search_insert(tgid, curr_pid, uid, tgid, type);
	}
}

static void get_current_task_mask(u32 type)
{
	get_current_task_or_thread_mask(type, false);
}

static void get_current_thread_mask(u32 type)
{
	get_current_task_or_thread_mask(type, true);
}

/* This function is executed every 400ms */
static __maybe_unused void cpus_proc_static_tickfn(struct work_struct *work)
{
	s32 type;

	if (check_proc_static == 0)
		cpus_procstatic_low();

	check_proc_static += CPUS_PROC_PERIOD;

	for (type = CPUSET_HFG; type >= CPUSET_LBG; type--) {
		if (!(high_load_switch & (1 << type)))
			continue;

		if (!(action_ctl_bits.bits_type & (1 << type)))
			continue;

		if (!(action_ctl_bits.bits_high & (1 << type)))
			continue;
		pr_info("cpuload: [cpus_proc_static_tickfn: type=%d]\n", type);
		/* Samples were taken every 400ms */
		/* Sample only and insert the red-black tree */
		get_current_task_mask(type);

		/* Report in four seconds */
		if (check_proc_static >= CPUS_PROC_DURING)
			cpus_proc_static_high(type);

		if (type != CPUSET_HFG)
			break;
	}

	if (check_proc_static >= CPUS_PROC_DURING) {
		check_proc_static = 0;
	} else {
		schedule_delayed_work_on(0, &cpus_proc_static_work,
			usecs_to_jiffies(CPUS_PROC_PERIOD));
	}
}

/* This function is executed every 400ms */
static __maybe_unused void cpus_proc_static_tickfn_unused(struct work_struct *work)
{
	u32 type;

	if (check_proc_static == 0)
		cpus_procstatic_low();

	check_proc_static += CPUS_PROC_PERIOD;

	for (type = 1; type < HIGH_LOAD_MAX_TYPE; type++) {
		if (!(action_ctl_bits.bits_type & (1 << type)))
			continue;

		if (!(action_ctl_bits.bits_high & (1 << type)))
			continue;

		/* get thread or task statistic */
		if (type == CLUSTER_BIG)
			get_current_thread_mask(type);
		else
			get_current_task_mask(type);

		if (check_proc_static >= CPUS_PROC_DURING)
			cpus_proc_static_high(type);
	}

	if (check_proc_static >= CPUS_PROC_DURING) {
		check_proc_static = 0;
	} else {
		schedule_delayed_work_on(0, &cpus_proc_static_work,
			usecs_to_jiffies(CPUS_PROC_PERIOD));
	}
}


static void grab_hotthread_workfn(struct work_struct *work)
{
	/*sample*/
	get_current_task_mask(CPUSET_ALL);
	/*report*/
	cpus_proc_static_high(CPUSET_ALL);

	schedule_delayed_work_on(0, &grab_hotthread_work,
			usecs_to_jiffies(ACTIVE_GRABTHREAD_DURATION));
}

static ssize_t proc_clm_enable_read(struct file *file,
		char __user *buf, size_t count, loff_t *ppos)
{
	char buffer[PROC_NUMBUF];
	size_t len = 0;

	len = snprintf(buffer, sizeof(buffer), "%llu\n", high_load_switch);
	return simple_read_from_buffer(buf, count, ppos, buffer, len);
}

static ssize_t proc_clm_enable_write(struct file *file,
			const char __user *buf, size_t count, loff_t *ppos)
{
	char buffer[PROC_NUMBUF];
	s32 err, enable;

	memset(buffer, 0, sizeof(buffer));

	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;

	if (copy_from_user(buffer, buf, count))
		return -EFAULT;

	err = kstrtouint(strstrip(buffer), 0, &enable);
	if (err)
		return err;

	if (enable != 0) {
		if (!high_load_switch) {
			schedule_delayed_work(&high_load_work,
				usecs_to_jiffies(DETECT_PERIOD));
			cpuset_notify_struct.inital = true;
			queue_work(system_wq, &cpuset_notify_struct.cpuset_notify_work);
		}
	} else {
		if (high_load_switch) {
			high_load_count_reset();
			cancel_delayed_work_sync(&high_load_work);
		}
	}

	high_load_switch = enable;

	return count;
}

static ssize_t proc_clm_highload_all_read(struct file *file,
		char __user *buf, size_t count, loff_t *ppos)
{
	char buffer[PROC_NUMBUF];
	size_t len = 0;

	len = snprintf(buffer, sizeof(buffer), "%d\n", highload_threshold);
	return simple_read_from_buffer(buf, count, ppos, buffer, len);
}

static ssize_t proc_clm_highload_all_write(struct file *file,
			const char __user *buf, size_t count, loff_t *ppos)
{
	char buffer[PROC_NUMBUF];
	s32 err, threshold;

	memset(buffer, 0, sizeof(buffer));

	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;

	if (copy_from_user(buffer, buf, count))
		return -EFAULT;

	err = kstrtouint(strstrip(buffer), 0, &threshold);
	if (err)
		return err;

	highload_threshold = threshold;

	return count;
}

static ssize_t proc_clm_highload_grp_read(struct file *file,
		char __user *buf, size_t count, loff_t *ppos)
{
	char buffer[PROC_NUMBUF];
	size_t len = 0;

	len = snprintf(buffer, sizeof(buffer), "%d,%d,%d,%d\n", load_level[CPUSET_LBG][0],
		load_level[CPUSET_BG][0], load_level[CPUSET_HBG][0], load_level[CPUSET_HFG][0]);
	return simple_read_from_buffer(buf, count, ppos, buffer, len);
}

static ssize_t proc_clm_highload_grp_write(struct file *file,
			const char __user *buf, size_t count, loff_t *ppos)
{
	char buffer[PROC_NUMBUF];
	s32 err, threshold;

	memset(buffer, 0, sizeof(buffer));
	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;

	if (copy_from_user(buffer, buf, count))
		return -EFAULT;

	err = kstrtouint(strstrip(buffer), 0, &threshold);
	if (err)
		return err;

	load_level[CPUSET_LBG][0] = threshold;
	load_level[CPUSET_BG][0] = threshold;
	load_level[CPUSET_HBG][0] = threshold;
	load_level[CPUSET_HFG][0] = threshold;
	return count;
}

static ssize_t proc_clm_report_threshold_read(struct file *file,
		char __user *buf, size_t count, loff_t *ppos)
{
	char buffer[2*PROC_NUMBUF];
	size_t len = 0;

	len = snprintf(buffer, sizeof(buffer), "%d,%d,%d,%d\n", load_level[CPUSET_LBG][1],
		load_level[CPUSET_BG][1], load_level[CPUSET_HBG][1], load_level[CPUSET_HFG][1]);
	return simple_read_from_buffer(buf, count, ppos, buffer, len);
}

static ssize_t proc_clm_report_threshold_write(struct file *file,
			const char __user *buf, size_t count, loff_t *ppos)
{
	char buffer[PROC_NUMBUF];
	s32 err, threshold;

	memset(buffer, 0, sizeof(buffer));

	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;

	if (copy_from_user(buffer, buf, count))
		return -EFAULT;

	err = kstrtouint(strstrip(buffer), 0, &threshold);
	if (err)
		return err;

	load_level[CPUSET_LBG][1] = threshold;
	load_level[CPUSET_BG][1] = threshold;
	load_level[CPUSET_HBG][1] = threshold;
	load_level[CPUSET_HFG][1] = threshold;
	return count;
}

static ssize_t proc_clm_lowload_grp_read(struct file *file,
		char __user *buf, size_t count, loff_t *ppos)
{
	char buffer[PROC_NUMBUF];
	size_t len = 0;

	len = snprintf(buffer, sizeof(buffer), "%d,%d,%d,%d\n", load_level[CPUSET_LBG][2],
		load_level[CPUSET_BG][2], load_level[CPUSET_HBG][2], load_level[CPUSET_HFG][2]);

	return simple_read_from_buffer(buf, count, ppos, buffer, len);
}

static ssize_t proc_clm_lowload_grp_write(struct file *file,
			const char __user *buf, size_t count, loff_t *ppos)
{
	char buffer[PROC_NUMBUF];
	s32 err, threshold;

	memset(buffer, 0, sizeof(buffer));

	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;

	if (copy_from_user(buffer, buf, count))
		return -EFAULT;

	err = kstrtouint(strstrip(buffer), 0, &threshold);
	if (err)
		return err;

	load_level[CPUSET_LBG][2] = threshold;
	load_level[CPUSET_BG][2] = threshold;
	load_level[CPUSET_HBG][2] = threshold;
	load_level[CPUSET_HFG][2] = threshold;

	return count;
}

static ssize_t proc_bg_dstat_percent_read(struct file *file,
		char __user *buf, size_t count, loff_t *ppos)
{
	char buffer[PROC_NUMBUF];
	size_t len = 0;

	len = snprintf(buffer, sizeof(buffer), "%d\n", bg_dtsate_percent);
	return simple_read_from_buffer(buf, count, ppos, buffer, len);
}

static ssize_t proc_bg_dstat_percent_write(struct file *file,
			const char __user *buf, size_t count, loff_t *ppos)
{
	char buffer[PROC_NUMBUF];
	s32 err, percent;

	memset(buffer, 0, sizeof(buffer));

	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;

	if (copy_from_user(buffer, buf, count))
		return -EFAULT;

	err = kstrtouint(strstrip(buffer), 0, &percent);
	if (err)
		return err;
	bg_dtsate_percent = percent;

	return count;
}

static ssize_t proc_clm_mux_switch_read(struct file *file,
		char __user *buf, size_t count, loff_t *ppos)
{
	char buffer[PROC_NUMBUF];
	size_t len = 0;

	len = snprintf(buffer, sizeof(buffer), "%d\n",  clm_mux_switch);
	return simple_read_from_buffer(buf, count, ppos, buffer, len);
}


static ssize_t proc_clm_mux_switch_write(struct file *file,
			const char __user *buf, size_t count, loff_t *ppos)
{
	char buffer[PROC_NUMBUF];
	s32 err, state;
	char function_bits;

	memset(buffer, 0, sizeof(buffer));

	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;

	if (copy_from_user(buffer, buf, count))
		return -EFAULT;

	err = kstrtouint(strstrip(buffer), 0, &state);
	if (err)
		return err;

	clm_mux_switch = state;

	function_bits = clm_mux_switch >> FUNCTION_BITS;
	if ((function_bits <= 0) || ffs(function_bits) > MAX_SWITH_NUM)
		goto done;
	control_array[ffs(function_bits) - 1] = clm_mux_switch & 1;

	if (control_array[ACTIVE_GRAB_BIT] & 1)
		schedule_delayed_work(&grab_hotthread_work, usecs_to_jiffies(ACTIVE_GRABTHREAD_DURATION));
	else
		cancel_delayed_work_sync(&grab_hotthread_work);
	if (control_array[SHORT_PEROID_GRAB_BIT] & 1) {
		cpu_lm_peroid_set(SHORT_HIGH_LOAD_DURATION / 1000);
		schedule_delayed_work(&short_high_load_work, usecs_to_jiffies(SHORT_HIGH_LOAD_DURATION));
	} else {
		cpu_lm_peroid_set(DEFAULT_CPU_LM_PERIOD);
		cancel_delayed_work_sync(&short_high_load_work);
	}

	if (control_array[NOTIFY_CPUSET_BIT] & 1) {
		cpuset_notify_struct.inital = false;
		queue_work(system_wq, &cpuset_notify_struct.cpuset_notify_work);
		control_array[NOTIFY_CPUSET_BIT] = 0;
	}
done:
	return count;
}

static const struct proc_ops proc_clm_enable_operations = {
	.proc_read	= proc_clm_enable_read,
	.proc_write	= proc_clm_enable_write,
	.proc_lseek	=	default_llseek,
};

static const struct proc_ops proc_clm_highload_all_operations = {
	.proc_read	= proc_clm_highload_all_read,
	.proc_write	= proc_clm_highload_all_write,
	.proc_lseek	=	default_llseek,
};

static const struct proc_ops proc_clm_highload_grp_operations = {
	.proc_read	= proc_clm_highload_grp_read,
	.proc_write	= proc_clm_highload_grp_write,
	.proc_lseek	=	default_llseek,
};

static const struct proc_ops proc_clm_report_threshold_operations = {
	.proc_read	= proc_clm_report_threshold_read,
	.proc_write	= proc_clm_report_threshold_write,
	.proc_lseek	=	default_llseek,
};

static const struct proc_ops proc_clm_lowload_grp_operations = {
	.proc_read	= proc_clm_lowload_grp_read,
	.proc_write	= proc_clm_lowload_grp_write,
	.proc_lseek	=	default_llseek,
};

static const struct proc_ops proc_bg_dstat_percent_operations = {
	.proc_read	= proc_bg_dstat_percent_read,
	.proc_write	= proc_bg_dstat_percent_write,
	.proc_lseek	=	default_llseek,
};

static const struct proc_ops proc_clm_mux_switch_operations = {
	.proc_read	= proc_clm_mux_switch_read,
	.proc_write	= proc_clm_mux_switch_write,
	.proc_lseek	=	default_llseek,
};

struct proc_dir_entry *jank_calcload_proc_init(
			struct proc_dir_entry *pde)
{
	struct proc_dir_entry *entry = NULL;


	entry = proc_create("clm_enable", S_IRUGO | S_IWUGO,
				pde, &proc_clm_enable_operations);
	if (!entry) {
		pr_info("create clm_enable fail\n");
		goto err_clm_enable;
	}

	entry = proc_create("clm_highload_all", S_IRUGO | S_IWUGO,
				pde, &proc_clm_highload_all_operations);
	if (!entry) {
		pr_info("create clm_highload_all fail\n");
		goto err_clm_highload_all;
	}

	entry = proc_create("clm_highload_grp", S_IRUGO | S_IWUGO,
				pde, &proc_clm_highload_grp_operations);
	if (!entry) {
		pr_info("create clm_highload_grp fail\n");
		goto err_clm_highload_grp;
	}

	entry = proc_create("clm_report_threshold", S_IRUGO | S_IWUGO,
				pde, &proc_clm_report_threshold_operations);
	if (!entry) {
		pr_info("create clm_report_threshold fail\n");
		goto err_clm_report_threshold;
	}

	entry = proc_create("clm_lowload_grp", S_IRUGO | S_IWUGO,
				pde, &proc_clm_lowload_grp_operations);
	if (!entry) {
		pr_info("create clm_lowload_grp fail\n");
		goto err_clm_lowload_grp;
	}
	entry = proc_create("bg_dstat_percent", S_IRUGO | S_IWUGO,
				pde, &proc_bg_dstat_percent_operations);
	if (!entry) {
		pr_info("create bg_dstat_percent fail\n");
		goto err_bg_dstat_percent;
	}
	entry = proc_create("clm_mux_switch", S_IRUGO | S_IWUGO,
				pde, &proc_clm_mux_switch_operations);
	if (!entry) {
		pr_info("create clm_mux_switch fail\n");
		goto err_clm_mux_switch;
	}

	return entry;

err_clm_lowload_grp:
	remove_proc_entry("clm_lowload_grp", pde);
err_clm_highload_grp:
	remove_proc_entry("clm_highload_grp", pde);
err_clm_report_threshold:
	remove_proc_entry("clm_report_threshold", pde);
err_clm_highload_all:
	remove_proc_entry("clm_highload_all", pde);
err_clm_enable:
	remove_proc_entry("clm_enable", pde);
err_bg_dstat_percent:
	remove_proc_entry("bg_dstat_percent", pde);
err_clm_mux_switch:
	remove_proc_entry("clm_mux_switch", pde);
	return NULL;
}

void jank_calcload_proc_deinit(struct proc_dir_entry *pde)
{
	remove_proc_entry("clm_lowload_grp", pde);
	remove_proc_entry("clm_report_threshold", pde);
	remove_proc_entry("clm_highload_grp", pde);
	remove_proc_entry("clm_highload_all", pde);
	remove_proc_entry("clm_enable", pde);
	remove_proc_entry("bg_dstat_percent", pde);
	remove_proc_entry("clm_mux_switch", pde);
}

void jank_calcload_init(void)
{
	s32 i = 0;
	check_intervals = 0;
	check_proc_static = 0;

	cpu_nums = cpu_nums_get();
	if (pidstat_init() != 0) {
		osi_err("cpuloadmonitor init failed!\n");
		return;
	}

	INIT_DEFERRABLE_WORK(&high_load_work, high_load_tickfn);
	INIT_DEFERRABLE_WORK(&grab_hotthread_work, grab_hotthread_workfn);
	INIT_DEFERRABLE_WORK(&short_high_load_work, short_high_load_workfn);
	INIT_WORK(&cpuset_notify_struct.cpuset_notify_work, notify_cpuset_fn);

	for (i = 0; i < HIGH_LOAD_MAX_TYPE; i++)
		last_status[i] = LOW_LOAD;

	create_cpu_netlink(NETLINK_OPLUS_CPU);
}

void jank_calcload_exit(void)
{
	if (high_load_switch) {
		high_load_count_reset();
		cancel_delayed_work_sync(&high_load_work);
	}
	high_load_switch = 0;

	pid_stat_clear();
	destroy_cpu_netlink();
}
