// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
/**********************************************************************************
* Description:     escape mode Kernel Driver
*
* Version   : 1.0
***********************************************************************************/
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/moduleparam.h>
#include <linux/jiffies.h>
#include <linux/version.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/seq_file.h>

#define MAX_CMDLINE_PARAM_LEN 1024
#define SEQ_printf(m, x...)                                                    \
	do {                                                                   \
		if (m)                                                         \
			seq_printf(m, x);                                      \
		else                                                           \
			pr_debug(x);                                           \
	} while (0)

static bool escape_mode_enable = false;

static void check_escape_mode(void)
{
	struct device_node *of_chosen = NULL;
	char *bootargs = NULL;

	of_chosen = of_find_node_by_path("/chosen");
	if (!of_chosen)
		return;
	bootargs = (char *)of_get_property(of_chosen, "bootargs", NULL);
	if (!bootargs)
		return;

	if (strstr(bootargs, "escape_mode=1")) {
		escape_mode_enable = true;
	}
}

static int escape_mode_show(struct seq_file *m, void *v)
{
	if (escape_mode_enable) {
		SEQ_printf(m, "%x\n", 1);
	} else {
		SEQ_printf(m, "%x\n", 0);
	}

	return 0;
}

static int escape_mode_open(struct inode *inode, struct file *file)
{
	return single_open(file, escape_mode_show, inode->i_private);
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
	static const struct file_operations escape_mode_fops = {
	.open = escape_mode_open,
	.write = NULL,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};
#else
	static const struct proc_ops escape_mode_fops = {
	.proc_open = escape_mode_open,
	.proc_write = NULL,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};
#endif

static int __init oplus_escape_mode_init(void)
{
	struct proc_dir_entry * pentry;

	pentry = proc_create("escape_mode", 0444, NULL, &escape_mode_fops);

	if (!pentry) {
		pr_err("escape_mode proc node create fail\n");
		return -ENOENT;
	}

	check_escape_mode();

	return 0;
}

static void __exit oplus_escape_mode_exit(void)
{
	pr_err("exit escape mode info register\n");
}

late_initcall(oplus_escape_mode_init);
module_exit(oplus_escape_mode_exit);

MODULE_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver);

MODULE_DESCRIPTION("OPLUS escape mode");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("");
