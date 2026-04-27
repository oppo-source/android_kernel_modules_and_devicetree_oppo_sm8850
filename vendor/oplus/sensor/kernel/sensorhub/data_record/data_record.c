/******************************************************************
** Copyright (C), 2004-2021 OPLUS Mobile Comm Corp., Ltd.
** data_record
** File: - data_record.c
** Description: Source file for data_record sensorhub driver.
** Version: 1.0
**
** --------------------------- Revision History: ---------------------
* <version> <date>      <author>                <desc>
*******************************************************************/
#define pr_fmt(fmt) "<data_record>" fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/mm.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/poll.h>
#include <linux/atomic.h>
#include <linux/of.h>
#include <linux/time64.h>
#include <linux/kdev_t.h>
#include <linux/vmalloc.h>
#include <linux/proc_fs.h>
#include <linux/version.h>
#include <linux/timekeeping.h>
#include <linux/delay.h>
#include <linux/ktime.h>
#include <linux/workqueue.h>
#include <linux/timer.h>
#include "scp.h"
#include "sensor_comm.h"
#include "hf_manager.h"
#include "share_memory.h"
#include "data_record.h"

static data_record_manager_t g_data_record_manager;
data_record_t tmp_data_buf[BUFFER_COUNT];  // 200 4k
char tmp_string_buf[BUFFER_COUNT*SINGLE_STR_MAX_SIZE];  //9.7k

static int sensor_open(struct inode *inode, struct file *file)
{
    pr_info("%s:Sensor device opened\n", __func__);
    return 0;
}

static int sensor_release(struct inode *inode, struct file *file)
{
    pr_info("%s:Sensor device closed\n", __func__);
    return 0;
}

static uint64_t min_length(uint64_t a, uint64_t b)
{
    return (a < b) ? a : b;
}
static int sensor_read_check_status(data_record_manager_t* manager)
{
    if (!manager->scp_inited || !manager->kernel_enabled) {
        pr_err("%s:!inited or !enabled\n", __func__);
        return -1;
    }
    if (!manager->scp_enabled) {
        pr_err("%s:status not sync with scp\n", __func__);
        return -1;
    }
    return 0;
}

static ssize_t sensor_read_try_flush_pending(data_record_manager_t* manager, char __user *data,
                                           int *cpy_length, int *already_cpy_length)
{
    ssize_t this_cpy_length = 0;

    for (int i = 0; i < MAX_SEGMENTS; i++) {
        pr_info("%s:cpy_length[%d]:%d\n", __func__, i, cpy_length[i]);
        if (cpy_length[i] != 0) {
            if (copy_to_user(data, tmp_string_buf + *already_cpy_length, cpy_length[i])) {
                pr_err("%s:copy to user buf failed\n", __func__);
                return -EFAULT;
            }
            this_cpy_length = cpy_length[i];
            *already_cpy_length += cpy_length[i];
            cpy_length[i] = 0;
            return this_cpy_length;
        }
    }
    return 0;
}

static uint64_t sensor_read_sync_and_fetch(data_record_manager_t* manager, uint64_t *retry_count)
{
    volatile uint64_t *sync = &manager->shm_manager->sync_var;
    uint64_t first = 0, second = 0;
    uint64_t w_pos = 0, r_pos = 0;
    uint64_t copy_count = 0;

    while (*sync != 0) {
        (*retry_count)++;
        pr_err("%s:sync:%llu,retry_count:%llu\n", __func__, *sync, *retry_count);
        return 0;
    }
    *sync = 1;
    *retry_count = 0;
    smp_mb();
    pr_info("%s:sync:%llu\n", __func__, *sync);
    usleep_range(1000, 1100);
    if (*sync != 1) {
        pr_err("%s:sync != 1\n", __func__);
        return 0;
    }
    if (manager->shm_manager->count < BUFFER_COUNT) {
        *sync = 0;
        smp_mb();
        pr_info("%s:data not enough\n", __func__);
        return 0;
    }
    r_pos = manager->shm_manager->read_pos;
    w_pos = manager->shm_manager->write_pos;
    if (w_pos > r_pos) {
        first = w_pos - r_pos;
        second = 0;
    } else {
        first = (uint64_t)MAX_SENSOR_DATA_RECORD_NUM_SHEM - r_pos;
        second = w_pos;
    }
    first = min_length(first, (uint64_t)BUFFER_COUNT);
    second = min_length((uint64_t)BUFFER_COUNT - first, second);
    copy_count = first + second;
    pr_info("%s:first:%llu,second:%llu,copy_count:%llu\n", __func__, first, second, copy_count);

    memcpy_fromio(&tmp_data_buf[0], &manager->shm_ptr[r_pos], first * sizeof(data_record_t));
    memcpy_fromio(&tmp_data_buf[first], &manager->shm_ptr[0], second * sizeof(data_record_t));

    r_pos += copy_count;
    if (r_pos >= (uint64_t)MAX_SENSOR_DATA_RECORD_NUM_SHEM) {
        r_pos -= (uint64_t)MAX_SENSOR_DATA_RECORD_NUM_SHEM;
    }
    manager->shm_manager->read_pos = r_pos;
    manager->shm_manager->count = (uint64_t)(manager->shm_manager->count - (uint64_t)copy_count);
    smp_mb();
    *sync = 0;

    return copy_count;
}

