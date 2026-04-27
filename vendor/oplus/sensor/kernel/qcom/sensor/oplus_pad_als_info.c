/* SPDX-License-Identifier: GPL-2.0-only  */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/sysfs.h>

#include <linux/syscalls.h>
#include <linux/unistd.h>
#include <linux/version.h>

#define PAD_ALS_NODE_LEN 32

extern struct proc_dir_entry *sensor_proc_dir;
struct oplus_pad_als_data {
    char red_max_lux[PAD_ALS_NODE_LEN];
    char green_max_lux[PAD_ALS_NODE_LEN];
    char blue_max_lux[PAD_ALS_NODE_LEN];
    char white_max_lux[PAD_ALS_NODE_LEN];
    char cali_coe[PAD_ALS_NODE_LEN];
    char row_coe[PAD_ALS_NODE_LEN];

    struct proc_dir_entry       *proc_pad_als;
};
static struct oplus_pad_als_data *gdata = NULL;

static ssize_t red_max_lux_read_proc(struct file *file, char __user *buf,
    size_t count, loff_t *off)
{
    char page[256] = {0};
    int len = 0;

    if (!gdata) {
        return -ENOMEM;
    }

    len = snprintf(page, sizeof(page), "%s", gdata->red_max_lux);

    if (len > *off) {
        len -= *off;
    } else {
        len = 0;
    }

    if (copy_to_user(buf, page, (len < count ? len : count))) {
        return -EFAULT;
    }

    *off += len < count ? len : count;
    return (len < count ? len : count);
}
static ssize_t red_max_lux_write_proc(struct file *file, const char __user *buf,
    size_t count, loff_t *off)

{
    char page[256] = {0};

    if (!gdata) {
        return -ENOMEM;
    }


    if (count > 256) {
        count = 256;
    }

    if (count > *off) {
        count -= *off;
    } else {
        count = 0;
    }

    if (copy_from_user(page, buf, count)) {
        return -EFAULT;
    }

    *off += count;

    strncpy(gdata->red_max_lux, page, PAD_ALS_NODE_LEN);

    return count;
}

static ssize_t white_max_lux_read_proc(struct file *file, char __user *buf,
    size_t count, loff_t *off)
{
    char page[256] = {0};
    int len = 0;

    if (!gdata) {
        return -ENOMEM;
    }

    len = snprintf(page, sizeof(page), "%s", gdata->white_max_lux);

    if (len > *off) {
        len -= *off;
    } else {
        len = 0;
    }

    if (copy_to_user(buf, page, (len < count ? len : count))) {
        return -EFAULT;
    }

    *off += len < count ? len : count;
    return (len < count ? len : count);
}
static ssize_t white_max_lux_write_proc(struct file *file, const char __user *buf,
    size_t count, loff_t *off)

{
    char page[256] = {0};

    if (!gdata) {
        return -ENOMEM;
    }


    if (count > 256) {
        count = 256;
    }

    if (count > *off) {
        count -= *off;
    } else {
        count = 0;
    }

    if (copy_from_user(page, buf, count)) {
        return -EFAULT;
    }

    *off += count;

    strncpy(gdata->white_max_lux, page, PAD_ALS_NODE_LEN);

    return count;
}

static ssize_t blue_max_lux_read_proc(struct file *file, char __user *buf,
    size_t count, loff_t *off)
{
    char page[256] = {0};
    int len = 0;

    if (!gdata) {
        return -ENOMEM;
    }

    len = snprintf(page, sizeof(page), "%s", gdata->blue_max_lux);

    if (len > *off) {
        len -= *off;
    } else {
        len = 0;
    }

    if (copy_to_user(buf, page, (len < count ? len : count))) {
        return -EFAULT;
    }

    *off += len < count ? len : count;
    return (len < count ? len : count);
}
static ssize_t blue_max_lux_write_proc(struct file *file, const char __user *buf,
    size_t count, loff_t *off)

{
    char page[256] = {0};

    if (!gdata) {
        return -ENOMEM;
    }


    if (count > 256) {
        count = 256;
    }

    if (count > *off) {
        count -= *off;
    } else {
        count = 0;
    }

    if (copy_from_user(page, buf, count)) {
        return -EFAULT;
    }

    *off += count;

    strncpy(gdata->blue_max_lux, page, PAD_ALS_NODE_LEN);

    return count;
}

static ssize_t green_max_lux_read_proc(struct file *file, char __user *buf,
    size_t count, loff_t *off)
{
    char page[256] = {0};
    int len = 0;

    if (!gdata) {
        return -ENOMEM;
    }

    len = snprintf(page, sizeof(page), "%s", gdata->green_max_lux);

    if (len > *off) {
        len -= *off;
    } else {
        len = 0;
    }

    if (copy_to_user(buf, page, (len < count ? len : count))) {
        return -EFAULT;
    }

    *off += len < count ? len : count;
    return (len < count ? len : count);
}
static ssize_t green_max_lux_write_proc(struct file *file, const char __user *buf,
    size_t count, loff_t *off)

