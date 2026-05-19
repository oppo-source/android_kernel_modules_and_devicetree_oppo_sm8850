#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/bpf.h>
#include <linux/bpf_verifier.h>
#include <linux/cpumask.h>
#include <linux/mmu_context.h>
#include <linux/jump_label.h>
#include "../../kernel/sched/sched.h"
#include <trace/hooks/sched.h>
#include "hmbird_II.h"
#include "hmbird_II_export.h"
#include "hmbird_II_freqgov.h"
#include "hmbird_II_shadow_tick.h"
#include "../hmbird_minidump.h"
#include "hmbird_common.h"
#include "hmbird_II_critical_task_monitor.h"
#include <linux/sched/ext.h>
#include <linux/random.h>
#include <trace/hooks/sched.h>
#include <trace/events/task.h>
#include <linux/preempt.h>
#include <linux/irq_work.h>
#include "trace_hmbird_II.h"
#include "../hmbird_kfunc.h"

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("OPLUS KERNEL III TEAM");
MODULE_VERSION("1.0");
#define wlog(fmt, ...)	pr_err(fmt, ##__VA_ARGS__)

enum scx_ops_enable_state {
	SCX_OPS_ENABLING,
	SCX_OPS_ENABLED,
	SCX_OPS_DISABLING,
	SCX_OPS_DISABLED,
	SCX_OPS_DISABLING_SWITCH,
};
extern enum hmbird_switch_reason_type sw_reason;
extern struct md_info_t *md_info;

#define MAX_STATE 10
const int scx2hmbird_state[MAX_STATE] = {1, 4, 2, 0, 3, 5, 6, 7, 8, 9};

static atomic_t hb_ops_enable_state_var = ATOMIC_INIT(SCX_OPS_DISABLED);
atomic_t __hb_ops_enabled = ATOMIC_INIT(0);
unsigned int hmbird_enable = 0;

static unsigned long (*addr_kallsyms_lookup_name)(const char *name);
LOOKUP_KERNEL_SYMBOL(kallsyms_lookup_name);

struct sched_class *addr_stop_sched_class;
struct sched_class *addr_dl_sched_class;
struct sched_class *addr_rt_sched_class;
struct sched_class *addr_fair_sched_class;
struct sched_class *addr_ext_sched_class;

int hmbird_prepare_and_check(void)
{
	lookup_kallsyms_lookup_name();
	if (!addr_kallsyms_lookup_name)
		return -1;
	addr_stop_sched_class = (struct sched_class *)addr_kallsyms_lookup_name("stop_sched_class");
	addr_dl_sched_class = (struct sched_class *)addr_kallsyms_lookup_name("dl_sched_class");
	addr_rt_sched_class = (struct sched_class *)addr_kallsyms_lookup_name("rt_sched_class");
	addr_fair_sched_class = (struct sched_class *)addr_kallsyms_lookup_name("fair_sched_class");
	addr_ext_sched_class = (struct sched_class *)addr_kallsyms_lookup_name("ext_sched_class");

	if (!addr_stop_sched_class
		|| !addr_dl_sched_class || !addr_rt_sched_class
		|| !addr_fair_sched_class || !addr_ext_sched_class) {
		HMBIRD_ERR("lookup kernel symbols failed\n");
		return -1;
	}
	return 0;
}

static int __hmbird_update_ux_state_locked(struct task_struct *p, int ux_state)
{
	struct rq *rq;
	int orig_ux_state = oplus_get_ux_state(p) & SCHED_ASSIST_UX_MASK;

	if (!scx_enabled())
		return 1;

	if (!task_on_rq_queued(p))
		return 1;

	if (p->on_cpu)
		return 1;

	if (p->flags & PF_EXITING)
		return 1;

	rq = task_rq(p);
	deactivate_task(rq, p, 0);
	activate_task(rq, p, 0);

	scx_trace_printk("runnable task[%s] "
			"orig ux_state[%d] -> new ux_state[%d]\n",
			p->comm, orig_ux_state, ux_state);
	return 0;
}

