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
#include <linux/kdev_t.h>
#include <linux/major.h>
#include <linux/proc_fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/writeback.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/moduleparam.h>
#include <linux/jiffies.h>
#include <linux/version.h>
#include <linux/rtc.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/of_address.h>
#include <linux/of_device.h>

#include "oplus_phoenix.h"

#define OPLUSRESERVE1 "oplusreserve1"
#define PARTLABEL_OPLUS_RESERVE_1 "PARTLABEL=oplusreserve1"
#define PART_ESCAPE_MODE "/dev/block/by-name/oplusreserve1"
#define OPLUS_HANG_INFO_SECTOR_START_UFS   (1321*4096) // escape offset
#define OPLUS_HANG_INFO_SECTOR_START_EMMC   (10014*512)
#define RETRY_COUNT_FOR_GET_DEVICE 3
#define WAITING_FOR_GET_DEVICE 1000

static bool ufs_flag = true;
static unsigned int hang_flag = 1;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
static struct file *get_reserve_partition_bdev(void)
{
	int retry_wait_for_device = RETRY_COUNT_FOR_GET_DEVICE;
	dev_t dev;
	struct file *bdev_file = NULL;
	int ret = 0;

	while (retry_wait_for_device--) {
		ret = lookup_bdev(PART_ESCAPE_MODE, &dev);
		if (ret) {
			msleep_interruptible(WAITING_FOR_GET_DEVICE);
			pr_err("failed to get bdev!,ret = %d\n", ret);
			continue;
		}
		if (dev != 0) {
			bdev_file = bdev_file_open_by_dev(dev, BLK_OPEN_READ | BLK_OPEN_WRITE | BLK_OPEN_EXCL, THIS_MODULE, NULL);
			if (!IS_ERR(bdev_file)) {
				pr_err("success to get dev block\n");
				return bdev_file;
			}
		}
		pr_err("Failed to get dev block, retry %d\n", retry_wait_for_device);
		msleep_interruptible(WAITING_FOR_GET_DEVICE);
	}
	pr_err("Failed to get dev block final\n");
	return NULL;
}
#else
static struct block_device *get_reserve_partition_bdev(void)
{
	struct block_device *bdev = NULL;
	int retry_wait_for_device = RETRY_COUNT_FOR_GET_DEVICE;
	dev_t dev;
	int ret = 0;

	while (retry_wait_for_device--) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
		ret = lookup_bdev(PART_ESCAPE_MODE, &dev);
		if (ret) {
			msleep_interruptible(WAITING_FOR_GET_DEVICE);
			pr_err("failed to get bdev!, ret = %d \n", ret);
			continue;
		}
#else
		dev = name_to_dev_t(PARTLABEL_OPLUS_RESERVE_1);
#endif
		if (dev != 0) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
			bdev = blkdev_get_by_dev(dev, BLK_OPEN_READ | BLK_OPEN_WRITE | BLK_OPEN_EXCL, THIS_MODULE, NULL);
#else
			bdev = blkdev_get_by_dev(dev, FMODE_READ | FMODE_WRITE | FMODE_EXCL, THIS_MODULE);
#endif
			if (!IS_ERR(bdev)) {
				pr_err("success to get dev block\n");
				return bdev;
			}
		}
		pr_err("Failed to get dev block, retry %d\n", retry_wait_for_device);
		msleep_interruptible(WAITING_FOR_GET_DEVICE);
	}
	pr_err("Failed to get dev block final\n");
	return NULL;
}
#endif

#if LINUX_VERSION_CODE > KERNEL_VERSION(5, 15, 0)
static int blkdev_fsync(struct file *filp, loff_t start, loff_t end,
						int datasync)
{
	struct inode *bd_inode = filp->f_mapping->host;
	struct block_device *bdev = I_BDEV(bd_inode);
	int error;

	error = file_write_and_wait_range(filp, start, end);
	if (error)
		return error;

	/*
	 * There is no need to serialise calls to blkdev_issue_flush with
	 * i_mutex and doing so causes performance issues with concurrent
	 * O_SYNC writers to a block device.
	 */
	error = blkdev_issue_flush(bdev);
	if (error == -EOPNOTSUPP)
		error = 0;

	return error;
}
#endif