static ssize_t sensor_read_format_and_flush_new(data_record_manager_t* manager, char __user *data,
                                              uint64_t copy_count, int *cpy_length, int *already_cpy_length)
{
    int tmp_length = 0;
    int total_string_length = 0;
    ssize_t this_cpy_length = 0;

    for (uint64_t i = 0; i < copy_count; i++) {
        tmp_length = snprintf(tmp_string_buf + total_string_length, SINGLE_STR_MAX_SIZE, "%llu,%d,%d,%d,%d\n",
                            tmp_data_buf[i].timestamp,
                            tmp_data_buf[i].sensor_type,
                            tmp_data_buf[i].x,
                            tmp_data_buf[i].y,
                            tmp_data_buf[i].z);
        total_string_length += tmp_length;
    }

    if (total_string_length > 0 && total_string_length <= MAX_TOTAL_SIZE) {
        int remaining = total_string_length;
        for (int i = 0; i < MAX_SEGMENTS && remaining > 0; i++) {
            cpy_length[i] = (remaining > MAX_SEGMENT_SIZE) ? MAX_SEGMENT_SIZE : remaining;
            remaining -= cpy_length[i];
        }
    }
    if (cpy_length[0] != 0) {
        if (copy_to_user(data, tmp_string_buf + *already_cpy_length, cpy_length[0])) {
            pr_err("%s:copy to user buf failed (immediate)\n", __func__);
            return -EFAULT;
        }
        this_cpy_length = cpy_length[0];
        *already_cpy_length += cpy_length[0];
        cpy_length[0] = 0;
        return this_cpy_length;
    }
    return 0;
}

static ssize_t sensor_read(struct file *file, char __user *data, size_t len, loff_t *ppos)
{
    static int already_cpy_length = 0;
    static int cpy_length[MAX_SEGMENTS] = {0};
    static uint64_t retry_count = 0;
    data_record_manager_t* manager = &g_data_record_manager;
    ssize_t ret = 0;
    uint64_t copy_count = 0;

    if (sensor_read_check_status(manager) < 0) {
        return 0;
    }

    mutex_lock(&manager->lock);

    ret = sensor_read_try_flush_pending(manager, data, cpy_length, &already_cpy_length);
    if (ret != 0) {
        mutex_unlock(&manager->lock);
        return ret;
    }

    // No pending data, reset buffers
    already_cpy_length = 0;
    memset(cpy_length, 0, sizeof(cpy_length));
    memset(tmp_data_buf, 0, sizeof(tmp_data_buf));
    memset(tmp_string_buf, 0, sizeof(tmp_string_buf));

    copy_count = sensor_read_sync_and_fetch(manager, &retry_count);
    if (copy_count == 0) {
        mutex_unlock(&manager->lock);
        return copy_count;
    }

    // Got new data, format and send
    ret = sensor_read_format_and_flush_new(manager, data, copy_count, cpy_length, &already_cpy_length);
    if (ret < 0) {
        mutex_unlock(&manager->lock);
        return ret;
    }
    mutex_unlock(&manager->lock);
    return ret;
}

static unsigned int sensor_poll(struct file *file, poll_table *wait)
{
    data_record_manager_t* manager = &g_data_record_manager;
    unsigned int mask = 0;
    uint64_t threshold = 0;

    poll_wait(file, &manager->read_wq, wait);

    threshold = ((int)MAX_SENSOR_DATA_RECORD_NUM_SHEM * 2) / 3;
    if (manager->shm_manager &&
       (manager->shm_manager->count >= threshold || !manager->kernel_enabled)) {
        pr_info("%s:data count >= threshold or kernel_enabled = 0\n", __func__);
        mask |= POLLIN | POLLRDNORM;
    }

    return mask;
}
static struct file_operations sensor_fops = {
    .owner = THIS_MODULE,
    .open = sensor_open,
    .read = sensor_read,
    .release = sensor_release,
    .poll = sensor_poll,
};