static inline bool hb_task_should_scx(int policy)
{
	if (!atomic_read(&__hb_ops_enabled))
		return false;

	if (unlikely(atomic_read(&hb_ops_enable_state_var) == SCX_OPS_DISABLING)) {
		if (current == sched_ext_helper) {
			atomic_xchg(&hb_ops_enable_state_var, SCX_OPS_DISABLING_SWITCH);
		}
	}

	if (!sched_ext_helper && atomic_read(&hb_ops_enable_state_var) == SCX_OPS_DISABLING)
		return false;

	if (unlikely(atomic_read(&hb_ops_enable_state_var) == SCX_OPS_DISABLING_SWITCH))
		return false;

	return true;
}

void hmbird_state_systrace_c(void)
{
	int scx_ops_state, hmbird_state;

	scx_ops_state = atomic_read(&hb_ops_enable_state_var);
	if (scx_ops_state < 0 && scx_ops_state >= MAX_STATE) {
		return;
	}
	hmbird_state = scx2hmbird_state[scx_ops_state];

	hmbird_II_output_systrace("C|9999|hmbird_state|%d\n", hmbird_state);
}

/*
 * workaround for a bug while disable sched_ext
 * This case is fixed in kernel 6.17 :
 * Fix a realtime tasks starvation case where failure to enqueue a timer
 * whose expiration time is already in the past would cause repeated
 * attempts to re-enqueue a deadline server task which leads to starving
 * the former, realtime one
 * https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/commit/?id=fe3ad7a58b581859a1a7c237b670f8bcbf5b253c
 *
 * Once bug is fixed, remove the workaround function
 */
void replenish_fair_dl_server(void)
{
		int cpu;
		struct rq *rq;
		struct rq_flags rf;
		struct sched_dl_entity *dl_se;

		for_each_possible_cpu(cpu) {
			rq = cpu_rq(cpu);
			rq_lock_irqsave(rq, &rf);
			dl_se = &rq->fair_server;
			dl_se->dl_throttled = 1;
			dl_se->runtime = 0;
			rq_unlock_irqrestore(rq, &rf);
		}
}

static void android_vh_scx_ops_enable_state(void *unused, int state)
{
	int state_prev;
	state_prev = atomic_xchg(&hb_ops_enable_state_var, state);
	if (state == SCX_OPS_DISABLED && state_prev != SCX_OPS_DISABLING_SWITCH) {
		pr_warn("sched_ext: ops error detected without ops (SCX_OPS_DISABLING_SWITCH)\n");
		sw_update(0, HMBIRD_DISABLED_WITHOUT_ING, HMBIRD_SWITCH_NORMAL);
	}

	if (state == SCX_OPS_DISABLING) {
		replenish_fair_dl_server();
	}

	pr_info("hmbird_II: scx_ops_enable_state = %d\n", atomic_read(&hb_ops_enable_state_var));
	if (unlikely(hmbird_debug & HMBIRD_DEBUG_SYSTRACE))
		hmbird_state_systrace_c();

	if (state >= 0 && state < MAX_STATE) {
		sw_update(1, scx2hmbird_state[state], HMBIRD_SWITCH_NORMAL);
	}
}

static void android_vh_scx_enabled(void *unused, int enabled)
{
	atomic_xchg(&__hb_ops_enabled, enabled);
	pr_info("hmbird_II: scx %s\n", atomic_read(&__hb_ops_enabled) ? "enabled" : "disabled");
	sw_update(1, enabled ? HMBIRD_ENABLED : HMBIRD_DISABLED, HMBIRD_SWITCH_NORMAL);
}

static inline bool move_entity(unsigned int flags)
{
	if ((flags & (DEQUEUE_SAVE | DEQUEUE_MOVE)) == DEQUEUE_SAVE)
		return false;
	return true;
}

