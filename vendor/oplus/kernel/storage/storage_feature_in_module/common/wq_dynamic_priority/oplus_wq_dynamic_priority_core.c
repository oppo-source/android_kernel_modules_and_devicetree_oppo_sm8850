// SPDX-License-Identifier: GPL-2.0
/*
 * OPLUS Workqueue Dynamic Priority & Block Layer Optimization
 *
 * Copyright (C) OPLUS
 */
#include <linux/version.h>

#if LINUX_VERSION_CODE > KERNEL_VERSION(6, 12, 0)

/* ============================================================
 * Section 1: Includes & Macros
 * ============================================================ */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/ioprio.h>
#include <linux/blk-mq.h>
#include <linux/reboot.h>
#include <linux/atomic.h>
#include <uapi/linux/major.h>
#include <trace/hooks/wqlockup.h>
#include <trace/hooks/blk.h>
#include <trace/events/block.h>
#include "elevator.h"
#include "dm-verity.h"
#include "dm-verity-fec.h"
#include "oplus_wq_dynamic_priority.h"
#include <linux/sa_common.h>

#define VIRTUAL_KWORKER_NORMAL_NICE	(-1000)
#define VIRTUAL_KWORKER_KBLOCKD_NICE	(-1001)
#define WQ_CMP(str)			(strncmp(wq->name, str, sizeof(str) - 1) == 0)

/* ============================================================
 * Section 2: Module Parameters & Statistics
 * ============================================================ */
static struct workqueue_attrs *normal_ux_attrs;
static struct workqueue_attrs *kblockd_ux_attrs;
struct workqueue_struct *oplus_kverityd_wq;
struct workqueue_struct *oplus_kblockd_wq;
static void *orig_verity_prefetch_io;
static void (*orig_verity_fec_finish_io)(struct dm_verity_io *io);
static struct work_struct verity_kp_register_work;
static atomic_t verity_work_found = ATOMIC_INIT(0);
static void verity_kp_register_workfn(struct work_struct *work);

/* Block dispatch counters */
unsigned long blk_sched;
unsigned long ublk_sched;

/* Verity counters */
static atomic_long_t kverity_cnt = ATOMIC_LONG_INIT(0);
static atomic_long_t kverity_ux_cnt = ATOMIC_LONG_INIT(0);

/* Feature switches */
static bool kverify_always_ux = true;
bool kblockd_always_ux = true;

/* Dispatch split switches - defined in oplus_io_sched.c */
extern bool dispatch_split;
extern bool debug_dispatch_split;

/* Sched latency stats - ALL requests */
static unsigned long lat_all_cnt;
static unsigned long lat_all_10ms_cnt;

/* Sched latency stats - RT IO only */
static unsigned long lat_rt_cnt;
static unsigned long lat_rt_10ms_cnt;

/* ============================================================
 * Section 2.1: Sysfs Attributes (dynamically created for v2)
 * ============================================================ */
static struct kobject *wq_stats_kobj;

/* Macro for unsigned long type sysfs attribute */
#define DEFINE_ULONG_ATTR(name)						\
static ssize_t name##_show(struct kobject *kobj,			\
			   struct kobj_attribute *attr, char *buf)	\
{									\
	return sprintf(buf, "%lu\n", name);				\
}									\
static ssize_t name##_store(struct kobject *kobj,			\
			    struct kobj_attribute *attr,		\
			    const char *buf, size_t count)		\
{									\
	int ret = kstrtoul(buf, 10, &name);				\
	return ret ? ret : count;					\
}									\
static struct kobj_attribute name##_attr = __ATTR_RW(name)

/* Macro for atomic_long_t type sysfs attribute */
#define DEFINE_ATOMIC_LONG_ATTR(name)					\
static ssize_t name##_show(struct kobject *kobj,			\
			   struct kobj_attribute *attr, char *buf)	\
{									\
	return sprintf(buf, "%ld\n", atomic_long_read(&name));		\
}									\
static ssize_t name##_store(struct kobject *kobj,			\
			    struct kobj_attribute *attr,		\
			    const char *buf, size_t count)		\
{									\
	long val;							\
	int ret = kstrtol(buf, 10, &val);				\
	if (ret)							\
		return ret;						\
	atomic_long_set(&name, val);					\
	return count;							\
}									\
static struct kobj_attribute name##_attr = __ATTR_RW(name)

