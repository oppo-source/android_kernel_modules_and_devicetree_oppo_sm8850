/***********************************************************
** SPDX-License-Identifier: GPL-2.0-only
** Copyright (C), 2025-2025 Oplus. All rights reserved.
** File: wireless_pen_trace.h
** Description: wireless pen trace
** Date: 2025-11-12
** -----------Revision History: -------------------------------
** <author>        <data>    <version >       <desc>
****************************************************************/

#undef TRACE_SYSTEM
#define TRACE_SYSTEM wls_pen_chg_stat

#if !defined(_WIRELESS_PEN_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _WIRELESS_PEN_TRACE_H

#include <linux/tracepoint.h>

TRACE_EVENT(wls_pen_chg_stat,
	TP_PROTO(long timestamp_ms, int pen_id, long start_time, long end_time, int start_soc, int end_soc, const char *err_reason),
	TP_ARGS(timestamp_ms, pen_id, start_time, end_time, start_soc, end_soc, err_reason),
	TP_STRUCT__entry(
		__field(long,	timestamp_ms)
		__field(int, 	pen_id)
		__field(long, 	start_time)
		__field(long, 	end_time)
		__field(int, 	start_soc)
		__field(int, 	end_soc)
		__dynamic_array(char, err_reason, strlen(err_reason) + 1)),

	TP_fast_assign(
		__entry->timestamp_ms	= timestamp_ms;
		__entry->pen_id			= pen_id;
		__entry->start_time		= start_time;
		__entry->end_time		= end_time;
		__entry->start_soc		= start_soc;
		__entry->end_soc		= end_soc;
		strncpy(__get_dynamic_array(err_reason), err_reason, strlen(err_reason) + 1)),
		TP_printk("timestamp_ms:%ld pen_id:%d start_time:%ld end_time:%ld start_soc:%d end_soc:%d err_reason:%s",
		__entry->timestamp_ms, __entry->pen_id, __entry->start_time, __entry->end_time, __entry->start_soc, __entry->end_soc, __get_str(err_reason))

);
#endif /* _WIRELESS_PEN_TRACE_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../../../vendor/oplus/kernel/charger/bazel/wireless_pen
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE wireless_pen_trace

/* This part must be outside protection */
#include <trace/define_trace.h>