static void android_vh_scx_restore_flags(void *unused, const struct sched_class *prev,
						const struct sched_class *next, int *flags)
{
	if (!move_entity(*flags)) {
		if ((prev == addr_rt_sched_class) && (next == &ext_sched_class)) {
			*flags |= DEQUEUE_MOVE;
		} else if ((prev == &ext_sched_class) && (next == addr_rt_sched_class)) {
			*flags |= DEQUEUE_MOVE;
		}
	}
}

static void android_vh_task_should_scx(void *unused, int *should_scx, int policy, int prio)
{
	if (hmbird_enable != HMBIRD_II_SCENE)
		return;
	if (hb_task_should_scx(policy) && prio > 10)
		*should_scx = 1;
}

static void android_vh_scx_ops_consider_migration(void *unused, bool *consider_migration)
{
	if (hmbird_enable != HMBIRD_II_SCENE)
		return;
	if (atomic_read(&__hb_ops_enabled))
		*consider_migration = true;
}

static void android_vh_fix_prev_keep_slice(void *unused, struct task_struct *p)
{
	if (hmbird_enable != HMBIRD_II_SCENE)
		return;
	if (atomic_read(&__hb_ops_enabled))
		p->scx.slice = 1000000;
}

void android_vh_dup_task_struct_handler(void *unused,
		struct task_struct *tsk, struct task_struct *orig)
{
	struct scx_entity *scx,*orig_scx;

	if (!tsk || !orig)
		return;

	scx = get_oplus_ext_entity(tsk);
	orig_scx = get_oplus_ext_entity(orig);
	if (!scx || !orig_scx)
		return;

	cpumask_copy(&scx->cpus_mask_back, &orig_scx->cpus_mask_back);
	scx->need_back = orig_scx->need_back;
	scx->sched_prop = HMBIRD_SCHED_PROP_PREFER_IDLE;
}

static void hmbird_task_change_cpumask(struct task_struct *p)
{
	struct scx_entity *scx;

	if (hmbird_enable != HMBIRD_II_SCENE)
		return;

	scx = get_oplus_ext_entity(p);
	if (!scx)
		return;

	if (0) {
		if (!p->user_cpus_ptr) {
			scx->need_back = true;
			cpumask_copy(&scx->cpus_mask_back, &p->cpus_mask);
			cpumask_copy(&p->cpus_mask, task_cpu_possible_mask(p));
			p->nr_cpus_allowed = cpumask_weight(&p->cpus_mask);
		}
	}
	scx->need_back = true;
	cpumask_copy(&scx->cpus_mask_back, &p->cpus_mask);
	cpumask_copy(&p->cpus_mask, task_cpu_possible_mask(p));
	p->nr_cpus_allowed = cpumask_weight(&p->cpus_mask);
}

static void hmbird_task_restore_cpumask(struct task_struct *p)
{
	struct scx_entity *scx;

	if (hmbird_enable != HMBIRD_II_SCENE)
		return;

	scx = get_oplus_ext_entity(p);
	if (!scx)
		return;

	if (0) {
		if (!p->user_cpus_ptr || scx->need_back) {
			scx->need_back = false;
			cpumask_copy(&p->cpus_mask, &scx->cpus_mask_back);
			p->nr_cpus_allowed = cpumask_weight(&p->cpus_mask);
		}
	}
	if (scx->need_back) {
		scx->need_back = false;
		cpumask_copy(&p->cpus_mask, &scx->cpus_mask_back);
		p->nr_cpus_allowed = cpumask_weight(&p->cpus_mask);
	}
}


static void android_vh_switching_to_scx_handler(void *unused, struct rq *rq, struct task_struct *p)
{
	if (hmbird_enable != HMBIRD_II_SCENE)
		return;
	if (p->nr_cpus_allowed == 1) {
		return;
	}
	hmbird_task_change_cpumask(p);
}