/* Macro for bool type sysfs attribute */
#define DEFINE_BOOL_ATTR(name)						\
static ssize_t name##_show(struct kobject *kobj,			\
			   struct kobj_attribute *attr, char *buf)	\
{									\
	return sprintf(buf, "%c\n", name ? 'Y' : 'N');			\
}									\
static ssize_t name##_store(struct kobject *kobj,			\
			    struct kobj_attribute *attr,		\
			    const char *buf, size_t count)		\
{									\
	if (count > 0) {						\
		if (buf[0] == 'Y' || buf[0] == 'y' || buf[0] == '1')	\
			name = true;					\
		else if (buf[0] == 'N' || buf[0] == 'n' || buf[0] == '0') \
			name = false;					\
		else							\
			return -EINVAL;					\
	}								\
	return count;							\
}									\
static struct kobj_attribute name##_attr = __ATTR_RW(name)

/* Define all sysfs attributes using macros */
DEFINE_ULONG_ATTR(blk_sched);
DEFINE_ULONG_ATTR(ublk_sched);
DEFINE_ATOMIC_LONG_ATTR(kverity_cnt);
DEFINE_ATOMIC_LONG_ATTR(kverity_ux_cnt);
DEFINE_BOOL_ATTR(kverify_always_ux);
DEFINE_BOOL_ATTR(kblockd_always_ux);
DEFINE_BOOL_ATTR(dispatch_split);
DEFINE_BOOL_ATTR(debug_dispatch_split);
DEFINE_ULONG_ATTR(lat_all_cnt);
DEFINE_ULONG_ATTR(lat_all_10ms_cnt);
DEFINE_ULONG_ATTR(lat_rt_cnt);
DEFINE_ULONG_ATTR(lat_rt_10ms_cnt);

static struct attribute *wq_stats_attrs[] = {
	&blk_sched_attr.attr,
	&ublk_sched_attr.attr,
	&kverity_cnt_attr.attr,
	&kverity_ux_cnt_attr.attr,
	&kverify_always_ux_attr.attr,
	&kblockd_always_ux_attr.attr,
	&dispatch_split_attr.attr,
	&debug_dispatch_split_attr.attr,
	&lat_all_cnt_attr.attr,
	&lat_all_10ms_cnt_attr.attr,
	&lat_rt_cnt_attr.attr,
	&lat_rt_10ms_cnt_attr.attr,
	NULL,
};

static struct attribute_group wq_stats_attr_group = {
	.attrs = wq_stats_attrs,
};

static int wq_sysfs_init(void)
{
	int ret;

	/* Create /sys/module/oplus_wq_dynamic_priority/parameters/ */
	wq_stats_kobj = kobject_create_and_add("parameters",
					       &THIS_MODULE->mkobj.kobj);
	if (!wq_stats_kobj)
		return -ENOMEM;

	ret = sysfs_create_group(wq_stats_kobj, &wq_stats_attr_group);
	if (ret) {
		kobject_put(wq_stats_kobj);
		wq_stats_kobj = NULL;
	}

	return ret;
}

static void wq_sysfs_exit(void)
{
	if (wq_stats_kobj) {
		sysfs_remove_group(wq_stats_kobj, &wq_stats_attr_group);
		kobject_put(wq_stats_kobj);
		wq_stats_kobj = NULL;
	}
}

/* ============================================================
 * Section 3: Workqueue Optimization
 * ============================================================ */
struct config_wq_flags {
	char *target_str;
	unsigned int new_flags;
};

static struct config_wq_flags oplus_wq_config[] = {
	{ "loop", WQ_UNBOUND | WQ_FREEZABLE | WQ_HIGHPRI },
	{ "kverityd", WQ_MEM_RECLAIM | WQ_HIGHPRI | WQ_UNBOUND },
	{ NULL, 0 }
};

static void android_rvh_alloc_and_link_pwqs_handler(void *unused,
						    struct workqueue_struct *wq,
						    int *ret, bool *skip)
{
	if (WQ_CMP("opluskblockd")) {
		*ret = apply_workqueue_attrs_locked(wq, kblockd_ux_attrs);
		*skip = true;
	} else if (WQ_CMP("opluskverityd") || WQ_CMP("loop")) {
		*ret = apply_workqueue_attrs_locked(wq, normal_ux_attrs);
		*skip = true;
	}
}

