// SPDX-License-Identifier: GPL-2.0
/*
 * OPLUS IO Scheduler - based on mq-deadline
 *
 * Features:
 * - Priority-based dispatch (RT/BE/IDLE)
 * - Separate RT dispatch via kthread workers
 * - UX task awareness
 *
 * Copyright (C) OPLUS
 */

/* ============================================================
 * Section 1: Includes & Macros
 * ============================================================ */
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/compiler.h>
#include <linux/kthread.h>
#include <linux/rbtree.h>
#include <linux/sbitmap.h>
#include <linux/moduleparam.h>
#include <linux/sa_common.h>
#include <linux/blktrace_api.h>
#include <trace/events/block.h>

#include "elevator.h"
#include "blk.h"
#include "blk-mq.h"
#include "blk-mq-debugfs.h"
#include "blk-mq-sched.h"

#define CREATE_TRACE_POINTS
#include "oplus_io_sched_trace.h"
#include "oplus_wq_dynamic_priority.h"

/* ============================================================
 * Section 2: Data Structures & Global Variables
 * ============================================================ */
static DEFINE_MUTEX(blk_workers_mutex);
static atomic_t kthreads_ref = ATOMIC_INIT(0);
struct kthread_worker **blk_workers;


/* External variables from oplus_wq_dynamic_priority_core.c */
extern long ublk_sched;
extern long blk_sched;
extern bool kblockd_always_ux;
extern struct workqueue_struct *oplus_kblockd_wq;

/* Module parameters - sysfs created by oplus_wq_dynamic_priority_core.c */
bool dispatch_split = true;
bool debug_dispatch_split = false;

/* Time after which to dispatch lower priority requests */
static const int prio_aging_expire = HZ / 50;

/* ============================================================
 * Section 3: Priority & Statistics Definitions
 * ============================================================ */
enum dd_data_dir {
	DD_READ		= READ,
	DD_WRITE	= WRITE,
};

enum { DD_DIR_COUNT = 2 };

enum dd_prio {
	DD_RT_PRIO	= 0,
	DD_BE_PRIO	= 1,
	DD_IDLE_PRIO	= 2,
	DD_PRIO_MAX	= 2,
};

enum { DD_PRIO_COUNT = 3 };

/*
 * I/O statistics per I/O priority. It is fine if these counters overflow.
 * What matters is that these counters are at least as wide as
 * log2(max_outstanding_requests).
 */
struct io_stats_per_prio {
	uint32_t inserted;
	uint32_t merged;
	uint32_t dispatched;
	atomic_t completed;
};

/*
 * Deadline scheduler data per I/O priority (enum dd_prio). Requests are
 * present on both sort_list[] and fifo_list[].
 */
struct dd_per_prio {
	struct list_head dispatch;
	struct rb_root sort_list[DD_DIR_COUNT];
	struct list_head fifo_list;
	struct io_stats_per_prio stats;
	int lv;
};

struct simple_deadline_data {
	/*
	 * run time data
	 */
	struct request_queue *request_queue;
	struct dd_per_prio per_prio[DD_PRIO_COUNT];
	int front_merges;
	/*
	 * settings that change how the i/o scheduler behaves
	 */
	u32 async_depth;
	int prio_aging_expire;
	spinlock_t lock;
};

/* Maps an I/O priority class to a deadline scheduler priority. */
static const enum dd_prio ioprio_class_to_prio[] = {
	[IOPRIO_CLASS_NONE]	= DD_BE_PRIO,
	[IOPRIO_CLASS_RT]	= DD_RT_PRIO,
	[IOPRIO_CLASS_BE]	= DD_BE_PRIO,
	[IOPRIO_CLASS_IDLE]	= DD_IDLE_PRIO,
};

static inline struct rb_root *
deadline_rb_root(struct dd_per_prio *per_prio, struct request *rq)
{
	return &per_prio->sort_list[rq_data_dir(rq)];
}

/*
 * Returns the I/O priority class (IOPRIO_CLASS_*) that has been assigned to a
 * request.
 */
static u8 dd_rq_ioclass(struct request *rq)
{
	return IOPRIO_PRIO_CLASS(req_get_ioprio(rq));
}

static void
deadline_add_rq_rb(struct dd_per_prio *per_prio, struct request *rq)
{
	struct rb_root *root = deadline_rb_root(per_prio, rq);

	elv_rb_add(root, rq);
}

static inline void
deadline_del_rq_rb(struct dd_per_prio *per_prio, struct request *rq)
{
	elv_rb_del(deadline_rb_root(per_prio, rq), rq);
}

static inline int get_task_type(struct task_struct *task)
{
	if (rt_or_dl_task_policy(task))
		return TASK_TYPE_RT;
	else if (test_task_ux(task))
		return TASK_TYPE_UX;
	else
		return TASK_TYPE_CFS;
}
/*
 * remove rq from rbtree and fifo.
 */
static void deadline_remove_request(struct request_queue *q,
				    struct dd_per_prio *per_prio,
				    struct request *rq)
{
	list_del_init(&rq->queuelist);