static void android_vh_scx_task_switch_finish_handler(void *unused, struct task_struct *p, int enable)
{
	if (hmbird_enable != HMBIRD_II_SCENE)
		return;
	if (p->nr_cpus_allowed == 1)
		return;
	if (!enable) {
		hmbird_task_restore_cpumask(p);
	}
}

static void trace_set_cpus_allowed_common(void *unused, struct task_struct *p, struct affinity_context *ctx, int *done)
{
	struct scx_entity *scx;

	if (hmbird_enable != HMBIRD_II_SCENE)
		return;

	*done = 1;
	if (ctx->flags & (SCA_MIGRATE_ENABLE | SCA_MIGRATE_DISABLE)) {
		p->cpus_ptr = ctx->new_mask;
		return;
	}

	scx = get_oplus_ext_entity(p);
	if (!scx)
		return;
	{
	if (0) {

		if (ctx->flags & SCA_USER || cpumask_weight(ctx->new_mask) == 1) {
			cpumask_copy(&p->cpus_mask, ctx->new_mask);
			scx->need_back = false;
			p->nr_cpus_allowed = cpumask_weight(ctx->new_mask);
			swap(p->user_cpus_ptr, ctx->user_mask);
		} else {
			cpumask_copy(&scx->cpus_mask_back, ctx->new_mask);
			scx->need_back = true;
			if (p->user_cpus_ptr) {
				cpumask_and(&p->cpus_mask, p->user_cpus_ptr, task_cpu_possible_mask(p));
			} else {
				cpumask_copy(&p->cpus_mask, task_cpu_possible_mask(p));
			}
			p->nr_cpus_allowed = cpumask_weight(&p->cpus_mask);
		}
	}
	}
	if (cpumask_weight(ctx->new_mask) == 1) {
		scx->need_back = false;
		cpumask_copy(&p->cpus_mask, ctx->new_mask);
		p->nr_cpus_allowed = cpumask_weight(ctx->new_mask);
		if (ctx->flags & SCA_USER)
			swap(p->user_cpus_ptr, ctx->user_mask);
	} else {
		cpumask_copy(&scx->cpus_mask_back, ctx->new_mask);
		scx->need_back = true;
		cpumask_copy(&p->cpus_mask, task_cpu_possible_mask(p));
		p->nr_cpus_allowed = cpumask_weight(&p->cpus_mask);
		if (ctx->flags & SCA_USER)
			swap(p->user_cpus_ptr, ctx->user_mask);
	}
}

static inline int task_specific_type(struct task_struct *p, bool is_ext)
{
	if (rt_prio(p->prio))
		return is_ext ? TASK_EXT_CLASS_RT : TASK_UNKNOWN_CLASS;
	else if (hb_bpf_is_ux(p))
		return is_ext ? TASK_EXT_CLASS_UX : TASK_FAIR_CLASS_UX;
	else if (hb_bpf_is_vip_mvp(p))
		return is_ext ? TASK_EXT_CLASS_VM : TASK_FAIR_CLASS_VM;
	else
		return is_ext ? TASK_EXT_CLASS : TASK_FAIR_CLASS;
}

int task_prio_type(struct task_struct *p)
{
	if (addr_ext_sched_class && p->sched_class == addr_ext_sched_class)
		return task_specific_type(p, 1);
	else if (addr_stop_sched_class && p->sched_class == addr_stop_sched_class)
		return TASK_STOP_CLASS;
	else if (addr_dl_sched_class && p->sched_class == addr_dl_sched_class)
		return TASK_DL_CLASS;
	else if (addr_rt_sched_class && p->sched_class == addr_rt_sched_class)
		return TASK_RT_CLASS;
	else if (addr_fair_sched_class && p->sched_class == addr_fair_sched_class)
		return task_specific_type(p, 0);
	else if (is_idle_task(p))
		return TASK_IDLE_CLASS;
	else
		return TASK_UNKNOWN_CLASS;
}

static void hb_locking_state_systrace_c(int cpu, struct task_struct *p)
{
	if (!locking_protect_enable())
		return;
	locking_state_systrace_c(cpu, p);
}