static int write_info(struct block_device *bdev, loff_t ki_pos, void *iov_base, size_t iov_len)
{
	struct kiocb kiocb;
	struct iov_iter iter;
	struct kvec iov;
	const struct file_operations f_op = {.fsync = blkdev_fsync};
	struct file dev_map_file;
	int ret = 0;

	memset(&dev_map_file, 0, sizeof(struct file));

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
	struct inode *bdev_inode = bdev->bd_mapping->host;
	dev_map_file.f_mapping = bdev_inode->i_mapping;
	dev_map_file.f_flags = O_DSYNC | __O_SYNC | O_NOATIME;
	dev_map_file.f_inode = bdev_inode;
#else
	dev_map_file.f_mapping = bdev->bd_inode->i_mapping;
	dev_map_file.f_flags = O_DSYNC | __O_SYNC | O_NOATIME;
	dev_map_file.f_inode = bdev->bd_inode;
#endif

#if LINUX_VERSION_CODE > KERNEL_VERSION(6, 1, 0)
	dev_map_file.f_iocb_flags = IOCB_DSYNC;
#endif

	init_sync_kiocb(&kiocb, &dev_map_file);
	kiocb.ki_pos = ki_pos;
	iov.iov_base = iov_base;
	iov.iov_len = iov_len;
	iov_iter_kvec(&iter, WRITE, &iov, 1, iov_len);

	ret = generic_write_checks(&kiocb, &iter);
	if (ret <= 0) {
		pr_err("generic_write_checks failed, ret=%d\n", ret);
		return ret;
	}

#if LINUX_VERSION_CODE > KERNEL_VERSION(6, 1, 0)
	ret = generic_perform_write(&kiocb, &iter);
#else
	ret = generic_perform_write(&dev_map_file, &iter, kiocb.ki_pos);
#endif
	if (ret <= 0) {
		pr_err("generic_perform_write failed, ret=%d\n", ret);
		return ret;
	}

	dev_map_file.f_op = &f_op;
	kiocb.ki_pos += ret;

	ret = generic_write_sync(&kiocb, ret);
	if (ret < 0) {
		pr_err("generic_write_sync failed, ret=%d\n", ret);
		return ret;
	}

	return 0;
}

int phx_mark_boot_complete(void)
{
        struct block_device *bdev = NULL;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
        struct file *bdev_file = NULL;
#endif
        int ret = 0;
	int flag = 0;
	loff_t ki_pos;

        if (ufs_flag) {
                ki_pos = OPLUS_HANG_INFO_SECTOR_START_UFS;   // start hang flag
        } else {
                ki_pos = OPLUS_HANG_INFO_SECTOR_START_EMMC;
        }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
        bdev_file = get_reserve_partition_bdev();
        if (bdev_file) {
                bdev = file_bdev(bdev_file);
        }
#else
        bdev = get_reserve_partition_bdev();
#endif
        if (bdev) {
                ret = write_info(bdev, ki_pos, &hang_flag, sizeof(unsigned int));
                if (ret) {
                        pr_err("write hang flag failed, ret = %d\n", ret);
                } else {
                        pr_info("write hang flag success\n");
			flag = 1;
                }
        } else {
                pr_err("get op1 bdev err\n");
                return 0;
        }
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
	if (bdev_file) {
		fput(bdev_file);
	}
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
        blkdev_put(bdev, THIS_MODULE);
#else
        blkdev_put(bdev, FMODE_READ | FMODE_WRITE | FMODE_EXCL);
#endif
        return flag;
}
EXPORT_SYMBOL(phx_mark_boot_complete);

MODULE_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver);

MODULE_DESCRIPTION("OPLUS phoenix escape mode");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("");
