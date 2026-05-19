/** Copyright (C), 2019-2024, OPLUS Mobile Comm Corp., Ltd.
* Description: Hmbird camera freq boost
* Author: Gao ZhiFeng 80407967
* Create: 2025-11-17
* Notes: Hmbird camera freq boost
*/
#include <linux/slab.h>
#include <linux/sysctl.h>
#include <linux/kmemleak.h>
#include <linux/ktime.h>
#include <linux/rwsem.h>
#include <linux/rbtree.h>
#include <linux/jiffies.h>
#include <linux/spinlock.h>

#include "hmbird_CameraScene.h"
#include "hmbird_common.h"
#include "../hmbird_II/hmbird_II.h"

#define MAX_NR_PIPELINE		(16)
#define MSEC_PER_SEC		1000L
#define NSEC_PER_MSEC		1000000L
#define JIFFIES_MS			(MSEC_PER_SEC / HZ)

#define CREATE_TRACE_POINTS
#include "trace_hmbird_CameraScene.h"

static unsigned long key_info[2];
static unsigned int boost_threshold_table[MAX_PIPELINE_STAGES];
static unsigned int jiffies_delay_table[MAX_PIPELINE_STAGES];
int boost_enable = 0;
raw_spinlock_t pipeline_lock;
pipeline_status_t g_pipeline[MAX_NR_PIPELINE];

static inline unsigned int check_jiffies_delayed(unsigned long start_jiffies, int stage)
{
	if (jiffies - start_jiffies < jiffies_delay_table[stage] - 1)
		return PIPELINE_NDELAYED;
	else if (jiffies - start_jiffies > jiffies_delay_table[stage])
		return PIPELINE_DELAYED;
	else
		return PIPELINE_SLOWPATH;
}

static unsigned int __try_check_pipeline_delayed_locked_fastpath(pipeline_status_t* pipeline)
{
	return check_jiffies_delayed(pipeline->start_jiffies, pipeline->stage);
}

static bool __check_pipeline_delayed_locked_slowpath(pipeline_status_t* pipeline)
{
	unsigned long now = ktime_get_ns();

	return (now >= pipeline->start_time + boost_threshold_table[pipeline->stage]);
}

unsigned long check_pipeline_delayed_locked(void)
{
	unsigned long max_delayed = 0;

	for (int i = 0; i < MAX_NR_PIPELINE; i++) {
		if (g_pipeline[i].is_started) {
			unsigned long flag = __try_check_pipeline_delayed_locked_fastpath(&g_pipeline[i]);

			if (flag == PIPELINE_NDELAYED)
				continue;
			else if (flag == PIPELINE_DELAYED)
				goto ascend;
			else {
				if (!__check_pipeline_delayed_locked_slowpath(&g_pipeline[i]))
					continue;
			}

ascend:
			g_pipeline[i].delayed_tick_count++;
			max_delayed = max(max_delayed, g_pipeline[i].delayed_tick_count);
		}
	}
	return max_delayed;
}

static void track_camera_pipeline_status_locked(int pipeline, int stage)
{
	g_pipeline[pipeline].stage = stage;
	g_pipeline[pipeline].delayed_tick_count = 0;
	if (stage == PIPELINE_STAGE_START) {
		g_pipeline[pipeline].start_time = ktime_get_ns();
		g_pipeline[pipeline].start_jiffies = jiffies;
		g_pipeline[pipeline].is_started = true;
	} else if (stage == PIPELINE_STAGE_FINISH) {
		g_pipeline[pipeline].is_started = false;
	}
}

static int cfg_key_info_handler(const struct ctl_table *table,
				int write, void *buffer, size_t *lenp,
				loff_t *ppos)
{
	int ret = -EPERM;
	static int cached[2] = {-1, -1};
	int pipeline_and_state[2] = {-1, -1};
	char buf[32];
	struct ctl_table tmp = { };
	static DEFINE_MUTEX(mutex);

	if (!boost_enable)
		return ret;

	mutex_lock(&mutex);
	if (!write) {
		ret = scnprintf(buf, sizeof(buf), "%d | %d", cached[0], cached[1]);
		if (ret < 0)
			goto unlock;
		tmp.data = &buf;
		tmp.maxlen = sizeof(buf);
		ret = proc_dostring(&tmp, write, buffer, lenp, ppos);
	} else {
		int pipeline;
		int stage;
		unsigned long flag;
		cached[0] = -1;
		cached[1] = -1;
		tmp.data = &pipeline_and_state;
		tmp.maxlen = sizeof(pipeline_and_state);
		ret = proc_dointvec(&tmp, write, buffer, lenp, ppos);

		if (ret)
			goto unlock;

		pipeline = pipeline_and_state[1] % MAX_NR_PIPELINE;
		stage = pipeline_and_state[0] + 1;
		if (stage > PIPELINE_STAGE_FINISH)
			stage = PIPELINE_STAGE_TRACE_END;

		hmbird_II_jank_systrace("C|9999|pipeline_%d|%d\n", pipeline, stage);

		raw_spin_lock_irqsave(&pipeline_lock, flag);
		track_camera_pipeline_status_locked(pipeline, pipeline_and_state[0]);
		raw_spin_unlock_irqrestore(&pipeline_lock, flag);

		if (!ret) {
			cached[0] = pipeline_and_state[0];
			cached[1] = pipeline_and_state[1];
		}
	}
unlock:
	mutex_unlock(&mutex);
	return ret;
}