static void dsq_id_systrace_c(int cpu, struct task_struct *p)
{
	int task_type = task_prio_type(p);
	hmbird_II_output_systrace("C|9999|Cpu%d_dsq_id|%u\n", cpu, task_type);
}

void ux_state_systrace_c(unsigned int cpu, struct task_struct *p);
static void sched_switch_handler(void *unused, bool preempt,
                        struct task_struct *prev, struct task_struct *next, unsigned int prev_state)
{
	int cpu = smp_processor_id();

	if (scx_enabled() && unlikely(hmbird_debug & HMBIRD_DEBUG_SYSTRACE)) {
		hb_locking_state_systrace_c(cpu, next);
		ux_state_systrace_c(cpu, next);
	}

	trace_hb_sched_switch(preempt, prev, next, prev_state, NULL);
	if (sched_hook_executed(cpu))
		return;

	if (scx_enabled() && unlikely(hmbird_debug & HMBIRD_DEBUG_SYSTRACE))
		dsq_id_systrace_c(cpu, next);
}

void get_scx_state(int *ops_enabled, int *ops_enable_state_var)
{
	*ops_enabled = atomic_read(&__hb_ops_enabled);
	*ops_enable_state_var = atomic_read(&hb_ops_enable_state_var);
}

noinline int tracing_mark_write(const char *buf)
{
	trace_printk(buf);
	return 0;
}

int nr_cluster;
struct hmbird_sched_cluster hmbird_cluster[MAX_NR_CLUSTER];


static DEFINE_MUTEX(qos_lock);
static DEFINE_PER_CPU(struct freq_qos_request, hmbird_II_qos_min);

struct hmbird_qos_req {
	struct delayed_work qos_work;
	struct cpumask req_mask;
	unsigned int min_freq;
	unsigned int max_freq; // may be unused now
};

static struct hmbird_qos_req request_req, reset_req;

#define FREQ_QOS_REQ_MAX_MS				(5 * MSEC_PER_SEC)

static void hmbird_qos_update_handler(struct work_struct *work)
{
	int cpu;
	struct cpumask req_mask;
	struct freq_qos_request *req;
	struct cpufreq_policy *policy;
	struct hmbird_qos_req *hreq = container_of((struct delayed_work *)work, struct hmbird_qos_req, qos_work);
	if (hreq != &reset_req)
		cancel_delayed_work_sync(&reset_req.qos_work);
	mutex_lock(&qos_lock);
	cpus_read_lock();
	cpumask_and(&req_mask, &hreq->req_mask, cpu_present_mask);
	for_each_cpu(cpu, &req_mask) {
		policy = cpufreq_cpu_get_raw(cpu);
		if (!policy) {
			continue;
		}
		req = &per_cpu(hmbird_II_qos_min, policy->cpu);
		cpumask_andnot(&req_mask, &req_mask, policy->related_cpus);
		freq_qos_update_request(req, hreq->min_freq);
	}
	cpus_read_unlock();
	mutex_unlock(&qos_lock);
	if (hreq != &reset_req && hreq->min_freq != FREQ_QOS_MIN_DEFAULT_VALUE)
		schedule_delayed_work(&reset_req.qos_work, msecs_to_jiffies(FREQ_QOS_REQ_MAX_MS));
}

void hmbird_qos_request_min(const struct cpumask *cpus, unsigned int min_freq)
{
	cancel_delayed_work(&request_req.qos_work);
	cpumask_copy(&request_req.req_mask, cpus);
	request_req.min_freq = min_freq;
	queue_delayed_work_on(0, system_highpri_wq, &request_req.qos_work, 0);
}

static inline void hmbird_freq_qos_request_exit(void)
{
	struct freq_qos_request *req;
	int cpu;
	for_each_present_cpu(cpu) {
		req = &per_cpu(hmbird_II_qos_min, cpu);
		if (req && freq_qos_request_active(req))
			freq_qos_remove_request(req);
	}
}

