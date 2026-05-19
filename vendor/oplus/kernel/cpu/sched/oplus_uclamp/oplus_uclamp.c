// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Oplus. All rights reserved.
 * File: oplus_uclamp.c
 * Description: set uclamp by pid
 * Author: yinzhibin
 * Create Date: 2025-10-20
 */

#include "linux/err.h"
#include "linux/types.h"
#include <linux/module.h>
#include <linux/version.h>
#include <linux/compat.h>
#include <linux/proc_fs.h>
#include <linux/list.h>
#include <linux/cgroup.h>
#include <linux/jiffies.h>
#include <linux/sched/prio.h>
#include <linux/sched/cputime.h>
#include <kernel/sched/sched.h>
#include <uapi/linux/sched/types.h>
#include <trace/hooks/sched.h>

#define OPLUS_UCLAMP_DIR "oplus_uc"
#define OPLUS_UCLAMP_CTRL "oplus_uclamp_ctrl"
#define OPLUS_UCLAMP_RANGE_MAX 1024
#define OPLUS_UCLAMP_RANGE_MIN 0
#define OPLUS_UCLAMP_MAX_TASKS 16
#define RESTORE_DELAY_MS 10000
#define VERSION "1.0"

struct oplus_uc_entry {
	pid_t oplus_uc_pid;
	int oplus_uc_min;
	int oplus_uc_max;
	struct list_head list;
	unsigned long expire_jiffies;
};

static struct proc_dir_entry *oplus_uc_dir;
static struct delayed_work oplus_uc_work;
static DEFINE_MUTEX(oplus_uc_lock);
static LIST_HEAD(oplus_uc_list);

static void oplus_uc_resched_work_locked(void)
{
	struct oplus_uc_entry *entry;
	unsigned long next = 0;
	bool have_expire = false;

	list_for_each_entry(entry, &oplus_uc_list, list) {
		if (!have_expire || time_before(entry->expire_jiffies, next)) {
			next = entry->expire_jiffies;
			have_expire = true;
		}
	}

	cancel_delayed_work(&oplus_uc_work);
	if (have_expire) {
		unsigned long delay = time_after(next, jiffies) ? (next - jiffies) : 0;
		schedule_delayed_work(&oplus_uc_work, delay);
	}
}

static int set_task_uclamp(struct task_struct *task, unsigned int min, unsigned int max)
{
	unsigned long cur_min = 0, cur_max = 0;
	u64 uclamp_flag = SCHED_FLAG_UTIL_CLAMP;
	struct sched_attr attr = {};

	if (!uclamp_flag || !task || !pid_alive(task))
		return -EINVAL;

	attr.sched_policy = task->policy;
	attr.sched_flags = SCHED_FLAG_KEEP_ALL | uclamp_flag | SCHED_FLAG_RESET_ON_FORK;
	attr.sched_util_min = min;
	attr.sched_util_max = max;

	cur_min = uclamp_eff_value(task, UCLAMP_MIN);
	cur_max = uclamp_eff_value(task, UCLAMP_MAX);

	if (cur_min != attr.sched_util_min || cur_max != attr.sched_util_max) {
		if (rt_policy(task->policy))
			attr.sched_priority = task->rt_priority;
		sched_setattr_nocheck(task, &attr);
	}

	return 0;
}

static bool set_task_uclamp_by_pid(int pid, int min, int max)
{
	struct task_struct *p, *t;

	rcu_read_lock();
	p = find_task_by_vpid(pid);
	if (IS_ERR_OR_NULL(p)) {
		rcu_read_unlock();
		return false;
	}
	get_task_struct(p);
	rcu_read_unlock();

	for_each_thread(p, t)
		set_task_uclamp(t, min, max);

	put_task_struct(p);

	return true;
}