static int handler_alloc_workqueue_pre(struct kprobe *p, struct pt_regs *regs)
{
	const char *fmt = (const char *)regs->regs[0];
	unsigned int flags = (unsigned int)regs->regs[1];
	struct config_wq_flags *item = oplus_wq_config;

	if (fmt) {
		while (item->target_str) {
			if ((strlen(fmt) >= strlen(item->target_str)) &&
			    !strncmp(fmt, item->target_str,
				     strlen(item->target_str)) &&
			    (item->new_flags != flags)) {
				pr_info("alloc_workqueue: matching fmt '%s', flags 0x%x -> 0x%x\n",
					fmt, flags, item->new_flags);
				regs->regs[1] = item->new_flags;
				break;
			}
			item++;
		}
	}
	return 0;
}

static struct kprobe oplus_alloc_workqueue_kp = {
	.symbol_name = "alloc_workqueue",
	.pre_handler = handler_alloc_workqueue_pre,
};

static void android_rvh_create_worker_handler(void *unused,
					      struct task_struct *task,
					      struct workqueue_attrs *attrs)
{
	if (attrs->nice == VIRTUAL_KWORKER_NORMAL_NICE ||
	    attrs->nice == VIRTUAL_KWORKER_KBLOCKD_NICE) {
		set_user_nice(task, MIN_NICE);
		oplus_set_ux_state_lock(task, SA_TYPE_LIGHT, -1, true);
		/* sched_set_fifo_low(task); */
		if (task->comm[8] == 'u')
			task->comm[8] = 'X';
	}
}

/* ============================================================
 * Section 4: Block Layer Dispatch Hooks
 * ============================================================ */
static void android_vh_blk_mq_kick_requeue_list_handler(void *unused,
							struct request_queue *q,
							unsigned long delay,
							bool *skip)
{
	mod_delayed_work_on(WORK_CPU_UNBOUND, oplus_kblockd_wq,
			    &q->requeue_work, 0);
	*skip = true;
}

static void android_vh_blk_mq_delay_run_hw_queue_handler(void *unused,
							 int cpu,
							 struct blk_mq_hw_ctx *hctx,
							 unsigned long delay,
							 bool *skip)
{
	struct request_queue *q = hctx->queue;
	struct elevator_queue *e = q->elevator;
	struct hctx_sched_entry *entry;

	blk_sched++;

	/* Dispatch separation: only when scheduler supports and has RT requests */
	if (unlikely(!kblockd_always_ux) && e && test_bit(ELEVATOR_F_DISPATCH_SEP_BIT, &e->flags)) {
		if (!sd_has_work_for_prioclass(hctx, IOPRIO_CLASS_RT))
			return;

		if (cpu == WORK_CPU_UNBOUND)
			cpu = raw_smp_processor_id();

		entry = (struct hctx_sched_entry *)q->android_oem_data1;
		*skip = true;
		ublk_sched++;

		if (q->nr_hw_queues <= 1)
			mod_delayed_work_on(cpu, oplus_kblockd_wq, &entry->ux_dwork, 0);
		else
			kthread_mod_delayed_work(blk_workers[cpu], &entry[cpu].dwork, 0);
		return;
	}

	/* Other cases: use high priority worker */
	*skip = true;
	ublk_sched++;
	mod_delayed_work_on(cpu, oplus_kblockd_wq, &hctx->run_work, delay);
}

/* Check if scheduler is mq-deadline or oplus_io_sched */
static inline bool is_deadline_sched(struct elevator_queue *e)
{
	const char *name;

	if (!e || !e->type || !e->type->elevator_name)
		return false;

	name = e->type->elevator_name;
	return strstr(name, "deadline") || strstr(name, "oplus");
}

/* block_rq_insert handler - record insert time */
static void block_rq_insert_handler(void *data, struct request *rq)
{
	/* Only record for mq-deadline or oplus_io_sched */
	if (is_deadline_sched(rq->q->elevator))
		rq->elv.priv[1] = (void *)jiffies;
}