static int data_record_notify_scp(int value)
{
    int ret = 0;
    struct sensor_comm_notify notify;
    data_record_manager_t* manager = &g_data_record_manager;

    if (!manager->scp_inited) {
        pr_err("%s:scp not inited\n", __func__);
        return -1;
    }

    pr_info("%s:kernel_notify_scp value:%d\n", __func__, value);

    notify.sensor_type = SENSOR_TYPE_INVALID;
    notify.command = SENS_COMM_NOTIFY_DATA_RECORD_CMD;
    notify.sequence = 0;
    notify.value[0] = value;
    notify.length = sizeof(notify.value[0]);

    ret = sensor_comm_notify(&notify);
    if (ret < 0) {
        pr_err("%s:sensor_comm_notify failed\n", __func__);
        return -1;
    }
    return 0;
}

static ssize_t data_record_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    data_record_manager_t* manager = &g_data_record_manager;

    return sprintf(buf, "kernel_enabled->%d\n"
                        "scp_inited->%d\n"
                        "ap_base:  0x%llx\n"
                        "scp_base: 0x%llx\n"
                        "shm_size: 0x%llx\n"
                        "read_pos: %llu\n"
                        "write_pos:%llu\n"
                        "count:    %llu\n"
                        "sync_var: %llu\n",
                    manager->kernel_enabled,
                    manager->scp_inited,
                    manager->shm_manager->ap_base,
                    manager->shm_manager->scp_base,
                    manager->shm_manager->shm_size,
                    manager->shm_manager->read_pos,
                    manager->shm_manager->write_pos,
                    manager->shm_manager->count,
                    manager->shm_manager->sync_var);
}

static ssize_t data_record_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    data_record_manager_t* manager = &g_data_record_manager;
    int ret = 0;

    if (strncmp(buf, "1", 1) == 0) {
        pr_info("%s:echo 1\n", __func__);
        manager->kernel_enabled = 1;
        ret = data_record_notify_scp(AP_OPEN_SCP);
        if (ret < 0) {
            pr_err("%s:notify_scp fail\n", __func__);
            return count;
        } else {
            manager->scp_enabled = 1;
        }
    } else if (strncmp(buf, "0", 1) == 0) {
        pr_info("%s:echo 0\n", __func__);
        manager->kernel_enabled = 0;
        ret = data_record_notify_scp(AP_CLOSE_SCP);
        if (ret < 0) {
            pr_err("%s:notify_scp fail\n", __func__);
            return count;
        } else {
            manager->scp_enabled = 0;
        }
    } else {
        pr_err("%s:invalid value\n", __func__);
    }
    return count;
}

DEVICE_ATTR(data_record, 0660, data_record_show, data_record_store);

static struct attribute *data_record_attributes[] = {
    &dev_attr_data_record.attr,
    NULL
};

static struct attribute_group data_record_attribute_group = {
    .attrs = data_record_attributes
};

static int create_node(void)
{
    int ret = register_chrdev(0, DEVICE_NAME, &sensor_fops);
    data_record_manager_t* manager = &g_data_record_manager;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
    manager->sensor_class = class_create(DEVICE_NAME);
#else
    manager->sensor_class = class_create(THIS_MODULE, DEVICE_NAME);
#endif
    if (IS_ERR(manager->sensor_class)) {
        pr_err("%s:Failed to create class\n", __func__);
        return PTR_ERR(manager->sensor_class);
    }

    manager->sensor_device = device_create(manager->sensor_class, NULL, MKDEV(ret, 0), NULL, DEVICE_NAME);
    if (IS_ERR(manager->sensor_device)) {
        class_destroy(manager->sensor_class);
        pr_err("%s:Failed to create device\n", __func__);
        return PTR_ERR(manager->sensor_device);
    }

    ret = sysfs_create_group(&(manager->sensor_platform_device->dev.kobj),
            &data_record_attribute_group);
    if (ret < 0) {
        pr_err("%s:unable to create data_record_attribute_group file err=%d\n", __func__, ret);
        sysfs_remove_group(&(manager->sensor_platform_device->dev.kobj), &data_record_attribute_group);
        return -ENOMEM;
    }
    kobject_uevent(&(manager->sensor_platform_device->dev.kobj), KOBJ_ADD);
    return 0;
}
static void data_record_work_handler(struct work_struct *work)
{
    data_record_manager_t* manager = container_of(work, data_record_manager_t, work);
    int ret = 0;

    pr_info("%s:sync status with scp\n", __func__);
    msleep(50);
    ret = data_record_notify_scp(AP_OPEN_SCP);
    if (ret < 0) {
        pr_err("%s:sync status failed\n", __func__);
        manager->scp_enabled = 0;
    } else {
        manager->scp_enabled = 1;
    }
}