static int hmbird_freq_qos_request_init(void)
{
	unsigned int cpu;
	int ret;

	struct cpufreq_policy *policy;
	struct freq_qos_request *req;
	reset_req.req_mask.bits[0] = 0xff;
	INIT_DELAYED_WORK(&request_req.qos_work, hmbird_qos_update_handler);
	INIT_DELAYED_WORK(&reset_req.qos_work, hmbird_qos_update_handler);

	for_each_cpu(cpu, cpu_possible_mask) {
		policy = cpufreq_cpu_get(cpu);
		if (!policy) {
			HMBIRD_ERR("%s: Failed to get cpufreq policy for cpu%d\n",
				__func__, cpu);
			ret = -EINVAL;
			goto cleanup;
		}

		req = &per_cpu(hmbird_II_qos_min, cpu);
		ret = freq_qos_add_request(&policy->constraints, req,
			FREQ_QOS_MIN, FREQ_QOS_MIN_DEFAULT_VALUE);
		if (ret < 0) {
			HMBIRD_ERR("%s: Failed to add min freq constraint (%d)\n",
				__func__, ret);
			cpufreq_cpu_put(policy);
			goto cleanup;
		}

		cpufreq_cpu_put(policy);
	}
	return 0;

cleanup:
	hmbird_freq_qos_request_exit();
	return ret;
}

struct kick_cpu_irq_work {
	struct cpumask cpus_to_kick;
	struct irq_work irq_work;
};

static DEFINE_PER_CPU(struct kick_cpu_irq_work, hmbird_II_kick_cpu_work);

static bool can_skip_idle_kick(struct rq *rq)
{
	lockdep_assert_rq_held(rq);

	/*
	 * We can skip idle kicking if @rq is going to go through at least one
	 * full SCX scheduling cycle before going idle. Just checking whether
	 * curr is not idle is insufficient because we could be racing
	 * balance_one() trying to pull the next task from a remote rq, which
	 * may fail, and @rq may become idle afterwards.
	 *
	 * The race window is small and we don't and can't guarantee that @rq is
	 * only kicked while idle anyway. Skip only when sure.
	 */
	return !is_idle_task(rq->curr) && !(rq->scx.flags & SCX_RQ_IN_BALANCE);
}

static void kick_one_cpu_if_idle(s32 cpu, struct rq *this_rq)
{
	struct rq *rq = cpu_rq(cpu);
	unsigned long flags;

	raw_spin_rq_lock_irqsave(rq, flags);
	if (!can_skip_idle_kick(rq) &&
		(cpu_online(cpu) || cpu == cpu_of(this_rq))) {
			resched_curr(rq);
			if (unlikely(hmbird_debug & HMBIRD_DEBUG_SYSTRACE))
				hmbird_II_output_systrace("C|9999|cpu_kick|%d", cpu);
	}
	raw_spin_rq_unlock_irqrestore(rq, flags);
}

static void kick_cpus_irq_workfn(struct irq_work *irq_work)
{
	struct rq *this_rq = this_rq();
	struct kick_cpu_irq_work *work = &per_cpu(hmbird_II_kick_cpu_work, this_rq->cpu);
	int cpu;

	for_each_cpu(cpu, &work->cpus_to_kick) {
		kick_one_cpu_if_idle(cpu, this_rq);
		cpumask_clear_cpu(cpu, &work->cpus_to_kick);
		if (unlikely(hmbird_debug & HMBIRD_DEBUG_SYSTRACE))
			hmbird_II_output_systrace("C|9999|cpu_kick_if_idle|%d", cpu);
	}
}