/* block_rq_issue handler - calculate I2D latency */
static void block_rq_issue_handler(void *data, struct request *rq)
{
	unsigned long insert_time;
	unsigned long i2d_jiffies;
	int ioprio_class;

	/* Only process for mq-deadline or oplus_io_sched */
	if (!is_deadline_sched(rq->q->elevator) || !rq->elv.priv[1])
		return;

	insert_time = (unsigned long)rq->elv.priv[1];

	/* Calculate I2D: current time - insert time */
	i2d_jiffies = jiffies - insert_time;

	/* Sanity check: I2D should be positive and reasonable (< 10s) */
	if (i2d_jiffies > 10 * HZ)
		return;

	/* Update stats */
	lat_all_cnt++;
	ioprio_class = IOPRIO_PRIO_CLASS(req_get_ioprio(rq));
	if (ioprio_class == IOPRIO_CLASS_RT)
		lat_rt_cnt++;

	/* Check if > 10ms */
	if (i2d_jiffies > HZ / 100) {
		lat_all_10ms_cnt++;
		if (ioprio_class == IOPRIO_CLASS_RT)
			lat_rt_10ms_cnt++;
	}
}

/* ============================================================
 * Section 5: Tracepoint Framework
 * ============================================================ */
struct tracepoints_table {
	const char *name;
	void *func;
	struct tracepoint *tp;
	bool registered;
};

static struct tracepoints_table tp_table[] = {
	{ .name = "android_rvh_alloc_and_link_pwqs",
	  .func = android_rvh_alloc_and_link_pwqs_handler },
	{ .name = "android_rvh_create_worker",
	  .func = android_rvh_create_worker_handler },
	{ .name = "android_vh_blk_mq_delay_run_hw_queue",
	  .func = android_vh_blk_mq_delay_run_hw_queue_handler },
	{ .name = "android_vh_blk_mq_kick_requeue_list",
	  .func = android_vh_blk_mq_kick_requeue_list_handler },
	{ .name = "block_rq_insert",
	  .func = block_rq_insert_handler },
	{ .name = "block_rq_issue",
	  .func = block_rq_issue_handler },
};

#define TP_TABLE_SIZE	ARRAY_SIZE(tp_table)

static void lookup_tracepoints(struct tracepoint *tp, void *ignore)
{
	int i;

	for (i = 0; i < TP_TABLE_SIZE; i++) {
		if (strcmp(tp_table[i].name, tp->name) == 0)
			tp_table[i].tp = tp;
	}
}

static int register_tracepoints(int start, int end)
{
	int i;

	if (end >= TP_TABLE_SIZE)
		end = TP_TABLE_SIZE - 1;

	for (i = start; i <= end; i++) {
		if (!tp_table[i].tp) {
			pr_err("%s: tracepoint %s not found\n",
			       THIS_MODULE->name, tp_table[i].name);
			return -ENOENT;
		}

		if (!tp_table[i].registered) {
			tracepoint_probe_register(tp_table[i].tp,
						  tp_table[i].func, NULL);
			tp_table[i].registered = true;
		}
	}

	return 0;
}

static void unregister_tracepoints(int start, int end)
{
	int i;

	for (i = start; i <= end && i < TP_TABLE_SIZE; i++) {
		if (tp_table[i].registered) {
			tracepoint_probe_unregister(tp_table[i].tp,
						    tp_table[i].func, NULL);
			tp_table[i].registered = false;
		}
	}
}

static void unregister_all_tracepoints(void)
{
	unregister_tracepoints(0, TP_TABLE_SIZE - 1);
}

/* ============================================================
 * Section 6: DM-Verity Optimization
 * ============================================================ */
static bool oplus_verity_fec_is_enabled(struct dm_verity *v)
{
	return v->fec && v->fec->dev;
}

static inline bool verity_is_system_shutting_down(void)
{
	return system_state == SYSTEM_HALT ||
	       system_state == SYSTEM_POWER_OFF ||
	       system_state == SYSTEM_RESTART;
}

static void restart_io_error(struct work_struct *w)
{
	kernel_restart("dm-verity device has I/O error");
}