static inline unsigned int u32_devide_roundup(unsigned int a, unsigned int b)
{
	return (a + b - 1) / b;
}

static int cfg_boost_threshold_handler(const struct ctl_table *table,
					int write, void *buffer, size_t *lenp,
					loff_t *ppos)
{
	int ret = -EPERM;
	char buf[256];
	char *token, *cur;
	int stage_count = 0;
	unsigned int thresholds[MAX_PIPELINE_STAGES];
	static DEFINE_MUTEX(mutex);

	struct ctl_table tmp = {
		.data = &buf,
		.maxlen = sizeof(buf),
		.mode = table->mode,
	};

	mutex_lock(&mutex);
	if (!write) {
		ret = scnprintf(buf, sizeof(buf), "%u", boost_threshold_table[0]);
		for (int i = 1; i < MAX_PIPELINE_STAGES && boost_threshold_table[i] != 0; i++) {
			ret += scnprintf(buf + ret, sizeof(buf) - ret, " %u",
					boost_threshold_table[i]);
		}
		tmp.data = &buf;
		tmp.maxlen = sizeof(buf);
		ret = proc_dostring(&tmp, write, buffer, lenp, ppos);
	} else {
		tmp.data = &buf;
		tmp.maxlen = sizeof(buf);
		ret = proc_dostring(&tmp, write, buffer, lenp, ppos);
		if (ret)
			goto unlock;

		cur = buf;
		while ((token = strsep(&cur, " ")) != NULL && stage_count < MAX_PIPELINE_STAGES) {
			ret = kstrtouint(token, 10, &thresholds[stage_count]);
			if (ret)
				goto unlock;

			if (thresholds[stage_count] < 4 * NSEC_PER_MSEC)
				thresholds[stage_count] = 4 * NSEC_PER_MSEC;

			stage_count++;
		}

		if (stage_count == 0)
			goto unlock;

		for (int i = 0; i < stage_count; i++) {
			boost_threshold_table[i] = thresholds[i];
			jiffies_delay_table[i] = u32_devide_roundup(thresholds[i] / NSEC_PER_MSEC, JIFFIES_MS);
		}
	}
unlock:
	mutex_unlock(&mutex);
	return ret;
}

static int cfg_boost_enable_handler(const struct ctl_table *table,
					int write, void *buffer, size_t *lenp,
					loff_t *ppos)
{
	int ret = -EPERM;
	unsigned int val;
	static DEFINE_MUTEX(mutex);

	struct ctl_table tmp = {
		.data = &val,
		.maxlen = sizeof(val),
		.mode = table->mode,
	};

	mutex_lock(&mutex);
	if (!write) {
		val = boost_enable;
		ret = proc_dointvec(&tmp, write, buffer, lenp, ppos);
	} else {
		ret = proc_dointvec(&tmp, write, buffer, lenp, ppos);
		if (ret)
			goto unlock;
		boost_enable = val;
	}
unlock:
	mutex_unlock(&mutex);
	return ret;
}

static struct ctl_table hmbird_Camera_table[] = {
	{
		.procname	= "key_info",
		.data		= &key_info,
		.maxlen		= sizeof(unsigned long)*2,
		.mode		= 0666,
		.proc_handler	= cfg_key_info_handler,
	},
	{
		.procname	= "boost_threshold",
		.data		= &boost_threshold_table,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0666,
		.proc_handler	= cfg_boost_threshold_handler,
	},
	{
		.procname	= "boost_enable",
		.data		= &boost_enable,
		.maxlen		= sizeof(int),
		.mode		= 0666,
		.proc_handler	= cfg_boost_enable_handler,
	},
};

static void camera_boost_init(void)
{
	unsigned long flag;

	raw_spin_lock_init(&pipeline_lock);

	raw_spin_lock_irqsave(&pipeline_lock, flag);
	for (int j = 0; j < MAX_NR_PIPELINE; j++) {
		g_pipeline[j].stage = PIPELINE_STAGE_FINISH;
		g_pipeline[j].delayed_tick_count = 0;
		g_pipeline[j].start_time = 0;
		g_pipeline[j].is_started = false;
	}
	raw_spin_unlock_irqrestore(&pipeline_lock, flag);
}

static struct ctl_table_header *hdr;
int hmbird_CameraScene_sysctl_init(void)
{
	camera_boost_init();

	hdr = register_sysctl("hmbird_Camera", hmbird_Camera_table);

	kmemleak_not_leak(hdr);
	return 0;
}

void hmbird_CameraScene_sysctl_deinit(void)
{
	unregister_sysctl_table(hdr);
}