{
    char page[256] = {0};

    if (!gdata) {
        return -ENOMEM;
    }


    if (count > 256) {
        count = 256;
    }

    if (count > *off) {
        count -= *off;
    } else {
        count = 0;
    }

    if (copy_from_user(page, buf, count)) {
        return -EFAULT;
    }

    *off += count;

    strncpy(gdata->green_max_lux, page, PAD_ALS_NODE_LEN);

    return count;
}

static ssize_t cali_coe_read_proc(struct file *file, char __user *buf,
    size_t count, loff_t *off)
{
    char page[256] = {0};
    int len = 0;

    if (!gdata) {
        return -ENOMEM;
    }

    len = snprintf(page, sizeof(page), "%s", gdata->cali_coe);

    if (len > *off) {
        len -= *off;
    } else {
        len = 0;
    }

    if (copy_to_user(buf, page, (len < count ? len : count))) {
        return -EFAULT;
    }

    *off += len < count ? len : count;
    return (len < count ? len : count);
}
static ssize_t cali_coe_write_proc(struct file *file, const char __user *buf,
    size_t count, loff_t *off)

{
    char page[256] = {0};

    if (!gdata) {
        return -ENOMEM;
    }


    if (count > 256) {
        count = 256;
    }

    if (count > *off) {
        count -= *off;
    } else {
        count = 0;
    }

    if (copy_from_user(page, buf, count)) {
        return -EFAULT;
    }

    *off += count;

    strncpy(gdata->cali_coe, page, PAD_ALS_NODE_LEN);

    return count;
}

static ssize_t row_coe_read_proc(struct file *file, char __user *buf,
    size_t count, loff_t *off)
{
    char page[256] = {0};
    int len = 0;

    if (!gdata) {
        return -ENOMEM;
    }

    len = snprintf(page, sizeof(page), "%s", gdata->row_coe);

    if (len > *off) {
        len -= *off;
    } else {
        len = 0;
    }

    if (copy_to_user(buf, page, (len < count ? len : count))) {
        return -EFAULT;
    }

    *off += len < count ? len : count;
    return (len < count ? len : count);
}
static ssize_t row_coe_write_proc(struct file *file, const char __user *buf,
    size_t count, loff_t *off)

{
    char page[256] = {0};

    if (!gdata) {
        return -ENOMEM;
    }


    if (count > 256) {
        count = 256;
    }

    if (count > *off) {
        count -= *off;
    } else {
        count = 0;
    }

    if (copy_from_user(page, buf, count)) {
        return -EFAULT;
    }

    *off += count;

    strncpy(gdata->row_coe, page, PAD_ALS_NODE_LEN);

    return count;
}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
static const struct proc_ops row_coe_fops = {
        .proc_read = row_coe_read_proc,
        .proc_write = row_coe_write_proc,
        .proc_lseek = default_llseek,
};

static const struct proc_ops red_max_lux_fops = {
        .proc_read = red_max_lux_read_proc,
        .proc_write = red_max_lux_write_proc,
        .proc_lseek = default_llseek,
};

static const struct proc_ops white_max_lux_fops = {
        .proc_read = white_max_lux_read_proc,
        .proc_write = white_max_lux_write_proc,
        .proc_lseek = default_llseek,
};

static const struct proc_ops blue_max_lux_fops = {
        .proc_read = blue_max_lux_read_proc,
        .proc_write = blue_max_lux_write_proc,
        .proc_lseek = default_llseek,
};

static const struct proc_ops green_max_lux_fops = {
        .proc_read = green_max_lux_read_proc,
        .proc_write = green_max_lux_write_proc,
        .proc_lseek = default_llseek,
};

static const struct proc_ops cali_coe_fops = {
        .proc_read = cali_coe_read_proc,
        .proc_write = cali_coe_write_proc,
        .proc_lseek = default_llseek,
};
#else
static struct file_operations row_coe_fops = {
        .read = row_coe_read_proc,
        .write = row_coe_write_proc,
};

static struct file_operations red_max_lux_fops = {
        .read = red_max_lux_read_proc,
        .write = red_max_lux_write_proc,
};

static struct file_operations white_max_lux_fops = {
        .read = white_max_lux_read_proc,
        .write = white_max_lux_write_proc,
};

static struct file_operations blue_max_lux_fops = {
        .read = blue_max_lux_read_proc,
        .write = blue_max_lux_write_proc,
};

static struct file_operations green_max_lux_fops = {
        .read = green_max_lux_read_proc,
        .write = green_max_lux_write_proc,
};

static struct file_operations cali_coe_fops = {
        .read = cali_coe_read_proc,
        .write = cali_coe_write_proc,
};
#endif

//call init api in oplus_sensor_devinfo.c
//due to kernel module only permit one module_init entrance in one .ko
MODULE_DESCRIPTION("custom version");
MODULE_LICENSE("GPL v2");