static void oplus_verity_finish_io(struct dm_verity_io *io, blk_status_t status)
{
	struct dm_verity *v = io->v;
	struct bio *bio = dm_bio_from_per_bio_data(io, v->ti->per_io_data_size);

	bio->bi_end_io = io->orig_bi_end_io;
	bio->bi_status = status;

	orig_verity_fec_finish_io(io);

	if (unlikely(status != BLK_STS_OK) &&
	    unlikely(!(bio->bi_opf & REQ_RAHEAD)) &&
	    !io->had_mismatch &&
	    !verity_is_system_shutting_down()) {
		if (v->error_mode == DM_VERITY_MODE_PANIC)
			panic("dm-verity device has I/O error");
		if (v->error_mode == DM_VERITY_MODE_RESTART) {
			static DECLARE_WORK(restart_work, restart_io_error);

			queue_work(v->verify_wq, &restart_work);
			return;
		}
	}

	bio_endio(bio);
}

static void (*orig_verity_work)(struct work_struct *work);

static void oplus_verity_end_io(struct bio *bio)
{
	struct dm_verity_io *io = bio->bi_private;

	if (bio->bi_status &&
	    (!oplus_verity_fec_is_enabled(io->v) ||
	     verity_is_system_shutting_down() ||
	     (bio->bi_opf & REQ_RAHEAD))) {
		oplus_verity_finish_io(io, bio->bi_status);
		return;
	}

	INIT_WORK(&io->work, orig_verity_work);
	queue_work(oplus_kverityd_wq, &io->work);
}

static struct kprobe oplus_verity_fec_init_kp = {
	.symbol_name = "verity_fec_init_io",
};

static int verity_fec_init_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
	struct dm_verity_io *io;
	struct bio *bio;
	unsigned short ioprio_class;

	io = (struct dm_verity_io *)regs->regs[0];
	if (!io || !io->v || !io->v->ti)
		return 0;

	bio = dm_bio_from_per_bio_data(io, io->v->ti->per_io_data_size);
	if (!bio)
		return 0;

	ioprio_class = IOPRIO_PRIO_CLASS(bio->bi_ioprio);
	atomic_long_inc(&kverity_cnt);
	if (ioprio_class == IOPRIO_CLASS_RT || kverify_always_ux) {
		bio->bi_end_io = oplus_verity_end_io;
		atomic_long_inc(&kverity_ux_cnt);
	}

	return 0;
}

static int submit_bio_noacct_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
	struct bio *bio = (struct bio *)regs->regs[0];

	if (!bio || !in_task())
		return 0;

	bio->bi_ioprio = get_current_ioprio();

	if (test_task_ux(current))
		bio->bi_ioprio = IOPRIO_PRIO_VALUE(IOPRIO_CLASS_RT, 5);

	return 0;
}

static struct kprobe oplus_submit_bio_kp = {
	.symbol_name = "submit_bio_noacct",
	.pre_handler = submit_bio_noacct_pre_handler,
};

static int find_kverity_work_handler(struct kprobe *p, struct pt_regs *regs)
{
	struct workqueue_struct *wq;
	struct work_struct *work;
	work_func_t func;

	if (atomic_read(&verity_work_found))
		return 0;

	wq = (struct workqueue_struct *)regs->regs[1];
	work = (struct work_struct *)regs->regs[2];

	if (!wq || !work || !WQ_CMP("kverityd"))
		return 0;

	func = work->func;

	if ((void *)func == orig_verity_prefetch_io)
		return 0;

	if (atomic_inc_return(&verity_work_found) == 1) {
		orig_verity_work = func;
		pr_info("Found verity_work = %pS\n", orig_verity_work);
		schedule_work(&verity_kp_register_work);
	}

	return 0;
}

static struct kprobe find_kverity_work_kp = {
	.symbol_name = "queue_work_on",
	.pre_handler = find_kverity_work_handler,
};

static void verity_kp_register_workfn(struct work_struct *work)
{
	int err;

	if (!orig_verity_work) {
		pr_err("%s: can't find orig_verity_work\n", __func__);
		return;
	}

	oplus_verity_fec_init_kp.pre_handler = verity_fec_init_pre_handler;
	err = register_kprobe(&oplus_verity_fec_init_kp);
	if (err < 0) {
		pr_err("register verity_fec_init_io failed: %d\n", err);
		return;
	}

	unregister_kprobe(&find_kverity_work_kp);
}

static void *lookup_name_via_kprobe(const char *name)
{
	struct kprobe kp = { .symbol_name = name };
	void *addr;

	if (register_kprobe(&kp) < 0)
		return NULL;

	addr = (void *)kp.addr;
	unregister_kprobe(&kp);
	return addr;
}