static int data_record_init_handler(int scp_enable_value)
{
    data_record_manager_t* manager = &g_data_record_manager;

    manager->scp_inited = 1;
    manager->scp_enabled = scp_enable_value;
    pr_info("%s:scp_inited =1,scp_enabled=:%d\n", __func__, scp_enable_value);
    if (!manager->scp_enabled && manager->kernel_enabled) {
        schedule_work(&manager->work);
    }
    return 0;
}

// The execution context of this function is in the interrupt,
// so it should be treated as an interrupt handling function
static void data_record_notify_func(struct sensor_comm_notify *n, void *private_data)
{
    struct sensor_comm_notify* notify = n;
    data_record_manager_t* manager = &g_data_record_manager;

    pr_info("%s:scp notify.value[0]:%x\n", __func__, notify->value[0]);
    switch (notify->value[0]) {
    case SCP_INIT_DONE:
        data_record_init_handler(notify->value[1]);
        break;
    case SCP_DATA_READY:
        pr_info("%s: SCP data ready, waking up readers\n", __func__);
        wake_up_interruptible(&manager->read_wq);
        break;
    default:
        pr_err("%s:invalid value\n", __func__);
        break;
    }
}

#define ROUNDUP(a, b) (((a) + ((b) - 1)) & ~((b) - 1))
static int data_record_probe(struct platform_device *pdev)
{
    uint64_t tmp_addr = 0;
    size_t dram_size = 0;
    data_record_manager_t* manager = &g_data_record_manager;

    pr_info("%s:executed!\n", __func__);
    memset(&g_data_record_manager, 0, sizeof(data_record_manager_t));
    INIT_WORK(&manager->work, data_record_work_handler);
    init_waitqueue_head(&manager->read_wq);
    mutex_init(&manager->lock);
    manager->sensor_platform_device = pdev;

    tmp_addr = (uint64_t)scp_get_reserve_mem_virt(SENS_DATA_RECORD_MEM_ID);
    dram_size = scp_get_reserve_mem_size(SENS_DATA_RECORD_MEM_ID);
    manager->shm_manager = (data_record_shem_t *)tmp_addr;
    manager->shm_manager->ap_base = tmp_addr;
    manager->shm_manager->read_pos = 0;
    manager->shm_manager->write_pos = 0;
    manager->shm_manager->count = 0;
    manager->shm_manager->sync_var = 0;
    manager->shm_manager->shm_size = (uint64_t)dram_size;
    manager->shm_ptr = (data_record_t *)ROUNDUP((tmp_addr + sizeof(data_record_shem_t)), 4);
    pr_info("%s:sizeof(data_record_shem_t):%d\n", __func__, (int)sizeof(data_record_shem_t));
    pr_info("%s:SENS_DATA_RECORD_MEM_ID ap_base:0x%llx,scp_base:0x%llx,shm_size:0x%llx,shm_ptr:0x%llx\n",
        __func__,
        manager->shm_manager->ap_base,
        manager->shm_manager->scp_base,
        manager->shm_manager->shm_size,
        (uint64_t)manager->shm_ptr);

    sensor_comm_notify_handler_register(SENS_COMM_NOTIFY_DATA_RECORD_CMD, data_record_notify_func, NULL);
    create_node();
    pr_info("%s:initialized\n", __func__);
    return 0;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0))
static void data_record_remove(struct platform_device *pdev)
#else
static int data_record_remove(struct platform_device *pdev)
#endif
{
    data_record_manager_t* manager = &g_data_record_manager;

    cancel_work_sync(&manager->work);
    device_destroy(manager->sensor_class, MKDEV(0, 0));
    class_destroy(manager->sensor_class);
    unregister_chrdev(0, DEVICE_NAME);
    sysfs_remove_group(&(manager->sensor_platform_device->dev.kobj), &data_record_attribute_group);
    sensor_comm_notify_handler_unregister(SENS_COMM_NOTIFY_DATA_RECORD_CMD);

    pr_info("%s:data_record_module exited\n", __func__);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0))
    return 0;
#endif
}

static const struct of_device_id of_drv_match[] = {
    {.compatible = "oplus,data_record"},
    {},
};
MODULE_DEVICE_TABLE(of, of_drv_match);

static struct platform_driver _driver = {
    .probe   = data_record_probe,
    .remove  = data_record_remove,
    .driver  = {
    .name    = "data_record",
    .of_match_table = of_drv_match,
    },
};

static int __init data_record_init(void)
{
    pr_info("%s:data_record_init call\n", __func__);

    platform_driver_register(&_driver);
    return 0;
}

static void __exit data_record_exit(void)
{
    pr_info("%s:data_record_exit call\n", __func__);

    platform_driver_unregister(&_driver);
}

module_init(data_record_init);
module_exit(data_record_exit);
MODULE_AUTHOR("Mediatek");
MODULE_DESCRIPTION("oplus sensor data record ");
MODULE_LICENSE("GPL");


