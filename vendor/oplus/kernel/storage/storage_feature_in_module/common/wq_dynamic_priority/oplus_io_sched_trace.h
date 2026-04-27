/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Tracepoints for oplus_io_sched
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM oplus_io_sched

#if !defined(_TRACE_OPLUS_IO_SCHED_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_OPLUS_IO_SCHED_H

#include <linux/tracepoint.h>
#include <linux/blkdev.h>

#define TASK_TYPE_RT	0
#define TASK_TYPE_UX	1
#define TASK_TYPE_CFS	2

#define show_task_type(type)			\
	__print_symbolic(type,			\
		{ TASK_TYPE_RT,  "RT"  },	\
		{ TASK_TYPE_UX,  "UX"  },	\
		{ TASK_TYPE_CFS, "CFS" })

#define show_ioprio_class(class)			\
	__print_symbolic(class,				\
		{ IOPRIO_CLASS_NONE, "NONE" },		\
		{ IOPRIO_CLASS_RT,   "RT"   },		\
		{ IOPRIO_CLASS_BE,   "BE"   },		\
		{ IOPRIO_CLASS_IDLE, "IDLE" })

#define QUEUE_WORK_KBLOCKD	0
#define QUEUE_WORK_KTHREAD	1

#define show_queue_work_type(type)			\
	__print_symbolic(type,				\
		{ QUEUE_WORK_KBLOCKD, "kblockd" },	\
		{ QUEUE_WORK_KTHREAD, "kthread" })

TRACE_EVENT(sdd_insert,

	TP_PROTO(struct request *rq, int task_type, int io_class),

	TP_ARGS(rq, task_type, io_class),

	TP_STRUCT__entry(
		__array(char, comm, TASK_COMM_LEN)
		__field(pid_t, pid)
		__field(int, task_type)
		__field(int, io_class)
		__field(sector_t, sector)
		__field(unsigned int, nr_bytes)
		__field(dev_t, dev)
	),

	TP_fast_assign(
		memcpy(__entry->comm, current->comm, TASK_COMM_LEN);
		__entry->pid = current->pid;
		__entry->task_type = task_type;
		__entry->io_class = io_class;
		__entry->sector = blk_rq_pos(rq);
		__entry->nr_bytes = blk_rq_bytes(rq);
		__entry->dev = rq->q->disk ? disk_devt(rq->q->disk) : 0;
	),

	TP_printk("task=%s pid=%d task_type=%s io_class=%s dev=%d,%d sector=%llu len=%u",
		__entry->comm, __entry->pid,
		show_task_type(__entry->task_type),
		show_ioprio_class(__entry->io_class),
		MAJOR(__entry->dev), MINOR(__entry->dev),
		(unsigned long long)__entry->sector, __entry->nr_bytes)
);

TRACE_EVENT(sdd_dispatch,

	TP_PROTO(struct request *rq, int task_type, int io_class),

	TP_ARGS(rq, task_type, io_class),

	TP_STRUCT__entry(
		__array(char, comm, TASK_COMM_LEN)
		__field(pid_t, pid)
		__field(int, task_type)
		__field(int, io_class)
		__field(sector_t, sector)
		__field(unsigned int, nr_bytes)
		__field(dev_t, dev)
	),

	TP_fast_assign(
		memcpy(__entry->comm, current->comm, TASK_COMM_LEN);
		__entry->pid = current->pid;
		__entry->task_type = task_type;
		__entry->io_class = io_class;
		__entry->sector = blk_rq_pos(rq);
		__entry->nr_bytes = blk_rq_bytes(rq);
		__entry->dev = rq->q->disk ? disk_devt(rq->q->disk) : 0;
	),

	TP_printk("task=%s pid=%d task_type=%s io_class=%s dev=%d,%d sector=%llu len=%u",
		__entry->comm, __entry->pid,
		show_task_type(__entry->task_type),
		show_ioprio_class(__entry->io_class),
		MAJOR(__entry->dev), MINOR(__entry->dev),
		(unsigned long long)__entry->sector, __entry->nr_bytes)
);

TRACE_EVENT(sdd_queue_work,

	TP_PROTO(int task_type, int pending_io_class, int work_type, int cpu),

	TP_ARGS(task_type, pending_io_class, work_type, cpu),

	TP_STRUCT__entry(
		__array(char, comm, TASK_COMM_LEN)
		__field(pid_t, pid)
		__field(int, task_type)
		__field(int, pending_io_class)
		__field(int, work_type)
		__field(int, cpu)
	),

	TP_fast_assign(
		memcpy(__entry->comm, current->comm, TASK_COMM_LEN);
		__entry->pid = current->pid;
		__entry->task_type = task_type;
		__entry->pending_io_class = pending_io_class;
		__entry->work_type = work_type;
		__entry->cpu = cpu;
	),

	TP_printk("task=%s pid=%d task_type=%s pending_io=%s work=%s cpu=%d",
		__entry->comm, __entry->pid,
		show_task_type(__entry->task_type),
		show_ioprio_class(__entry->pending_io_class),
		show_queue_work_type(__entry->work_type),
		__entry->cpu)
);

TRACE_EVENT(sdd_dispatch_timeout,

	TP_PROTO(struct request *rq, int task_type, int io_class, u64 jiffies),

	TP_ARGS(rq, task_type, io_class, jiffies),

	TP_STRUCT__entry(
		__array(char, comm, TASK_COMM_LEN)
		__field(pid_t, pid)
		__field(int, task_type)
		__field(int, io_class)
		__field(sector_t, sector)
		__field(unsigned int, nr_bytes)
		__field(dev_t, dev)
		__field(int, tag)
		__field(int, internal_tag)
		__field(u64, time_ms)
	),

	TP_fast_assign(
		memcpy(__entry->comm, current->comm, TASK_COMM_LEN);
		__entry->pid = current->pid;
		__entry->task_type = task_type;
		__entry->io_class = io_class;
		__entry->sector = blk_rq_pos(rq);
		__entry->nr_bytes = blk_rq_bytes(rq);
		__entry->dev = rq->q->disk ? disk_devt(rq->q->disk) : 0;
		__entry->tag = rq->tag;
		__entry->internal_tag = rq->internal_tag;
		__entry->time_ms = jiffies_to_msecs(jiffies);
	),

	TP_printk("task=%s pid=%d task_type=%s io_class=%s dev=%d,%d sector=%llu len=%u tag=%d itag=%d I2D=%llu",
		__entry->comm, __entry->pid,
		show_task_type(__entry->task_type),
		show_ioprio_class(__entry->io_class),
		MAJOR(__entry->dev), MINOR(__entry->dev),
		(unsigned long long)__entry->sector, __entry->nr_bytes,
		__entry->tag, __entry->internal_tag,
		__entry->time_ms)
);

#endif /* _TRACE_OPLUS_IO_SCHED_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH storage_feature_in_module/common/wq_dynamic_priority
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE oplus_io_sched_trace
#include <trace/define_trace.h>