static int kverity_hook_init(void)
{
	int err;

	orig_verity_prefetch_io = lookup_name_via_kprobe("verity_prefetch_io");
	if (!orig_verity_prefetch_io) {
		pr_err("verity_prefetch_io not found\n");
		return -ENOENT;
	}

	orig_verity_fec_finish_io = lookup_name_via_kprobe("verity_fec_finish_io");
	if (!orig_verity_fec_finish_io) {
		pr_err("verity_fec_finish_io not found\n");
		return -ENOENT;
	}

	INIT_WORK(&verity_kp_register_work, verity_kp_register_workfn);

	err = register_kprobe(&find_kverity_work_kp);
	if (err < 0) {
		pr_err("register find_kverity_work_kp failed: %d\n", err);
		return err;
	}

	return 0;
}

static void kverity_hook_exit(void)
{
	cancel_work_sync(&verity_kp_register_work);

	if (atomic_read(&verity_work_found))
		unregister_kprobe(&oplus_verity_fec_init_kp);
	else
		unregister_kprobe(&find_kverity_work_kp);
}

/* ============================================================
 * Section 7: Module Init/Exit
 * ============================================================ */
extern int oplus_wq_hook_v1_init(void);
extern void oplus_wq_hook_v1_exit(void);

static int oplus_wq_hook_v2_init(void)
{
	int err;

	/* Step 1: Register kprobes */
	err = register_kprobe(&oplus_alloc_workqueue_kp);
	if (err < 0) {
		pr_err("%s: kprobe alloc_workqueue failed: %d\n", __func__, err);
		return err;
	}

	err = register_kprobe(&oplus_submit_bio_kp);
	if (err < 0) {
		pr_err("%s: kprobe submit_bio failed: %d\n", __func__, err);
		goto err_unregister_alloc_wq_kp;
	}

	/* Step 2: Allocate normal_ux_attrs first (needed by tracepoint 0-1 handlers) */
	normal_ux_attrs = alloc_workqueue_attrs();
	if (!normal_ux_attrs) {
		pr_err("%s: alloc normal_ux_attrs failed\n", __func__);
		err = -ENOMEM;
		goto err_unregister_submit_bio_kp;
	}
	normal_ux_attrs->nice = VIRTUAL_KWORKER_NORMAL_NICE;

	kblockd_ux_attrs = alloc_workqueue_attrs();
	if (!kblockd_ux_attrs) {
		pr_err("%s: alloc kblockd_ux_attrs failed\n", __func__);
		err = -ENOMEM;
		goto err_free_ux_attrs;
	}
	kblockd_ux_attrs->nice = VIRTUAL_KWORKER_KBLOCKD_NICE;

	/* Step 3: Lookup and register tracepoints 0-1 (use normal_ux_attrs) */
	for_each_kernel_tracepoint(lookup_tracepoints, NULL);

	err = register_tracepoints(0, 1);
	if (err) {
		pr_err("%s: register tracepoints 0-1 failed\n", __func__);
		goto err_free_blk_attrs;
	}

	/*
	 * Step 4: Create workqueues
	 * Use "opluskverityd/opluskblockd" names to trigger android_rvh_alloc_and_link_pwqs_handler
	 * which applies normal_ux_attrs for UX inheritance
	 */
	oplus_kverityd_wq = alloc_workqueue("opluskverityd",
					    WQ_MEM_RECLAIM | WQ_HIGHPRI | WQ_UNBOUND,
					    0);
	if (!oplus_kverityd_wq) {
		pr_err("%s: alloc oplus_kverityd_wq failed\n", __func__);
		err = -ENOMEM;
		goto err_unregister_tp_0_1;
	}

	oplus_kblockd_wq = alloc_workqueue("opluskblockd",
					   WQ_MEM_RECLAIM | WQ_HIGHPRI | WQ_UNBOUND,
					   0);
	if (!oplus_kblockd_wq) {
		pr_err("%s: alloc oplus_kblockd_wq failed\n", __func__);
		err = -ENOMEM;
		goto err_destroy_kverityd_wq;
	}

	/* Step 5: Register tracepoints 2-5 (use oplus_kblockd_wq, includes I2D) */
	err = register_tracepoints(2, 5);
	if (err) {
		pr_err("%s: register tracepoints 2-5 failed\n", __func__);
		goto err_destroy_kblockd_wq;
	}

	/* Step 6: Initialize kverity hook */
	err = kverity_hook_init();
	if (err) {
		pr_err("%s: kverity_hook_init failed: %d\n", __func__, err);
		goto err_unregister_tp_2_5;
	}

	/* Step 7: Initialize sysfs */
	err = wq_sysfs_init();
	if (err) {
		pr_err("%s: wq_sysfs_init failed: %d\n", __func__, err);
		goto err_kverity_hook_exit;
	}

	/* Step 8: Initialize IO scheduler */
	simple_deadline_init();
	pr_info("%s: init success\n", __func__);
	return 0;

err_kverity_hook_exit:
	kverity_hook_exit();
err_unregister_tp_2_5:
	unregister_tracepoints(2, 5);
err_destroy_kblockd_wq:
	destroy_workqueue(oplus_kblockd_wq);
err_destroy_kverityd_wq:
	destroy_workqueue(oplus_kverityd_wq);
err_unregister_tp_0_1:
	unregister_tracepoints(0, 1);
err_free_blk_attrs:
	free_workqueue_attrs(kblockd_ux_attrs);
err_free_ux_attrs:
	free_workqueue_attrs(normal_ux_attrs);
err_unregister_submit_bio_kp:
	unregister_kprobe(&oplus_submit_bio_kp);
err_unregister_alloc_wq_kp:
	unregister_kprobe(&oplus_alloc_workqueue_kp);
	pr_err("%s: init failed\n", __func__);
	return err;
}

