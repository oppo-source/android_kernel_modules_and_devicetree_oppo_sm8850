/** Copyright (C), 2018-2024, OPLUS Mobile Comm Corp., Ltd.
* Description: This file defines the trace event "sensor_err" for kernel tracepoint system.
*              The trace event format matches oplusSensorErrToStatsd function expectations.
*              This file is used by the kernel trace infrastructure to generate trace events.
* Create: 2025-11-19
*/

#if !defined(_TRACE_SENSOR_ERR_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_SENSOR_ERR_H

#include <linux/version.h>
#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM sensor

TRACE_EVENT(sensor_err,

	TP_PROTO(const char* reverse_domain_name, long time_s, int app_id, const char* log_tag, const char* event_id, const char* log_type, const char* payload),

	TP_ARGS(reverse_domain_name, time_s, app_id, log_tag, event_id, log_type, payload),

	TP_STRUCT__entry(
		__string(reverse_domain_name, reverse_domain_name)
		__field(long, time_s)
		__field(int, app_id)
		__string(log_tag, log_tag)
		__string(event_id, event_id)
		__string(log_type, log_type)
		__string(payload, payload)),

	TP_fast_assign(
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
		__assign_str(reverse_domain_name);
#else
		__assign_str(reverse_domain_name, reverse_domain_name);
#endif
		__entry->time_s = time_s;
		__entry->app_id = app_id;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
		__assign_str(log_tag);
		__assign_str(event_id);
		__assign_str(log_type);
		__assign_str(payload);
#else
		__assign_str(log_tag, log_tag);
		__assign_str(event_id, event_id);
		__assign_str(log_type, log_type);
		__assign_str(payload, payload);
#endif
),

	TP_printk("reverse_domain_name:%s time_s:%ld app_id:%d log_tag:%s event_id:%s log_type:%s payload:%s",
			__get_str(reverse_domain_name), __entry->time_s, __entry->app_id, __get_str(log_tag),
			__get_str(event_id), __get_str(log_type), __get_str(payload))
);

#undef TRACE_INCLUDE_PATH
#if defined(CFG_OPLUS_ARCH_IS_QCOM)
#define TRACE_INCLUDE_PATH ../../../vendor/oplus/sensor/kernel/oplus_sensor_err
#elif defined(CFG_OPLUS_ARCH_IS_MTK)
#define TRACE_INCLUDE_PATH ../../../vendor/oplus/sensor/kernel/oplus_sensor_err
#endif
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace_sensor_err

/* This part must be outside protection */
#include <trace/define_trace.h>

#endif

