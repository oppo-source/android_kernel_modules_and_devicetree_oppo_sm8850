/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024 Oplus. All rights reserved.
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM hmbird_II

#if !defined(_TRACE_HMBIRD_II_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HMBIRD_II_H

#include <linux/sched.h>
#include <linux/types.h>
#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(hmbird_cfg_val,

	TP_PROTO(unsigned int val),

	TP_ARGS(val),

	TP_STRUCT__entry(
		__field(unsigned int, val)),

	TP_fast_assign(
		__entry->val = val;),

	TP_printk("val=%u",
		__entry->val)
);

DEFINE_EVENT(hmbird_cfg_val, hmbird_debug_update,
	TP_PROTO(unsigned int hmbird_debug),
	TP_ARGS(hmbird_debug));

DEFINE_EVENT(hmbird_cfg_val, hmbird_frame_update,
	TP_PROTO(unsigned int frame),
	TP_ARGS(frame));
DEFINE_EVENT(hmbird_cfg_val, hmbird_busy_pct_update,
	TP_PROTO(unsigned int busy_pct),
	TP_ARGS(busy_pct));

DEFINE_EVENT(hmbird_cfg_val, hmbird_feature_update,
	TP_PROTO(unsigned int feat),
	TP_ARGS(feat));

TRACE_EVENT(hmbird_cfg_coefficient,

	TP_PROTO(int cluster_id, unsigned long coefficient),

	TP_ARGS(cluster_id, coefficient),

	TP_STRUCT__entry(
		__field(int, cluster_id)
		__field(unsigned long, coefficient)),

	TP_fast_assign(
		__entry->cluster_id = cluster_id;
		__entry->coefficient = coefficient;),

	TP_printk("cfg_coefficient:cluster=%d, coefficient=%lu",
		__entry->cluster_id, __entry->coefficient)
);

TRACE_EVENT(hmbird_cfg_perf_high_ratio,

	TP_PROTO(int cluster_id, unsigned long perf_high_ratio),

	TP_ARGS(cluster_id, perf_high_ratio),

	TP_STRUCT__entry(
		__field(int, cluster_id)
		__field(unsigned long, perf_high_ratio)),

	TP_fast_assign(
		__entry->cluster_id = cluster_id;
		__entry->perf_high_ratio = perf_high_ratio;),

	TP_printk("cfg_perf_high_ratio:cluster=%d, perf_high_ratio=%lu",
		__entry->cluster_id, __entry->perf_high_ratio)
);

TRACE_EVENT(hmbird_cfg_freq_policy,

	TP_PROTO(int cluster_id, unsigned long freq_policy),

	TP_ARGS(cluster_id, freq_policy),

	TP_STRUCT__entry(
		__field(int, cluster_id)
		__field(unsigned long, freq_policy)),

	TP_fast_assign(
		__entry->cluster_id = cluster_id;
		__entry->freq_policy = freq_policy;),

	TP_printk("cfg_freq_policy:cluster=%d, freq_policy=%lu",
		__entry->cluster_id, __entry->freq_policy)
);

TRACE_EVENT(hb_sched_switch,

	TP_PROTO(bool preempt, struct task_struct *prev,
		struct task_struct *next, unsigned int prev_state, bool *done),

	TP_ARGS(preempt, prev, next, prev_state, done),

	TP_STRUCT__entry(
		__array(char, prev_comm, TASK_COMM_LEN)
		__field(pid_t, prev_pid)
		__field(int, prev_prio)
		__field(long, prev_state)
		__array(char, next_comm, TASK_COMM_LEN)
		__field(pid_t, next_pid)
		__field(int, next_prio)
	),

	TP_fast_assign(
		memcpy(__entry->next_comm, next->comm, TASK_COMM_LEN);
		__entry->prev_pid	= prev->pid;
		__entry->prev_prio	= prev->prio;
		__entry->prev_state	= prev_state;
		memcpy(__entry->prev_comm, prev->comm, TASK_COMM_LEN);
		__entry->next_pid	= next->pid;
		__entry->next_prio	= next->prio;
	),

	TP_printk("prev_comm=%s prev_pid=%d prev_prio=%d prev_state=%s%s ==> next_comm=%s next_pid=%d next_prio=%d",
		__entry->prev_comm, __entry->prev_pid, __entry->prev_prio,

		(__entry->prev_state & (TASK_REPORT_MAX - 1)) ?
		  __print_flags(__entry->prev_state & (TASK_REPORT_MAX - 1), "|",
				{ TASK_INTERRUPTIBLE, "S" },
				{ TASK_UNINTERRUPTIBLE, "D" },
				{ __TASK_STOPPED, "T" },
				{ __TASK_TRACED, "t" },
				{ EXIT_DEAD, "X" },
				{ EXIT_ZOMBIE, "Z" },
				{ TASK_PARKED, "P" },
				{ TASK_DEAD, "I" }) :
		  "R",

		__entry->prev_state & TASK_REPORT_MAX ? "+" : "",
		__entry->next_comm, __entry->next_pid, __entry->next_prio)
);

#endif /*_TRACE_HMBIRD_II_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ./hmbird_II

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace_hmbird_II
/* This part must be outside protection */
#include <trace/define_trace.h>