#define WQ_DYN_VERSION	"/soc/oplus,wq_dynamic_version"

struct device_node;
extern int of_property_read_string(const struct device_node *np,
				   const char *propname,
				   const char **out_string);
extern struct device_node *of_find_node_opts_by_path(const char *path,
						     const char **opts);

static int wq_dyn_version = 1;	/* default v1 */

static int oplus_wq_parse_dts_version(void)
{
	const char *value = NULL;
	struct device_node *np;

	//return 2;

	np = of_find_node_opts_by_path(WQ_DYN_VERSION, NULL);
	if (!np) {
		pr_info("%s: dts node not found, use default v1\n", __func__);
		return 1;
	}

	if (of_property_read_string(np, "version", &value) || !value) {
		pr_info("%s: version property not found, use default v1\n",
			__func__);
		return 1;
	}

	if (strcmp(value, "v2") == 0 || strcmp(value, "2") == 0) {
		pr_info("%s: dts version = v2\n", __func__);
		return 2;
	}

	pr_info("%s: dts version = v1 (value=%s)\n", __func__, value);
	return 1;
}

static void oplus_wq_hook_v2_exit(void)
{
	simple_deadline_exit();
	wq_sysfs_exit();
	kverity_hook_exit();
	unregister_all_tracepoints();
	destroy_workqueue(oplus_kblockd_wq);
	destroy_workqueue(oplus_kverityd_wq);
	free_workqueue_attrs(kblockd_ux_attrs);
	free_workqueue_attrs(normal_ux_attrs);
	unregister_kprobe(&oplus_submit_bio_kp);
	unregister_kprobe(&oplus_alloc_workqueue_kp);
}

static int __init oplus_wq_hook_init(void)
{
	wq_dyn_version = oplus_wq_parse_dts_version();

	if (wq_dyn_version == 2) {
		pr_info("%s: using v2 init\n", __func__);
		return oplus_wq_hook_v2_init();
	}

	pr_info("%s: using v1 init\n", __func__);
	return oplus_wq_hook_v1_init();
}

static void __exit oplus_wq_hook_exit(void)
{
	if (wq_dyn_version == 2) {
		pr_info("%s: using v2 exit\n", __func__);
		oplus_wq_hook_v2_exit();
	} else {
		pr_info("%s: using v1 exit\n", __func__);
		oplus_wq_hook_v1_exit();
	}
}

module_init(oplus_wq_hook_init);
module_exit(oplus_wq_hook_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("lijiang");
MODULE_AUTHOR("Gray Jia");
MODULE_DESCRIPTION("OPLUS Block Layer & Workqueue Optimization");

#endif /* LINUX_VERSION_CODE > KERNEL_VERSION(6, 12, 0) */
