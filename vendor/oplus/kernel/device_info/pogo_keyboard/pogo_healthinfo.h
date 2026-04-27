/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef _POGO_KEYBOARD_HEALTHINFO_
#define _POGO_KEYBOARD_HEALTHINFO_

#include <linux/i2c.h>
#include <linux/firmware.h>
#include <linux/list.h>
#include <linux/types.h>

#define DEFAULT_CHILD_STR_LEN       8
#define DEFAULT_REPORT_STR_LEN      100
#define DEFAULT_BUF_MATRIX_LINEBREAK    8
#define PREFIX_HEALTH_REPORT        "health_report-"
#define MAX_HEALTH_REPORT_LEN       50
#define MAX_HEALTH_INFO_SIZE 4096  /* 4KB */

/* Health report macros for exception reporting */
#define POGO_HEALTH_REPORT_ALLOC_FAIL                "alloc_fail"
#define POGO_HEALTH_REPORT_INIT_FAIL                 "init_fail"
#define POGO_HEALTH_REPORT_KB_INIT_FAIL              "kb_init_fail"
#define POGO_HEALTH_REPORT_TP_INIT_FAIL              "tp_init_fail"
#define POGO_HEALTH_REPORT_READ_KBVER_FAIL           "read_kbver_fail"
#define POGO_HEALTH_REPORT_SET_TP_STATUS_FAIL        "set_tp_status_fail"
#define POGO_HEALTH_REPORT_GET_CHARGEI_FAIL          "get_chr_current_fail"
#define POGO_HEALTH_REPORT_SET_TP_DEBUG_FAIL         "set_tp_debug_fail"
#define POGO_HEALTH_REPORT_SET_LCD_STATUS_FAIL       "set_lcd_status_fail"
#define POGO_HEALTH_REPORT_SET_LEVEL_FAIL            "set_level_fail"
#define POGO_HEALTH_REPORT_UPLOAD_SN_FAIL            "upload_sn_fail"
#define POGO_HEALTH_REPORT_GET_SN_FAIL               "get_sn_fail"
#define POGO_HEALTH_REPORT_UEVENT_KOBJ_FAIL          "uevent_kobj_fail"
#define POGO_HEALTH_REPORT_KB_SUSPEND_FAIL           "kb_suspend_fail"
#define POGO_HEALTH_REPORT_KFIFO_OUT_FAIL            "kfifo_out_fail"
#define POGO_HEALTH_REPORT_FW_GET_FAIL               "fw_get_fail"
#define POGO_HEALTH_REPORT_FW_UPDATE_FAIL            "fw_update_fail"
#define POGO_HEALTH_REPORT_ENTER_OTA_FAIL            "enter_ota_fail"
#define POGO_HEALTH_REPORT_OTA_FAIL                  "ota_fail"
#define POGO_HEALTH_REPORT_READ_FAIL                 "read_fail"
#define POGO_HEALTH_REPORT_WRITE_FAIL                "write_fail"
#define POGO_HEALTH_REPORT_WRRD_FAIL                 "write_read_fail"
#define POGO_HEALTH_REPORT_COMM_FAIL                 "comm_fail"
#define POGO_HEALTH_REPORT_READ_TIMEOUT_FAIL         "read_timeout"
#define POGO_HEALTH_REPORT_WRITE_TIMEOUT_FAIL        "write_timeout"
#define POGO_HEALTH_REPORT_HARDWARE_ERROR            "hardware_error"
#define POGO_HEALTH_REPORT_UNKNOWN_ERROR             "unknown_error"

extern struct pogo_keyboard_data *pogo_keyboard_client;

struct health_value_count {
    struct list_head head;
    void *value;
    int count;
};

struct monitor_data {
    struct list_head	health_report_list;
};

int pogo_healthinfo_report(struct monitor_data *monitor_data, char *report);

/* Helper macro to report health info if pogo_keyboard_client is available */
#define POGO_HEALTH_REPORT(report_macro) \
    do { \
        struct pogo_keyboard_data *__client = pogo_keyboard_client; \
        if (__client && !in_interrupt() && !in_atomic()) { \
            mutex_lock(&__client->monitor_mutex); \
            pogo_healthinfo_report(&__client->monitor_data, report_macro); \
            mutex_unlock(&__client->monitor_mutex); \
        } \
    } while (0)

int pogo_healthinfo_read(char __user *buf, size_t size, struct monitor_data *monitor_data);

int pogo_healthinfo_read_seq(struct seq_file *s, struct monitor_data *monitor_data);

int pogo_healthinfo_clear(struct monitor_data *monitor_data);

int pogo_healthinfo_init(struct monitor_data *monitor_data);

#endif /* _POGO_KEYBOARD_HEALTHINFO_ */