static void oplus_uc_work_fn(struct work_struct *work)
{
	LIST_HEAD(to_free);
	struct oplus_uc_entry *entry, *tmp;

	mutex_lock(&oplus_uc_lock);
	list_for_each_entry_safe(entry, tmp, &oplus_uc_list, list) {
		if (time_after_eq(jiffies, entry->expire_jiffies)) {
			list_del(&entry->list);
			list_add_tail(&entry->list, &to_free);
		}
	}
	oplus_uc_resched_work_locked();
	mutex_unlock(&oplus_uc_lock);

	list_for_each_entry_safe(entry, tmp, &to_free, list) {
		if (!set_task_uclamp_by_pid(entry->oplus_uc_pid, OPLUS_UCLAMP_RANGE_MIN, OPLUS_UCLAMP_RANGE_MAX)) {
			pr_debug("restore skipped: pid=%d not found\n", entry->oplus_uc_pid);
		}
		list_del(&entry->list);
		kfree(entry);
	}
}

static struct oplus_uc_entry *oplus_uc_find_node_by_pid(pid_t pid)
{
	struct oplus_uc_entry *entry;
	list_for_each_entry(entry, &oplus_uc_list, list) {
		if (entry->oplus_uc_pid == pid)
			return entry;
	}
	return NULL;
}

static void oplus_uc_cleanup_list(void)
{
	struct oplus_uc_entry *entry, *tmp;

	mutex_lock(&oplus_uc_lock);
	list_for_each_entry_safe(entry, tmp, &oplus_uc_list, list) {
		list_del(&entry->list);
		set_task_uclamp_by_pid(entry->oplus_uc_pid, OPLUS_UCLAMP_RANGE_MIN, OPLUS_UCLAMP_RANGE_MAX);
		kfree(entry);
	}
	mutex_unlock(&oplus_uc_lock);
}

static int oplus_uc_list_update_node(int pid, int min, int max, int timeout_ms, int count)
{
	/*update the node if it exists*/
	struct oplus_uc_entry *entry = NULL;
	int size = 0;

	mutex_lock(&oplus_uc_lock);
	entry = oplus_uc_find_node_by_pid(pid);
	if (!entry) {
		list_for_each_entry(entry, &oplus_uc_list, list)
			size++;
		if (size >= OPLUS_UCLAMP_MAX_TASKS) {
			mutex_unlock(&oplus_uc_lock);
			return -ENOSPC;
		}
	}
	mutex_unlock(&oplus_uc_lock);

	if (!set_task_uclamp_by_pid(pid, min, max))
		return -ESRCH;

	mutex_lock(&oplus_uc_lock);
	entry = oplus_uc_find_node_by_pid(pid);
	if (!entry) {
		/*create the node*/
		entry = kzalloc(sizeof(*entry), GFP_KERNEL);
		if (!entry) {
			mutex_unlock(&oplus_uc_lock);
			set_task_uclamp_by_pid(pid, OPLUS_UCLAMP_RANGE_MIN, OPLUS_UCLAMP_RANGE_MAX);
			return -ENOMEM;
		}
		entry->oplus_uc_pid = pid;
		INIT_LIST_HEAD(&entry->list);
		list_add_tail(&entry->list, &oplus_uc_list);
	}
	entry->oplus_uc_min = min;
	entry->oplus_uc_max = max;
	entry->expire_jiffies = jiffies + msecs_to_jiffies(timeout_ms);

	oplus_uc_resched_work_locked();
	mutex_unlock(&oplus_uc_lock);

	return count;
}

static int oplus_uc_list_release_node(int pid, int count)
{
	/*release the node if it exists*/
	struct oplus_uc_entry *entry = NULL;

	set_task_uclamp_by_pid(pid, OPLUS_UCLAMP_RANGE_MIN, OPLUS_UCLAMP_RANGE_MAX);
	mutex_lock(&oplus_uc_lock);
	entry = oplus_uc_find_node_by_pid(pid);

	if (entry) {
		list_del(&entry->list);
		kfree(entry);
		oplus_uc_resched_work_locked();
	}
	mutex_unlock(&oplus_uc_lock);

	return count;
}