	/*
	 * We might not be on the rbtree, if we are doing an insert merge
	 */
	if (!RB_EMPTY_NODE(&rq->rb_node))
		deadline_del_rq_rb(per_prio, rq);

	elv_rqhash_del(q, rq);
	if (q->last_merge == rq)
		q->last_merge = NULL;
}

static void sd_request_merged(struct request_queue *q, struct request *req,
			      enum elv_merge type)
{
	struct simple_deadline_data *dd = q->elevator->elevator_data;
	const u8 ioprio_class = dd_rq_ioclass(req);
	const enum dd_prio prio = ioprio_class_to_prio[ioprio_class];
	struct dd_per_prio *per_prio = &dd->per_prio[prio];

	/*
	 * if the merge was a front merge, we need to reposition request
	 */
	if (type == ELEVATOR_FRONT_MERGE) {
		elv_rb_del(deadline_rb_root(per_prio, req), req);
		deadline_add_rq_rb(per_prio, req);
	}
}

/*
 * Callback function that is invoked after @next has been merged into @req.
 */
static void sd_merged_requests(struct request_queue *q, struct request *req,
			       struct request *next)
{
	struct simple_deadline_data *dd = q->elevator->elevator_data;
	const u8 ioprio_class = dd_rq_ioclass(next);
	const enum dd_prio prio = ioprio_class_to_prio[ioprio_class];

	lockdep_assert_held(&dd->lock);

	dd->per_prio[prio].stats.merged++;

	/*
	 * if next expires before rq, assign its expire time to rq
	 * and move into next position (next will be deleted) in fifo
	 */
	if (!list_empty(&req->queuelist) && !list_empty(&next->queuelist)) {
		if (time_before((unsigned long)next->fifo_time,
				(unsigned long)req->fifo_time)) {
			list_move(&req->queuelist, &next->queuelist);
			req->fifo_time = next->fifo_time;
		}
	}

	/*
	 * kill knowledge of next, this one is a goner
	 */
	deadline_remove_request(q, &dd->per_prio[prio], next);
}

/*
 * move an entry to dispatch queue
 */
static void
deadline_move_request(struct simple_deadline_data *dd, struct dd_per_prio *per_prio,
		      struct request *rq)
{
	/*
	 * take it off the sort and fifo list
	 */
	deadline_remove_request(rq->q, per_prio, rq);
}

/* Number of requests queued for a given priority level. */
static u32 dd_queued(struct simple_deadline_data *dd, enum dd_prio prio)
{
	const struct io_stats_per_prio *stats = &dd->per_prio[prio].stats;

	lockdep_assert_held(&dd->lock);

	return stats->inserted - atomic_read(&stats->completed);
}

/* ============================================================
 * Section 4: Debugfs Support
 * ============================================================ */
#ifdef CONFIG_BLK_DEBUG_FS
#define DEADLINE_FIFO_ATTRS(prio, name)		\
static void *deadline_##name##_fifo_start(struct seq_file *m,		\
					  loff_t *pos)			\
	__acquires(&dd->lock)						\
{									\
	struct request_queue *q = m->private;				\
	struct simple_deadline_data *dd = q->elevator->elevator_data;		\
	struct dd_per_prio *per_prio = &dd->per_prio[prio];		\
									\
	spin_lock(&dd->lock);						\
	return seq_list_start(&per_prio->fifo_list, *pos);	\
}									\
									\
static void *deadline_##name##_fifo_next(struct seq_file *m, void *v,	\
					 loff_t *pos)			\
{									\
	struct request_queue *q = m->private;				\
	struct simple_deadline_data *dd = q->elevator->elevator_data;		\
	struct dd_per_prio *per_prio = &dd->per_prio[prio];		\
									\
	return seq_list_next(v, &per_prio->fifo_list, pos);	\
}									\
									\
static void deadline_##name##_fifo_stop(struct seq_file *m, void *v)	\
	__releases(&dd->lock)						\
{									\
	struct request_queue *q = m->private;				\
	struct simple_deadline_data *dd = q->elevator->elevator_data;		\
									\
	spin_unlock(&dd->lock);						\
}									\
									\
static const struct seq_operations deadline_##name##_fifo_seq_ops = {	\
	.start	= deadline_##name##_fifo_start,				\
	.next	= deadline_##name##_fifo_next,				\
	.stop	= deadline_##name##_fifo_stop,				\
	.show	= blk_mq_debugfs_rq_show,				\
};
DEADLINE_FIFO_ATTRS(DD_RT_PRIO, rt);
DEADLINE_FIFO_ATTRS(DD_BE_PRIO, be);
DEADLINE_FIFO_ATTRS(DD_IDLE_PRIO, idle);

#undef DEADLINE_FIFO_ATTRS

static int dd_async_depth_show(void *data, struct seq_file *m)
{
	struct request_queue *q = data;
	struct simple_deadline_data *dd = q->elevator->elevator_data;

	seq_printf(m, "%u\n", dd->async_depth);
	return 0;
}