/**
 * Since scx_bpf_kick_cpu skips the CPUs that can_skip_idle_kick,
 * and when we call scx_bpf_kick_cpu, the task has not yet been re-queued,
 * we re-implemented the kick_if_idle logic here.
 *
 * Background:
 * - scx_bpf_kick_cpu has an optimization to skip CPUs which are not definitely idle.
 * - In our top-app migration path, kick is requested before the wakee is re-queued,
 *   and the optimization can incorrectly skip the target CPU.
 * - This helper enforces "kick if (maybe) idle" semantics to reduce migration latency.
 */
void hmbird_II_kick_cpu_if_idle(int cpu)
{
	unsigned long irq_flags;
	struct rq *this_rq;
	struct kick_cpu_irq_work *work;

	local_irq_save(irq_flags);
	this_rq = this_rq();
	work = &per_cpu(hmbird_II_kick_cpu_work, this_rq->cpu);
	cpumask_set_cpu(cpu, &work->cpus_to_kick);
	irq_work_queue(&work->irq_work);
	local_irq_restore(irq_flags);
}

static void hmbird_kick_cpus_work_init(void)
{
	struct kick_cpu_irq_work *work;
	int cpu;

	for_each_cpu(cpu, cpu_possible_mask) {
		work = &per_cpu(hmbird_II_kick_cpu_work, cpu);
		cpumask_clear(&work->cpus_to_kick);
		init_irq_work(&work->irq_work, kick_cpus_irq_workfn);
	}
}

static void hmbird_build_clusters(void)
{
	int cpu;
	struct hmbird_sched_cluster *cluster;
	int cluster_id;
	memset(hmbird_cluster, 0, sizeof(struct hmbird_sched_cluster) * MAX_NR_CLUSTER);

	for_each_cpu(cpu, cpu_possible_mask) {
		cluster_id = topology_cluster_id(cpu);
		if (cluster_id < 0 || cluster_id >= MAX_NR_CLUSTER) {
			HMBIRD_ERR("build clusters bug!\n");
			break;
		}
		cpumask_set_cpu(cpu, &hmbird_cluster[cluster_id].cpus);
	}

	/* check nr_cluster */
	for (cluster_id = 0; cluster_id < MAX_NR_CLUSTER - 1; cluster_id++) {
		cluster = &hmbird_cluster[cluster_id];
		if (cpumask_empty(&cluster->cpus))
			break;
		cluster->id = cluster_id;
		nr_cluster++;
	}

	hmbird_cluster[nr_cluster].id = MAX_NR_CLUSTER;

	for_each_sched_cluster(cluster) {
		for_each_cpu(cpu, &cluster->cpus) {
			pr_err("hmbird_II::for_each_cluster=%d, cpu=%d\n", cluster->id, cpu);
		}
	}
}

extern int critical_task_monitor_init(void);
extern int critical_task_monitor_deinit(void);

static bool task_is_percpu(struct task_struct *p)
{
	return (p->nr_cpus_allowed == 1) || is_migration_disabled(p);
}

static void set_task_vtime(struct task_struct *p, u64 vtime)
{
	p->scx.dsq_vtime = vtime;
}

static void set_task_vtime_from_prio(struct task_struct *p)
{
	int type = task_prio_type(p);
	switch (type) {
	case TASK_DL_CLASS:
	case TASK_RT_CLASS:
	case TASK_EXT_CLASS_RT:
		set_task_vtime(p, RT_VTIME);
		break;
	case TASK_FAIR_CLASS_UX:
	case TASK_EXT_CLASS_UX:
		set_task_vtime(p, UX_VTIME);
		break;
	case TASK_FAIR_CLASS_VM:
	case TASK_EXT_CLASS_VM:
		set_task_vtime(p, VIP_MVP_VTIME);
		break;
	case TASK_FAIR_CLASS:
	case TASK_EXT_CLASS:
		if (task_is_percpu(p))
			set_task_vtime(p, PCP_CFS_VTIME);
		else if (p->prio < NORMAL_PRIO)
			set_task_vtime(p, HIGH_CFS_VTIME);
		else if (p->prio > NORMAL_PRIO)
			set_task_vtime(p, LOW_CFS_VTIME);
		else
			set_task_vtime(p, NORM_CFS_VTIME);
		break;
	default:
		set_task_vtime(p, NORM_CFS_VTIME);
	}
}

