/** Copyright (C), 2018-2024, OPLUS Mobile Comm Corp., Ltd.
* Description: This file implements the sensor error trace support module.
*              It provides functionality to report sensor error information to the kernel
*              trace system via tracepoint named "sensor_err" for sensors-hal.
*              The trace event format matches oplusSensorErrToStatsd function.
* Create: 2025-11-19
*/

#define pr_fmt(fmt) "<sensor_err_trace>" fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/time.h>
#include "oplus_trace_sensor_err.h"
#define CREATE_TRACE_POINTS
#include "trace_sensor_err.h"

static uint16_t error_list[] = {
	PS_INIT_FAIL_ID,
	PS_I2C_ERR_ID,
	PS_ESD_REST_ID,
	PS_NO_INTERRUPT_ID,
	ALS_INIT_FAIL_ID,
	ALS_I2C_ERR_ID,
	ALS_ESD_REST_ID,
	ALS_NO_INTERRUPT_ID,
	ACCEL_INIT_FAIL_ID,
	ACCEL_I2C_ERR_ID,
	ACCEL_ESD_REST_ID,
	ACCEL_NO_INTERRUPT_ID,
	ACCEL_ORIGIN_DATA_TO_ZERO_ID,
	ACCEL_DATA_BLOCK_ID,
	ACCEL_DATA_FULL_RANGE_ID,
	ACCEL_SUB_DATA_BLOCK_ID,
	GYRO_INIT_FAIL_ID,
	GYRO_I2C_ERR_ID,
	GYRO_ESD_REST_ID,
	GYRO_NO_INTERRUPT_ID,
	GYRO_ORIGIN_DATA_TO_ZERO_ID,
	GYRO_DATA_BLOCK_ID,
	GYRO_SUB_DATA_BLOCK_ID,
	MAG_INIT_FAIL_ID,
	MAG_I2C_ERR_ID,
	MAG_ESD_REST_ID,
	MAG_NO_INTERRUPT_ID,
	MAG_ORIGIN_DATA_TO_ZERO_ID,
	MAG_DATA_BLOCK_ID,
	MAG_DATA_FULL_RANGE_ID,
	SAR_INIT_FAIL_ID,
	SAR_I2C_ERR_ID,
	SAR_ESD_REST_ID,
	SAR_NO_INTERRUPT_ID,
	SAR_ORIGIN_DATA_TO_ZERO_ID,
	BAROMETER_I2C_ERR_ID,
	HALL_I2C_ERR_ID,
};

static long get_timestamp_ms(void)
{
	struct timespec64 now;
	ktime_get_real_ts64(&now);
	return timespec64_to_ns(&now) / NSEC_PER_MSEC;
}

int oplus_trace_sensor_err_report(uint16_t event_id, char* fb_event_id, char* fb_field, const char* payload)
{
	int i = 0;
	pr_err("oplus_trace_sensor_err_report version\n");

	if (!fb_event_id || !fb_field) {
		return -1;
	}

	/* Check if event_id is in the allowed list */
	for (i = 0; i < sizeof(error_list)/sizeof(uint16_t); i++) {
		if (error_list[i] == event_id) {
			const char *event_id_str = fb_event_id ? fb_event_id : "unknown";
			const char *log_type_str = fb_field ? fb_field : "unknown";
			const char *payload_str = payload ? payload : "";
			pr_info("trace event_id =%d\n", event_id);
			trace_sensor_err("com.oplus.kernel", get_timestamp_ms(), SENSOR_ERR_APP_ID,
				SENSOR_ERR_LOG_TAG, event_id_str, log_type_str, payload_str);
			break;
		}
	}

	return 0;
}
EXPORT_SYMBOL(oplus_trace_sensor_err_report);

static int __init oplus_trace_sensor_err_init(void)
{
	pr_info("oplus_trace_sensor_err_init call\n");
	return 0;
}

static void __exit oplus_trace_sensor_err_exit(void)
{
	pr_info("oplus_trace_sensor_err_exit call\n");
}

module_init(oplus_trace_sensor_err_init);
module_exit(oplus_trace_sensor_err_exit);

MODULE_AUTHOR("Tao.Wei");
MODULE_DESCRIPTION("Oplus sensor error trace support");
MODULE_LICENSE("GPL");