static int dd_queued_show(void *data, struct seq_file *m)
{
	struct request_queue *q = data;
	struct simple_deadline_data *dd = q->elevator->elevator_data;
	u32 rt, be, idle;

	spin_lock(&dd->lock);
	rt = dd_queued(dd, DD_RT_PRIO);
	be = dd_queued(dd, DD_BE_PRIO);
	idle = dd_queued(dd, DD_IDLE_PRIO);
	spin_unlock(&dd->lock);

	seq_printf(m, "%u %u %u\n", rt, be, idle);

	return 0;
}

/* Number of requests owned by the block driver for a given priority. */
static u32 dd_owned_by_driver(struct simple_deadline_data *dd, enum dd_prio prio)
{
	const struct io_stats_per_prio *stats = &dd->per_prio[prio].stats;

	lockdep_assert_held(&dd->lock);

	return stats->dispatched + stats->merged -
		atomic_read(&stats->completed);
}

static int dd_owned_by_driver_show(void *data, struct seq_file *m)
{
	struct request_queue *q = data;
	struct simple_deadline_data *dd = q->elevator->elevator_data;
	u32 rt, be, idle;

	spin_lock(&dd->lock);
	rt = dd_owned_by_driver(dd, DD_RT_PRIO);
	be = dd_owned_by_driver(dd, DD_BE_PRIO);
	idle = dd_owned_by_driver(dd, DD_IDLE_PRIO);
	spin_unlock(&dd->lock);

	seq_printf(m, "%u %u %u\n", rt, be, idle);

	return 0;
}

#define DEADLINE_DISPATCH_ATTR(prio)					\
static void *deadline_dispatch##prio##_start(struct seq_file *m,	\
					     loff_t *pos)		\
	__acquires(&dd->lock)						\
{									\
	struct request_queue *q = m->private;				\
	struct simple_deadline_data *dd = q->elevator->elevator_data;		\
	struct dd_per_prio *per_prio = &dd->per_prio[prio];		\
									\
	spin_lock(&dd->lock);						\
	return seq_list_start(&per_prio->dispatch, *pos);		\
}									\
									\
static void *deadline_dispatch##prio##_next(struct seq_file *m,		\
					    void *v, loff_t *pos)	\
{									\
	struct request_queue *q = m->private;				\
	struct simple_deadline_data *dd = q->elevator->elevator_data;		\
	struct dd_per_prio *per_prio = &dd->per_prio[prio];		\
									\
	return seq_list_next(v, &per_prio->dispatch, pos);		\
}									\
									\
static void deadline_dispatch##prio##_stop(struct seq_file *m, void *v)	\
	__releases(&dd->lock)						\
{									\
	struct request_queue *q = m->private;				\
	struct simple_deadline_data *dd = q->elevator->elevator_data;		\
									\
	spin_unlock(&dd->lock);						\
}									\
									\
static const struct seq_operations deadline_dispatch##prio##_seq_ops = { \
	.start	= deadline_dispatch##prio##_start,			\
	.next	= deadline_dispatch##prio##_next,			\
	.stop	= deadline_dispatch##prio##_stop,			\
	.show	= blk_mq_debugfs_rq_show,				\
}

DEADLINE_DISPATCH_ATTR(0);
DEADLINE_DISPATCH_ATTR(1);
DEADLINE_DISPATCH_ATTR(2);
#undef DEADLINE_DISPATCH_ATTR

