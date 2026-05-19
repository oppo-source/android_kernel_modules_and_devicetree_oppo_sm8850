// SPDX-License-Identifier: GPL-2.0-only
/*
 * mm_misc, contain some memory management misc feature
 *
 * Copyright (C) 2025-2026 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "mm_misc: " fmt

#include <linux/types.h>
#include <linux/compiler_attributes.h>
#include <linux/mm.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/jump_label.h>
#include <linux/sched.h>
#include <linux/sched/cputime.h>
#include <linux/jiffies.h>
#include <linux/string.h>
#include <trace/hooks/mm.h>

#define KBUF_LEN 10

static bool is_digit_str(const char *str)
{
	return strspn(str, "0123456789") == strlen(str);
}

static ssize_t verify_and_parse_input(const char __user *buf,
	    size_t count, int *val)
{
	char kbuf[KBUF_LEN] = {0};
	char *str;

	if (count > KBUF_LEN - 1) {
		pr_warn("input too long\n");
		return -EINVAL;
	}

	if (copy_from_user(kbuf, buf, count))
		return -EINVAL;

	kbuf[count] = 0;
	str = strstrip(kbuf);
	if (*str == 0) {
		pr_warn("input empty\n");
		return -EINVAL;
	}

	if (!is_digit_str(str)) {
		pr_warn("input invalid, not a digit string\n");
		return -EINVAL;
	}

	if (kstrtoint(str, 0, val)) {
		pr_warn("not a valid number\n");
		return -EINVAL;
	}

	return 0;
}

#ifdef CONFIG_ALLOC_SLOWPATH_STAT
DEFINE_STATIC_KEY_FALSE(g_alloc_slowpath_stats);
static bool g_alloc_slowpath_stats_enabled;
static struct proc_dir_entry *g_alloc_slowpath_stats_entry;
static atomic64_t g_slowpath_total_ms;
static atomic64_t g_slowpath_running_ms;

static int alloc_slowpath_stats_show(struct seq_file *m, void *v)
{
	if (g_alloc_slowpath_stats_enabled) {
		seq_printf(m, "slowpath_total_ms: %lld\n", atomic64_read(&g_slowpath_total_ms));
		seq_printf(m, "slowpath_running_ms: %lld\n", atomic64_read(&g_slowpath_running_ms));
	} else {
		seq_printf(m, "0\n");
	}

	return 0;
}

static int alloc_slowpath_stats_open(struct inode *inode, struct file *file)
{
	return single_open(file, alloc_slowpath_stats_show, NULL);
}

static ssize_t alloc_slowpath_stats_write(struct file *file, const char __user *buf,
		size_t count, loff_t *ppos)
{
	ssize_t ret;
	int val;

	ret = verify_and_parse_input(buf, count, &val);
	if (ret != 0)
		return ret;

	g_alloc_slowpath_stats_enabled = !!val;
	if (g_alloc_slowpath_stats_enabled)
		static_branch_enable(&g_alloc_slowpath_stats);
	else
		static_branch_disable(&g_alloc_slowpath_stats);

	return count;
}

static const struct proc_ops proc_alloc_slowpath_stats_ops = {
	.proc_open = alloc_slowpath_stats_open,
	.proc_read = seq_read,
	.proc_write = alloc_slowpath_stats_write,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static void alloc_slowpath_start(void *data __maybe_unused, u64 *stime)
{
	if (static_branch_unlikely(&g_alloc_slowpath_stats)) {
		u64 utime;
		task_cputime(current, &utime, stime);
	}
}

static void alloc_slowpath_end(void *data __maybe_unused,
		gfp_t *gfp_mask __maybe_unused,
		unsigned int order __maybe_unused,
		unsigned long alloc_start,
		u64 stime,
		unsigned long did_some_progress __maybe_unused,
		unsigned long pages_reclaimed __maybe_unused,
		int retry_loop_count __maybe_unused)
{
	u64 utime, stime_end, running, total;

	if (!static_branch_unlikely(&g_alloc_slowpath_stats))
		return;

	task_cputime(current, &utime, &stime_end);
	running = (stime_end - stime) / NSEC_PER_MSEC;
	total = jiffies_to_msecs(jiffies - alloc_start);
	atomic64_add(running, &g_slowpath_running_ms);
	atomic64_add(total, &g_slowpath_total_ms);
}

static int register_alloc_slowpath_hooks(void)
{
	int ret;

	ret = register_trace_android_vh_alloc_pages_slowpath_start(alloc_slowpath_start, NULL);
	if (ret)
		return ret;

	ret = register_trace_android_vh_alloc_pages_slowpath_end(alloc_slowpath_end, NULL);
	if (ret)
		unregister_trace_android_vh_alloc_pages_slowpath_start(alloc_slowpath_start, NULL);

	return ret;
}

static void unregister_alloc_slowpath_hooks(void)
{
	unregister_trace_android_vh_alloc_pages_slowpath_start(alloc_slowpath_start, NULL);
	unregister_trace_android_vh_alloc_pages_slowpath_end(alloc_slowpath_end, NULL);
}

static int create_alloc_slowpath_stats_proc(void)
{
	struct proc_dir_entry *oplus_mem_dir;

	oplus_mem_dir = proc_mkdir("oplus_mem", NULL);
	if (!oplus_mem_dir)
		g_alloc_slowpath_stats_entry = proc_create("oplus_mem/alloc_slowpath_stats",
			0660, NULL, &proc_alloc_slowpath_stats_ops);
	else
		g_alloc_slowpath_stats_entry = proc_create("alloc_slowpath_stats",
			0660, oplus_mem_dir, &proc_alloc_slowpath_stats_ops);

	if (!g_alloc_slowpath_stats_entry) {
		pr_err("alloc_slowpath_stats_proc create failed, ENOMEM\n");
		return -ENOMEM;
	}

	return 0;
}

static void remove_alloc_slowpath_stats_proc(void)
{
	if (g_alloc_slowpath_stats_entry) {
		proc_remove(g_alloc_slowpath_stats_entry);
		g_alloc_slowpath_stats_entry = NULL;
	}
}
#else
static int register_alloc_slowpath_hooks(void)
{
	return 0;
}

static void unregister_alloc_slowpath_hooks(void)
{
}

static int create_alloc_slowpath_stats_proc(void)
{
}

static void remove_alloc_slowpath_stats_proc(void)
{
}
#endif

static int __init mm_misc_init(void)
{
	int ret = 0;

	ret = register_alloc_slowpath_hooks();
	if (ret) {
		pr_err("alloc_slowpath vendor_hook register failed: %d\n", ret);
		return ret;
	}

	ret = create_alloc_slowpath_stats_proc();
	if (ret) {
		pr_err("alloc_slowpath_stats proc create failed: %d\n", ret);
		unregister_alloc_slowpath_hooks();
		return ret;
	}

	pr_info("%s init done\n", __func__);
	return 0;
}

static void __exit mm_misc_exit(void)
{
	unregister_alloc_slowpath_hooks();
	remove_alloc_slowpath_stats_proc();
}

module_init(mm_misc_init);
module_exit(mm_misc_exit);
MODULE_LICENSE("GPL v2");