static void android_vh_enq_to_priq_handler(void *unused, struct rq *rq, struct scx_dispatch_q *dsq, struct task_struct *p, bool *enq_priq)
{
	if (dsq->id == SCX_DSQ_GLOBAL) {
		set_task_vtime_from_prio(p);
		*enq_priq = 1;
		return;
	}
}

bool is_pre_switch_task(struct task_struct *p)
{
	return rt_prio(p->prio) || hb_bpf_is_ux(p) || hb_bpf_is_vip_mvp(p);
}

static bool check_task_need_skip_switch(bool enable, int curr_round, struct task_struct *p)
{
	if (enable) {
		if (curr_round == 1) {
			if (!is_pre_switch_task(p))
				return false;
		} else {
			if (p->sched_class != addr_ext_sched_class)
				return false;
		}
	} else {
		if (curr_round == 1) {
			if (is_pre_switch_task(p))
				return false;
		} else {
			if (p->sched_class == addr_ext_sched_class)
				return false;
		}
	}
	return true;
}

static void android_vh_scx_switch_repeat_skip_handler(void *unused, struct task_struct *p,
													bool *skip, int *repeat)
{
	if (*repeat == 0) {
		(*repeat)++;
	} else if (*repeat == SCX_SWITCH_MAX_REPEAT) {
		*repeat = -1;
	}
	if ((atomic_read(&hb_ops_enable_state_var) == SCX_OPS_ENABLING)) {
		*skip = check_task_need_skip_switch(true, *repeat, p);
	} else if ((atomic_read(&hb_ops_enable_state_var) == SCX_OPS_DISABLING) ||
		(atomic_read(&hb_ops_enable_state_var) == SCX_OPS_DISABLING_SWITCH)) {
		*skip = check_task_need_skip_switch(false, *repeat, p);
	}
}

int hmbird_II_init(void)
{
	hmbird_build_clusters();
	hmbird_kick_cpus_work_init();
	hmbird_sysctl_init();
	hmbird_freqgov_init();
	hmbird_shadow_tick_init();
	critical_task_monitor_init();
	hmbird_freq_qos_request_init();
	register_trace_android_vh_task_should_scx(android_vh_task_should_scx, NULL);
	register_trace_android_vh_scx_ops_consider_migration(android_vh_scx_ops_consider_migration, NULL);
	register_trace_android_vh_scx_fix_prev_slice(android_vh_fix_prev_keep_slice, NULL);
	register_trace_android_vh_scx_enabled(android_vh_scx_enabled, NULL);
	register_trace_android_vh_scx_ops_enable_state(android_vh_scx_ops_enable_state, NULL);
	register_trace_android_vh_dup_task_struct(android_vh_dup_task_struct_handler, NULL);
	register_trace_android_vh_scx_task_switch_finish(android_vh_scx_task_switch_finish_handler, NULL);
	register_trace_android_vh_scx_set_cpus_allowed(trace_set_cpus_allowed_common, NULL);
	register_trace_android_vh_scx_restore_flags(android_vh_scx_restore_flags, NULL);
	register_trace_android_vh_switching_to_scx(android_vh_switching_to_scx_handler, NULL);
	register_trace_sched_switch(sched_switch_handler, NULL);
	hmbird_update_ux_state_cb_init(__hmbird_update_ux_state_locked);
	register_trace_android_vh_enq_to_priq(android_vh_enq_to_priq_handler, NULL);
	register_trace_android_vh_scx_switch_repeat_skip(android_vh_scx_switch_repeat_skip_handler, NULL);
	return 0;
}

void hmbird_II_exit(void)
{
	hmbird_sysctl_deinit();
	critical_task_monitor_deinit();
	ko_exceps_update(KO_DEINITED, jiffies);
}