#define DEADLINE_QUEUE_ATTRS(name)					\
	{#name "_fifo_list", 0400,					\
			.seq_ops = &deadline_##name##_fifo_seq_ops}

static const struct blk_mq_debugfs_attr sd_queue_debugfs_attrs[] = {
	DEADLINE_QUEUE_ATTRS(rt),
	DEADLINE_QUEUE_ATTRS(be),
	DEADLINE_QUEUE_ATTRS(idle),
	{"async_depth", 0400, dd_async_depth_show},
	{"dispatch_rt", 0400, .seq_ops = &deadline_dispatch0_seq_ops},
	{"dispatch_be", 0400, .seq_ops = &deadline_dispatch1_seq_ops},
	{"dispatch_idle", 0400, .seq_ops = &deadline_dispatch2_seq_ops},
	{"owned_by_driver", 0400, dd_owned_by_driver_show},
	{"queued", 0400, dd_queued_show},
	{},
};
#undef DEADLINE_QUEUE_ATTRS
#endif

/* ============================================================
 * Section 5: Sysfs Attributes
 * ============================================================ */
#define SHOW_INT(__FUNC, __VAR)						\
static ssize_t __FUNC(struct elevator_queue *e, char *page)		\
{									\
	struct simple_deadline_data *dd = e->elevator_data;			\
									\
	return sysfs_emit(page, "%d\n", __VAR);				\
}
#define SHOW_JIFFIES(__FUNC, __VAR) SHOW_INT(__FUNC, jiffies_to_msecs(__VAR))
SHOW_JIFFIES(deadline_prio_aging_expire_show, dd->prio_aging_expire);
SHOW_INT(deadline_front_merges_show, dd->front_merges);
SHOW_INT(deadline_async_depth_show, dd->async_depth);
#undef SHOW_INT
#undef SHOW_JIFFIES

#define STORE_FUNCTION(__FUNC, __PTR, MIN, MAX, __CONV)			\
static ssize_t __FUNC(struct elevator_queue *e, const char *page, size_t count)	\
{									\
	struct simple_deadline_data *dd = e->elevator_data;			\
	int __data, __ret;						\
									\
	__ret = kstrtoint(page, 0, &__data);				\
	if (__ret < 0)							\
		return __ret;						\
	if (__data < (MIN))						\
		__data = (MIN);						\
	else if (__data > (MAX))					\
		__data = (MAX);						\
	*(__PTR) = __CONV(__data);					\
	return count;							\
}
#define STORE_INT(__FUNC, __PTR, MIN, MAX)				\
	STORE_FUNCTION(__FUNC, __PTR, MIN, MAX, )
#define STORE_JIFFIES(__FUNC, __PTR, MIN, MAX)				\
	STORE_FUNCTION(__FUNC, __PTR, MIN, MAX, msecs_to_jiffies)
STORE_JIFFIES(deadline_prio_aging_expire_store, &dd->prio_aging_expire, 0, INT_MAX);
STORE_INT(deadline_front_merges_store, &dd->front_merges, 0, 1);
STORE_INT(deadline_async_depth_store, &dd->async_depth, 1, INT_MAX);
#undef STORE_FUNCTION
#undef STORE_INT
#undef STORE_JIFFIES

#define DD_ATTR(name) \
	__ATTR(name, 0644, deadline_##name##_show, deadline_##name##_store)

static struct elv_fs_entry sd_attrs[] = {
	DD_ATTR(prio_aging_expire),
	DD_ATTR(front_merges),
	DD_ATTR(async_depth),
	__ATTR_NULL
};


/* ============================================================
 * Section 6: Request Dispatch Logic
 * ============================================================ */
static bool sd_has_work_for_prio(struct dd_per_prio *per_prio);

/*
 * For the specified data direction, return the next request to
 * dispatch using arrival ordered lists.
 */
static struct request *
deadline_fifo_request(struct dd_per_prio *per_prio)
{
	struct request *rq = NULL;

	if (!list_empty(&per_prio->fifo_list))
		rq = rq_entry_fifo(per_prio->fifo_list.next);

	return rq;
}

/*
 * Returns true if and only if @rq started after @latest_start where
 * @latest_start is in jiffies.
 */
static bool started_after(struct request *rq, unsigned long latest_start)
{
	return time_after((unsigned long)rq->fifo_time, latest_start);
}

/*
 * deadline_dispatch_requests selects the best request according to
 * read/write expire, fifo_batch, etc and with a start time <= @latest_start.
 */
static struct request *__sd_dispatch_request(struct simple_deadline_data *dd,
					     struct dd_per_prio *per_prio,
					     unsigned long latest_start)
{

	struct request *rq = NULL;
	u8 ioprio_class;
	u64 cost_time;
	enum dd_prio prio;

	lockdep_assert_held(&dd->lock);

	if (!list_empty(&per_prio->dispatch)) {
		rq = list_first_entry(&per_prio->dispatch, struct request,
  				      queuelist);

		if (started_after(rq, latest_start))
			return NULL;

		list_del_init(&rq->queuelist);
		goto done;
	}

	rq = deadline_fifo_request(per_prio);

	if (!rq || started_after(rq, latest_start))
		return NULL;

	deadline_move_request(dd, per_prio, rq);
done:
	ioprio_class  = dd_rq_ioclass(rq);
	prio = ioprio_class_to_prio[ioprio_class];
	dd->per_prio[prio].stats.dispatched++;
	rq->rq_flags |= RQF_STARTED;
	trace_sdd_dispatch(rq, get_task_type(current), ioprio_class);
	cost_time = jiffies - rq->fifo_time;

	if (cost_time > HZ / 100)
		trace_sdd_dispatch_timeout(rq, get_task_type(current), ioprio_class, cost_time);

	return rq;
}

/*
 * Check whether there are any requests with priority other than DD_RT_PRIO
 * that were inserted more than prio_aging_expire jiffies ago.
 */
static struct request *dd_dispatch_prio_aged_requests(struct simple_deadline_data *dd,
						      unsigned long now)
{
	struct request *rq;
	enum dd_prio prio;
	int prio_cnt;

	lockdep_assert_held(&dd->lock);

	prio_cnt = !!dd_queued(dd, DD_RT_PRIO) + !!dd_queued(dd, DD_BE_PRIO) +
		   !!dd_queued(dd, DD_IDLE_PRIO);
	if (prio_cnt < 2)
		return NULL;

	for (prio = DD_BE_PRIO; prio <= DD_PRIO_MAX; prio++) {
		rq = __sd_dispatch_request(dd, &dd->per_prio[prio],
					   now - dd->prio_aging_expire);
		if (rq)
			return rq;
	}

	return NULL;
}

bool sd_has_work_for_prioclass(struct blk_mq_hw_ctx *hctx, u8 ioprio_class);

static inline int blk_mq_first_mapped_cpu(struct blk_mq_hw_ctx *hctx)
{
	int cpu = cpumask_first_and(hctx->cpumask, cpu_online_mask);

	if (cpu >= nr_cpu_ids)
		cpu = cpumask_first(hctx->cpumask);
	return cpu;
}

static inline int blk_mq_hctx_next_cpu(struct blk_mq_hw_ctx *hctx)
{
	int next_cpu = hctx->next_cpu;

	if (hctx->queue->nr_hw_queues == 1)
		return WORK_CPU_UNBOUND;

	next_cpu = cpumask_next_and(next_cpu, hctx->cpumask, cpu_online_mask);

	if (next_cpu >= nr_cpu_ids)
 		next_cpu = blk_mq_first_mapped_cpu(hctx);

	if (unlikely(!cpu_online(next_cpu)))
		next_cpu  = WORK_CPU_UNBOUND;

	return next_cpu;
}

static struct request *sd_dispatch_request(struct blk_mq_hw_ctx *hctx)
{
	struct simple_deadline_data *dd = hctx->queue->elevator->elevator_data;
	unsigned long now;
	struct request *rq = NULL;
	struct request_queue *q = hctx->queue;
	struct hctx_sched_entry *entry;
	enum dd_prio prio;
	int cpu;

	spin_lock(&dd->lock);
	now = jiffies;

	/* Handle aged requests first */
	rq = dd_dispatch_prio_aged_requests(dd, now);
	if (rq)
		goto unlock;

	/* Bypass dispatch_split for testing or when kblockd_always_ux is set */
	if (unlikely(!dispatch_split) || kblockd_always_ux) {
		prio = DD_RT_PRIO;
		goto direct_dispatch;
	}

	if (rt_or_dl_task_policy(current) || test_task_ux(current)) {
		/*
		 * RT/UX thread: dispatch all priorities (RT -> BE -> IDLE)
		 * This thread can handle everything efficiently.
		 */
		prio = DD_RT_PRIO;
		goto direct_dispatch;

	} else {
		/*
		 * Normal thread: never dispatch RT IO directly.
		 * If RT IO exists, delegate everything to high priority thread.
		 */
		if (sd_has_work_for_prioclass(hctx, IOPRIO_CLASS_RT)) {
			cpu = raw_smp_processor_id();
			entry = (struct hctx_sched_entry *)q->android_oem_data1;
			if (q->nr_hw_queues > 1)
				entry = &entry[cpu];

			ublk_sched++;
			trace_sdd_queue_work(get_task_type(current), IOPRIO_CLASS_RT,
					     QUEUE_WORK_KTHREAD, cpu);

			/* Delegate to high priority thread for RT IO priority */
			if (q->nr_hw_queues <= 1)
				mod_delayed_work_on(cpu, oplus_kblockd_wq, &entry->ux_dwork, 0);
			else
				kthread_mod_delayed_work(blk_workers[cpu], &entry->dwork, 0);

			/* Return immediately, let high priority thread handle all IO */
			goto unlock;
		}

		/* No RT IO, normal thread dispatches BE/IDLE */
		prio = DD_BE_PRIO;
		goto direct_dispatch;
	}

direct_dispatch:
	for (; prio <= DD_PRIO_MAX; prio++) {
		rq = __sd_dispatch_request(dd, &dd->per_prio[prio], now);
		if (rq || dd_queued(dd, prio))
			break;
	}

	/* Debug check: normal thread should never dispatch RT IO */
	if (unlikely(debug_dispatch_split) && rq) {
		if (!rt_or_dl_task_policy(current) && !test_task_ux(current) &&
		    IOPRIO_PRIO_CLASS(req_get_ioprio(rq)) == IOPRIO_CLASS_RT) {
			pr_err("%s err: normal thread %s dispatched RT IO\n",
			       KBUILD_MODNAME, current->comm);
		}
	}

unlock:
	spin_unlock(&dd->lock);
	return rq;
}
/*
 * 'depth' is a number in the range 1..INT_MAX representing a number of
 * requests. Scale it with a factor (1 << bt->sb.shift) / q->nr_requests since
 * 1..(1 << bt->sb.shift) is the range expected by sbitmap_get_shallow().
 * Values larger than q->nr_requests have the same effect as q->nr_requests.
 */
static int dd_to_word_depth(struct blk_mq_hw_ctx *hctx, unsigned int qdepth)
{
	struct sbitmap_queue *bt = &hctx->sched_tags->bitmap_tags;
	const unsigned int nrr = hctx->queue->nr_requests;

	return ((qdepth << bt->sb.shift) + nrr - 1) / nrr;
}

/*
 * Called by __blk_mq_alloc_request(). The shallow_depth value set by this
 * function is used by __blk_mq_get_tag().
 */
static void sd_limit_depth(blk_opf_t opf, struct blk_mq_alloc_data *data)
{
	struct simple_deadline_data *dd = data->q->elevator->elevator_data;

	/* Do not throttle synchronous reads. */
	if (op_is_sync(opf) && !op_is_write(opf))
		return;

	/*
	 * Throttle asynchronous requests and writes such that these requests
	 * do not block the allocation of synchronous requests.
	 */
	data->shallow_depth = dd_to_word_depth(data->hctx, dd->async_depth);
}

/* Called by blk_mq_update_nr_requests(). */
static void sd_depth_updated(struct blk_mq_hw_ctx *hctx)
{
	struct request_queue *q = hctx->queue;
	struct simple_deadline_data *dd = q->elevator->elevator_data;
	struct blk_mq_tags *tags = hctx->sched_tags;

	dd->async_depth = q->nr_requests;

	sbitmap_queue_min_shallow_depth(&tags->bitmap_tags, 1);
}

/* Called by blk_mq_init_hctx() and blk_mq_init_sched(). */
static int sd_init_hctx(struct blk_mq_hw_ctx *hctx, unsigned int hctx_idx)
{
	sd_depth_updated(hctx);
	return 0;
}

/* ============================================================
 * Section 7: Kthread Workers Management
 * ============================================================ */
static void oplus_blk_mq_thread_work(struct kthread_work *work)
{
	struct hctx_sched_entry *entry;

	current->flags |= PF_MEMALLOC_NOIO;
	entry = container_of(work, struct hctx_sched_entry, dwork.work);

	/*
	 * If we are stopped, don't run the queue.
	 */
	if (test_bit(BLK_MQ_S_STOPPED, &entry->hctx->state))
		return;

	blk_mq_run_hw_queue(entry->hctx, false);
}

/* Work function for high priority workqueue (single queue devices) */
static void oplus_blk_mq_ux_work(struct work_struct *work)
{
	struct hctx_sched_entry *entry;

	current->flags |= PF_MEMALLOC_NOIO;
	entry = container_of(work, struct hctx_sched_entry, ux_dwork.work);

	/*
	 * If we are stopped, don't run the queue.
	 */
	if (test_bit(BLK_MQ_S_STOPPED, &entry->hctx->state))
		return;

	blk_mq_run_hw_queue(entry->hctx, false);
}
static void oplus_blk_workers_put(void)
{
    int i;

    if (atomic_dec_and_test(&kthreads_ref)) {
		for_each_possible_cpu(i) {
			if (blk_workers[i] && !IS_ERR(blk_workers[i])) {
				kthread_destroy_worker(blk_workers[i]);
				blk_workers[i] = NULL;
			}
		}
		kfree(blk_workers);
		blk_workers = NULL;
	}
}

static int oplus_blk_workers_get(void)
{
    int i;
    struct kthread_worker *worker;

    if (atomic_inc_return(&kthreads_ref) == 1){
		blk_workers = kzalloc(num_possible_cpus() * sizeof(struct kthread_worker *), GFP_KERNEL);
		if (!blk_workers) {
			pr_err("alloc blk_workers array failed\n");
			atomic_dec(&kthreads_ref);
			return -ENOMEM;
		}

		for_each_possible_cpu(i) {
			worker = kthread_create_worker_on_cpu(i, 0, "blk_run_queue%d", i);
			if (IS_ERR(worker)) {
				int err = PTR_ERR(worker);
				pr_err("create blk_worker for cpu%d failed: %d\n", i, err);
				oplus_blk_workers_put();
				return err;
			}
			sched_set_fifo_low(worker->task);
			blk_workers[i] = worker;
		}
	}

    return 0;
}

/* ============================================================
 * Section 8: Elevator Init/Exit
 * ============================================================ */
static void sd_exit_sched(struct elevator_queue *e)
{
	struct simple_deadline_data *dd = e->elevator_data;
	struct request_queue *q = dd->request_queue;
	enum dd_prio prio;

	for (prio = 0; prio <= DD_PRIO_MAX; prio++) {
		struct dd_per_prio *per_prio = &dd->per_prio[prio];
		const struct io_stats_per_prio *stats = &per_prio->stats;
		uint32_t queued;

		WARN_ON_ONCE(!list_empty(&per_prio->fifo_list));

		spin_lock(&dd->lock);
		queued = dd_queued(dd, prio);
		spin_unlock(&dd->lock);

		WARN_ONCE(queued != 0,
			  "statistics for priority %d: i %u m %u d %u c %u\n",
			  prio, stats->inserted, stats->merged,
			  stats->dispatched, atomic_read(&stats->completed));
	}
	mutex_lock(&blk_workers_mutex);
	oplus_blk_workers_put();
	mutex_unlock(&blk_workers_mutex);
	kfree((struct hctx_sched_entry *)q->android_oem_data1);
	q->android_oem_data1 = 0;
	kfree(dd);
}

/*
 * initialize elevator private data (simple_deadline_data).
 */
static int sd_init_sched(struct request_queue *q, struct elevator_type *e)
{
	struct simple_deadline_data *dd;
	struct elevator_queue *eq;
	enum dd_prio prio;
	int i;
	struct hctx_sched_entry *entry;
	int ret = -ENOMEM;

	eq = elevator_alloc(q, e);
	if (!eq)
		return ret;

	dd = kzalloc_node(sizeof(*dd), GFP_KERNEL, q->node);
	if (!dd)
		goto put_eq;

	eq->elevator_data = dd;

	for (prio = 0; prio <= DD_PRIO_MAX; prio++) {
		struct dd_per_prio *per_prio = &dd->per_prio[prio];
		INIT_LIST_HEAD(&per_prio->dispatch);
		INIT_LIST_HEAD(&per_prio->fifo_list);
		per_prio->sort_list[DD_READ] = RB_ROOT;
		per_prio->sort_list[DD_WRITE] = RB_ROOT;
		per_prio->lv = prio;
	}

	dd->prio_aging_expire = prio_aging_expire;
	dd->front_merges = 1;
	dd->request_queue = q;

	spin_lock_init(&dd->lock);

	/* We dispatch from request queue wide instead of hw queue */
	blk_queue_flag_set(QUEUE_FLAG_SQ_SCHED, q);
	entry = (struct hctx_sched_entry *)q->android_oem_data1;
	if (unlikely(!entry)) {
		q->android_oem_data1 = (u64)kzalloc(sizeof(struct hctx_sched_entry) * q->nr_hw_queues, GFP_KERNEL);
		if (!q->android_oem_data1)
			goto put_eq;

		mutex_lock(&blk_workers_mutex);
		ret = oplus_blk_workers_get();
		mutex_unlock(&blk_workers_mutex);

		if (ret)
			goto free_q_oem_data;

		entry = (struct hctx_sched_entry *)q->android_oem_data1;
		for (i = 0; i < q->nr_hw_queues; i++) {
			entry[i].hctx = xa_load(&q->hctx_table, i);
			/* Single queue: use ux_dwork, multi-queue: use dwork */
			if (q->nr_hw_queues <= 1)
				INIT_DELAYED_WORK(&entry[i].ux_dwork, oplus_blk_mq_ux_work);
			else
				kthread_init_delayed_work(&entry[i].dwork, oplus_blk_mq_thread_work);
		}
	}

	set_bit(ELEVATOR_F_DISPATCH_SEP_BIT, &eq->flags);
	q->elevator = eq;
	pr_info("switch oplus_io_sched done");
	return 0;

free_q_oem_data:
	kfree((struct hctx_sched_entry *)q->android_oem_data1);
	q->android_oem_data1 = 0;
	kfree(dd);
put_eq:
	kobject_put(&eq->kobj);
	return ret;
}

/* ============================================================
 * Section 9: Request Insert/Merge/Complete
 * ============================================================ */

/*
 * Try to merge @bio into an existing request. If @bio has been merged into
 * an existing request, store the pointer to that request into *@rq.
 */
static int sd_request_merge(struct request_queue *q, struct request **rq,
			    struct bio *bio)
{
	struct simple_deadline_data *dd = q->elevator->elevator_data;
	const u8 ioprio_class = IOPRIO_PRIO_CLASS(bio->bi_ioprio);
	const enum dd_prio prio = ioprio_class_to_prio[ioprio_class];
	struct dd_per_prio *per_prio = &dd->per_prio[prio];
	sector_t sector = bio_end_sector(bio);
	struct request *__rq;

	if (!dd->front_merges)
		return ELEVATOR_NO_MERGE;

	__rq = elv_rb_find(&per_prio->sort_list[bio_data_dir(bio)], sector);
	if (__rq) {
		BUG_ON(sector != blk_rq_pos(__rq));

		if (elv_bio_merge_ok(__rq, bio)) {
			*rq = __rq;
			if (blk_discard_mergable(__rq))
				return ELEVATOR_DISCARD_MERGE;
			return ELEVATOR_FRONT_MERGE;
		}
	}

	return ELEVATOR_NO_MERGE;
}

/*
 * Attempt to merge a bio into an existing request. This function is called
 * before @bio is associated with a request.
 */
static bool sd_bio_merge(struct request_queue *q, struct bio *bio,
		unsigned int nr_segs)
{
	struct simple_deadline_data *dd = q->elevator->elevator_data;
	struct request *free = NULL;
	bool ret;

	spin_lock(&dd->lock);
	ret = blk_mq_sched_try_merge(q, bio, nr_segs, &free);
	spin_unlock(&dd->lock);

	if (free)
		blk_mq_free_request(free);

	return ret;
}

/*
 * add rq to rbtree and fifo
 */
static void sd_insert_request(struct blk_mq_hw_ctx *hctx, struct request *rq,
			      blk_insert_t flags, struct list_head *free)
{
	struct request_queue *q = hctx->queue;
	struct simple_deadline_data *dd = q->elevator->elevator_data;
	u16 ioprio = req_get_ioprio(rq);
	u8 ioprio_class = IOPRIO_PRIO_CLASS(ioprio);
	struct dd_per_prio *per_prio;
	enum dd_prio prio;

	lockdep_assert_held(&dd->lock);

	prio = ioprio_class_to_prio[ioprio_class];
	per_prio = &dd->per_prio[prio];
	if (!rq->elv.priv[0])
		per_prio->stats.inserted++;
	rq->elv.priv[0] = per_prio;

	if (blk_mq_sched_try_insert_merge(q, rq, free))
		return;


	trace_block_rq_insert(rq);
	trace_sdd_insert(rq, get_task_type(current), ioprio_class);

	if (flags & BLK_MQ_INSERT_AT_HEAD) {
		list_add(&rq->queuelist, &per_prio->dispatch);
		rq->fifo_time = jiffies;
	} else {
		deadline_add_rq_rb(per_prio, rq);

		if (rq_mergeable(rq)) {
			elv_rqhash_add(q, rq);
			if (!q->last_merge)
				q->last_merge = rq;
		}

		/*
		 * set expire time and add to fifo list
		 */
		rq->fifo_time = jiffies;
		list_add_tail(&rq->queuelist, &per_prio->fifo_list);

	}
}

/*
 * Called from blk_mq_insert_request() or blk_mq_dispatch_plug_list().
 */
static void sd_insert_requests(struct blk_mq_hw_ctx *hctx,
			       struct list_head *list,
			       blk_insert_t flags)
{
	struct request_queue *q = hctx->queue;
	struct simple_deadline_data *dd = q->elevator->elevator_data;
	LIST_HEAD(free);

	spin_lock(&dd->lock);
	while (!list_empty(list)) {
		struct request *rq;

		rq = list_first_entry(list, struct request, queuelist);
		list_del_init(&rq->queuelist);
		sd_insert_request(hctx, rq, flags, &free);
	}
	spin_unlock(&dd->lock);

	blk_mq_free_requests(&free);
}

/* Callback from inside blk_mq_rq_ctx_init(). */
static void sd_prepare_request(struct request *rq)
{
	rq->elv.priv[0] = NULL;
}

/*
 * Callback from inside blk_mq_free_request().
 *
 * For zoned block devices, write unlock the target zone of
 * completed write requests. Do this while holding the zone lock
 * spinlock so that the zone is never unlocked while deadline_fifo_request()
 * or deadline_next_request() are executing. This function is called for
 * all requests, whether or not these requests complete successfully.
 *
 * For a zoned block device, __sd_dispatch_request() may have stopped
 * dispatching requests if all the queued requests are write requests directed
 * at zones that are already locked due to on-going write requests. To ensure
 * write request dispatch progress in this case, mark the queue as needing a
 * restart to ensure that the queue is run again after completion of the
 * request and zones being unlocked.
 */
static void sd_finish_request(struct request *rq)
{
	struct dd_per_prio *per_prio = rq->elv.priv[0];
	/*
	* The block layer core may call dd_finish_request() without having
	* called dd_insert_requests(). Skip requests that bypassed I/O
	* scheduling. See also blk_mq_request_bypass_insert().
	*/
	if (per_prio)
		atomic_inc(&per_prio->stats.completed);

}

static bool sd_has_work_for_prio(struct dd_per_prio *per_prio)
{
	return !list_empty_careful(&per_prio->dispatch) ||
		!list_empty_careful(&per_prio->fifo_list);
}

bool sd_has_work_for_prioclass(struct blk_mq_hw_ctx *hctx, u8 ioprio_class)
{
	const enum dd_prio prio = ioprio_class_to_prio[ioprio_class];
	struct simple_deadline_data *dd = hctx->queue->elevator->elevator_data;

	return sd_has_work_for_prio(&dd->per_prio[prio]);
}

static bool sd_has_work(struct blk_mq_hw_ctx *hctx)
{
	struct simple_deadline_data *dd = hctx->queue->elevator->elevator_data;
	enum dd_prio prio;

	for (prio = 0; prio <= DD_PRIO_MAX; prio++)
		if (sd_has_work_for_prio(&dd->per_prio[prio]))
			return true;

	return false;
}
/* ============================================================
 * Section 10: Elevator Registration
 * ============================================================ */
static struct elevator_type simple_deadline = {
	.ops = {
		.depth_updated		= sd_depth_updated,
		.limit_depth		= sd_limit_depth,
		.insert_requests	= sd_insert_requests,
		.dispatch_request	= sd_dispatch_request,
		.prepare_request	= sd_prepare_request,
		.finish_request		= sd_finish_request,
		.next_request		= elv_rb_latter_request,
		.former_request		= elv_rb_former_request,
		.bio_merge		= sd_bio_merge,
		.request_merge		= sd_request_merge,
		.requests_merged	= sd_merged_requests,
		.request_merged		= sd_request_merged,
		.has_work		= sd_has_work,
		.init_sched		= sd_init_sched,
		.exit_sched		= sd_exit_sched,
		.init_hctx		= sd_init_hctx,
	},

#ifdef CONFIG_BLK_DEBUG_FS
	.queue_debugfs_attrs = sd_queue_debugfs_attrs,
#endif
	.elevator_attrs = sd_attrs,
	.elevator_name = "oplus_io_sched",
	.elevator_alias = "simple_deadline",
	.elevator_owner = THIS_MODULE,
};

int simple_deadline_init(void)
{
	return elv_register(&simple_deadline);
}

void simple_deadline_exit(void)
{
	elv_unregister(&simple_deadline);
}
