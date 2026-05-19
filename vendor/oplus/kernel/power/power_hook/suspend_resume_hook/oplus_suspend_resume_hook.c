#include <linux/proc_fs.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/printk.h>
#include <linux/atomic.h>
#include <linux/hardirq.h>
#include <linux/plist.h>
#include <linux/mutex.h>
#include <linux/tracepoint.h>
#include <linux/rtc.h>
#include <linux/timekeeping.h>
#include <linux/time.h>
#include <linux/suspend.h>
#include <trace/events/power.h>
#include "../utils/oplus_power_hook_utils.h"

#define OPLUS_SUSPEND_RESUME_PROC_DIR "oplus_suspend_resume_debug"

static atomic_t suspend_resume_debug_on = ATOMIC_INIT(1);
static struct proc_dir_entry *suspend_resume_proc = NULL;
static struct proc_dir_entry *suspend_resume_debug_proc = NULL;
static DEFINE_MUTEX(suspend_resume_debug_mutex);

static void suspend_resume_trace_probe(void *unused, const char *action, int val, bool start);

static struct tracepoints_table suspend_resume_tracepoints_table[] = {
	{
	.name = "suspend_resume",
	.func = suspend_resume_trace_probe,
	},
};

static ssize_t suspend_resume_debug_on_write(struct file *filp,
		const char __user *buff, size_t len, loff_t *data)
{
	char buf[10] = {0};
	unsigned int val = 0;
	int tpt_size;
	int old_state;
	int new_state;

	tpt_size = sizeof(suspend_resume_tracepoints_table) /
			sizeof(suspend_resume_tracepoints_table[0]);

	if (len == 0 || len >= sizeof(buf))
		return -EFAULT;

	if (copy_from_user((char *)buf, buff, len))
		return -EFAULT;

	buf[len] = '\0';

	if (kstrtouint(buf, 0, &val))
		return -EINVAL;

	mutex_lock(&suspend_resume_debug_mutex);

	old_state = atomic_read(&suspend_resume_debug_on);
	new_state = !!(val);

	if (old_state == 0 && new_state == 1) {
		find_and_register_tracepoint_probe(suspend_resume_tracepoints_table, tpt_size);
	} else if (old_state == 1 && new_state == 0) {
		unregister_tracepoint_probe(suspend_resume_tracepoints_table, tpt_size);
	}

	atomic_set(&suspend_resume_debug_on, new_state);

	mutex_unlock(&suspend_resume_debug_mutex);

	return len;
}

static int suspend_resume_debug_on_show(struct seq_file *seq_filp, void *v)
{
	seq_printf(seq_filp, "%d\n", atomic_read(&suspend_resume_debug_on));
	return 0;
}

static int suspend_resume_debug_on_open(struct inode *inode, struct file *file)
{
	int ret;

	ret = single_open(file, suspend_resume_debug_on_show, NULL);

	return ret;
}

static const struct proc_ops suspend_resume_debug_on_fops = {
	.proc_open		= suspend_resume_debug_on_open,
	.proc_write		= suspend_resume_debug_on_write,
	.proc_read		= seq_read,
};


static void print_utc_time(char *prefix)
{
	struct timespec64 utc_ts;
	struct tm utc_tm;

	/* Get and print UTC time */
	ktime_get_real_ts64(&utc_ts);
	time64_to_tm(utc_ts.tv_sec, 0, &utc_tm);

	pr_info("%s %04ld-%02d-%02d %02d:%02d:%02d.%09ld UTC\n", prefix,
		utc_tm.tm_year + 1900, utc_tm.tm_mon + 1,
		utc_tm.tm_mday, utc_tm.tm_hour,
		utc_tm.tm_min, utc_tm.tm_sec,
		utc_ts.tv_nsec);
}


void suspend_resume_trace_probe(void *unused, const char *action, int val, bool start)
{
	if (!atomic_read(&suspend_resume_debug_on))
		return;

	if ((strncmp(action, "suspend_enter", 13) == 0) &&
		(val == 1) && start) {
		print_utc_time("PM: suspend entry");
	} else if((strncmp(action, "thaw_processes", 14) == 0) &&
		(val == 0) && !start) {
		print_utc_time("PM: suspend exit");
	}
}

int oplus_suspend_resume_hook_init(void)
{
	int tpt_size;

	tpt_size = sizeof(suspend_resume_tracepoints_table) /
			sizeof(suspend_resume_tracepoints_table[0]);

	suspend_resume_proc = proc_mkdir(OPLUS_SUSPEND_RESUME_PROC_DIR, NULL);
	if(!suspend_resume_proc) {
		pr_info("[SUSPEND_RESUME_DEBUG] Failed create /proc/%s\n",
			OPLUS_SUSPEND_RESUME_PROC_DIR);
		goto out;
	}

	suspend_resume_debug_proc = proc_create("debug_on", 0666,
		suspend_resume_proc, &suspend_resume_debug_on_fops);
	if(!suspend_resume_debug_proc) {
		pr_info("[SUSPEND_RESUME_DEBUG] Failed create /proc/%s/debug_on\n",
			OPLUS_SUSPEND_RESUME_PROC_DIR);
		goto err_create_proc_node;
	}

	find_and_register_tracepoint_probe(suspend_resume_tracepoints_table, tpt_size);

	return 0;

err_create_proc_node:
	remove_proc_entry(OPLUS_SUSPEND_RESUME_PROC_DIR, NULL);
	suspend_resume_debug_proc = NULL;

out:
	return -ENOENT;
}

void oplus_suspend_resume_hook_exit(void)
{
	int tpt_size = 0;

	tpt_size = sizeof(suspend_resume_tracepoints_table) /
		sizeof(suspend_resume_tracepoints_table[0]);

	unregister_tracepoint_probe(suspend_resume_tracepoints_table, tpt_size);

	if (suspend_resume_proc) {
		if (suspend_resume_debug_proc) {
			remove_proc_entry("debug_on", suspend_resume_proc);
			suspend_resume_debug_proc = NULL;
		}
		remove_proc_entry(OPLUS_SUSPEND_RESUME_PROC_DIR, NULL);
		suspend_resume_proc = NULL;
	}
}
