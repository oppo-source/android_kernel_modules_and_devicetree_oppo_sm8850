/******************************************************************
** Copyright (C), 2004-2021 OPLUS Mobile Comm Corp., Ltd.
** data_record
** File: - data_record.h
** Description: Source file for data_record sensorhub driver.
** Version: 1.0
**
** --------------------------- Revision History: ---------------------
* <version> <date>      <author>                <desc>
*******************************************************************/
#ifndef __DATA_RECORD_H__
#define __DATA_RECORD_H__

#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/kobject.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/param.h>
#include <linux/proc_fs.h>
#include <linux/time.h>
#include <linux/miscdevice.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/io.h>

#define DEVICE_NAME "data_record"
#define DATA_RECORD_GAIN 1000000
#define BUFFER_COUNT 200
#define SINGLE_STR_MAX_SIZE 50
#define MAX_SENSOR_DATA_RECORD_NUM_SHEM  2400 //2400 60k

#define SCP_INIT_DONE    1
#define SCP_DATA_READY   2

#define AP_CLOSE_SCP     0
#define AP_OPEN_SCP      1

#define MAX_SEGMENT_SIZE 4000
#define MAX_TOTAL_SIZE (BUFFER_COUNT * SINGLE_STR_MAX_SIZE)  // 200 * 50 = 10000
#define MAX_SEGMENTS ((MAX_TOTAL_SIZE + MAX_SEGMENT_SIZE - 1) / MAX_SEGMENT_SIZE)  // ceil(10000/4000) = 3

typedef struct {
    uint64_t timestamp;
    int32_t sensor_type;
    int32_t x;
    int32_t y;
    int32_t z;
} data_record_t;

// Shared memory structure, members should be placed at the beginning of shared memory
typedef struct {
    uint64_t ap_base;
    uint64_t scp_base;
    uint64_t shm_size;
    uint64_t read_pos;
    uint64_t write_pos;
    uint64_t count;
    volatile uint64_t sync_var;
} data_record_shem_t;

typedef struct {
    data_record_shem_t *shm_manager;
    data_record_t *shm_ptr;
    int kernel_enabled;
    int scp_inited;
    volatile int scp_enabled;
    struct work_struct work;
    struct mutex lock;
    wait_queue_head_t read_wq;
    struct class *sensor_class;
    struct device *sensor_device;
    struct platform_device *sensor_platform_device;
} data_record_manager_t;

#endif /*__DATA_RECORD_H__*/