static ssize_t oplus_uc_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	char buffer[32] = {0};
	int timeout_ms = 0;
	int ret = 0;
	int pid = 0;
	int min = 0;
	int max = 0;

	if (count == 0 || count >= sizeof(buffer))
		return -EINVAL;

	ret = simple_write_to_buffer(buffer, sizeof(buffer), ppos, buf, count);
	if (ret <= 0)
		return -EINVAL;

	ret = sscanf(buffer, "%d:%d-%d:%d", &pid, &min, &max, &timeout_ms);
	if (ret != 4) {
		pr_err("Invalid Input Param!\n");
		return -EINVAL;
	}

	if (timeout_ms <= 0)
		timeout_ms = RESTORE_DELAY_MS;

	if (min < OPLUS_UCLAMP_RANGE_MIN || max > OPLUS_UCLAMP_RANGE_MAX || min > max)
		return -EINVAL;

	if (min == OPLUS_UCLAMP_RANGE_MIN && max == OPLUS_UCLAMP_RANGE_MAX)
		return oplus_uc_list_release_node(pid, count);

	return oplus_uc_list_update_node(pid, min, max, timeout_ms, count);
}

static ssize_t oplus_uc_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	char *kbuf;
	int len = 0;
	struct oplus_uc_entry *entry;

	kbuf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	mutex_lock(&oplus_uc_lock);
	list_for_each_entry(entry, &oplus_uc_list, list) {
		len += scnprintf(kbuf + len, PAGE_SIZE - len,
		"%d:%d-%d\n", entry->oplus_uc_pid, entry->oplus_uc_min, entry->oplus_uc_max);
	}
	mutex_unlock(&oplus_uc_lock);

	len = simple_read_from_buffer(buf, count, ppos, kbuf, len);
	kfree(kbuf);
	return len;
}

static const struct proc_ops oplus_uc_fops = {
	.proc_read = oplus_uc_read,
	.proc_write = oplus_uc_write,
	.proc_lseek = default_llseek,
};

static int __init oplus_uclamp_init(void)
{
	int ret = 0;
	struct proc_dir_entry *proc_node;

	oplus_uc_dir = proc_mkdir(OPLUS_UCLAMP_DIR, NULL);
	if (!oplus_uc_dir) {
		pr_err("Couldn't create dir /proc/%s!", OPLUS_UCLAMP_DIR);
		ret = -ENOMEM;
		goto err_create_dir;
	}

	proc_node = proc_create_data(OPLUS_UCLAMP_CTRL, 0666, oplus_uc_dir, &oplus_uc_fops, NULL);
	if (!proc_node) {
		pr_err("Couldn't create node /proc/%s/%s!", OPLUS_UCLAMP_DIR, OPLUS_UCLAMP_CTRL);
		ret = -ENOMEM;
		goto err_create_node;
	}

	INIT_DELAYED_WORK(&oplus_uc_work, oplus_uc_work_fn);

	pr_info("version %s init successfully.", VERSION);

	return ret;

err_create_node:
	remove_proc_entry(OPLUS_UCLAMP_DIR, NULL);
err_create_dir:
	return ret;
}

static void __exit oplus_uclamp_exit(void)
{
	cancel_delayed_work_sync(&oplus_uc_work);
	oplus_uc_cleanup_list();

	if (oplus_uc_dir) {
		remove_proc_entry(OPLUS_UCLAMP_CTRL, oplus_uc_dir);
		remove_proc_entry(OPLUS_UCLAMP_DIR, NULL);
	}

	pr_info("exit successfully.");
}

module_init(oplus_uclamp_init);
module_exit(oplus_uclamp_exit);
MODULE_DESCRIPTION("Oplus Uclamp Driver");
MODULE_LICENSE("GPL v2");
