/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#if !defined(_HW_FENCE_TRACE_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _HW_FENCE_TRACE_H_
#include <linux/stringify.h>
#include <linux/types.h>
#include <linux/tracepoint.h>
#include <linux/version.h>
#undef TRACE_SYSTEM
#define TRACE_SYSTEM hw_fence
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE hw_fence_trace

#define HW_FENCE_STATE_TO_STRING(state) ((state) ? "awake" : "dormant")

TRACE_EVENT(hw_fence_update,
	TP_PROTO(const char *tag, u32 tag_id, u32 client_id, u32 hash,
		struct msm_hw_fence *hw_fence, char *arg_name, u64 arg),
	TP_ARGS(tag, tag_id, client_id, hash, hw_fence, arg_name, arg),
	TP_STRUCT__entry(
			__string(dump_tag, tag)
			__field(u32, tag_id)
			__field(u32, client_id)
			__field(u32, hash)
			__field(u32, valid)
			__field(u32, error)
			__field(u64, ctx)
			__field(u64, seq)
			__field(u64, wait_client_mask)
			__field(u32, alloc)
			__field(u64, flags)
			__array(u64, parents, MSM_HW_FENCE_MAX_JOIN_PARENTS)
			__field(u32, parents_cnt)
			__field(u32, pending_child_cnt)
			__field(u64, fence_create_time)
			__field(u64, fence_trigger_time)
			__field(u64, fence_wait_time)
			__field(u32, refcount)
			__field(u32, h_synx)
			__field(u64, client_data)
			__string(name, arg_name)
			__field(u64, arg)
	),
	TP_fast_assign(
#if (KERNEL_VERSION(6, 10, 0) <= LINUX_VERSION_CODE)
			__assign_str(dump_tag);
#else
			__assign_str(dump_tag, tag);
#endif
			__entry->tag_id = tag_id;
			__entry->client_id = client_id;
			__entry->hash = hash;
			if (hw_fence) {
				__entry->valid = hw_fence->valid;
				__entry->error = hw_fence->error;
				__entry->ctx = hw_fence->ctx_id;
				__entry->seq = hw_fence->seq_id;
				__entry->wait_client_mask = hw_fence->wait_client_mask;
				__entry->alloc = hw_fence->fence_allocator;
				__entry->flags = hw_fence->flags;
				if (hw_fence->parents_cnt > MSM_HW_FENCE_MAX_JOIN_PARENTS)
					__entry->parents_cnt = MSM_HW_FENCE_MAX_JOIN_PARENTS;
				else
					__entry->parents_cnt = hw_fence->parents_cnt;
				memcpy(__entry->parents, hw_fence->parent_list,
					sizeof(__entry->parents));
				__entry->pending_child_cnt = hw_fence->pending_child_cnt;
				__entry->fence_create_time = hw_fence->fence_create_time;
				__entry->fence_trigger_time = hw_fence->fence_trigger_time;
				__entry->fence_wait_time = hw_fence->fence_wait_time;
				__entry->refcount = hw_fence->refcount;
				__entry->h_synx = hw_fence->h_synx;
				__entry->client_data = hw_fence->client_data;
			}
#if (KERNEL_VERSION(6, 10, 0) <= LINUX_VERSION_CODE)
			__assign_str(name);
#else
			__assign_str(name, arg_name);
#endif
			__entry->arg = arg;
	),
	TP_printk("[%s:%d] client:%d %s:%llu hfence[%u] v:%d err:%u ctx:%llu seq:%llu wait:0x%llx alloc:%d f:0x%llx child_cnt:%d parents[%u]:%lld|%lld|%lld ct:%llu tt:%llu wt:%llu ref:0x%x h_synx:%u data:%llu",
		__get_str(dump_tag), __entry->tag_id, __entry->client_id, __get_str(name),
		__entry->arg, __entry->hash, __entry->valid, __entry->error, __entry->ctx,
		__entry->seq, __entry->wait_client_mask, __entry->alloc, __entry->flags,
		__entry->pending_child_cnt, __entry->parents_cnt,
		__entry->parents[0], __entry->parents[1], __entry->parents[2],
		__entry->fence_create_time, __entry->fence_trigger_time, __entry->fence_wait_time,
		__entry->refcount, __entry->h_synx, __entry->client_data)
)

TRACE_EVENT(hw_fence_update_queue,
	TP_PROTO(const char *tag, u32 tag_id, u32 client_id, char *queue_type,
		struct msm_hw_fence_queue *queue, u32 rd_idx, u32 wr_idx, u32 tx_wm),
	TP_ARGS(tag, tag_id, client_id, queue_type, queue, rd_idx, wr_idx, tx_wm),
	TP_STRUCT__entry(
			__string(dump_tag, tag)
			__field(u32, tag_id)
			__field(u32, client_id)
			__string(queue_type_name, queue_type)
			__field(u32, q_size_bytes)
			__field(u32, rd_idx)
			__field(u32, wr_idx)
			__field(u32, tx_wm)
			__field(u32, skip_wr_idx)
			__field(u32, rd_wr_idx_start)
			__field(u32, rd_wr_idx_factor)
	),
	TP_fast_assign(
#if (KERNEL_VERSION(6, 10, 0) <= LINUX_VERSION_CODE)
			__assign_str(dump_tag);
#else
			__assign_str(dump_tag, tag);
#endif
			__entry->tag_id = tag_id;
			__entry->client_id = client_id;
#if (KERNEL_VERSION(6, 10, 0) <= LINUX_VERSION_CODE)
			__assign_str(queue_type_name);
#else
			__assign_str(queue_type_name, queue_type);
#endif
			__entry->q_size_bytes = queue->q_size_bytes;
			__entry->rd_idx = rd_idx;
			__entry->wr_idx = wr_idx;
			__entry->tx_wm = tx_wm;
			__entry->skip_wr_idx = queue->skip_wr_idx;
			__entry->rd_wr_idx_start = queue->rd_wr_idx_start;
			__entry->rd_wr_idx_factor = queue->rd_wr_idx_factor;
	),
	TP_printk("[%s:%d] Client:%d %s q_sz_bytes:%u rd_idx:%u wr_idx:%u tx_wm:%u skips:%s start:%u factor:%u",
			__get_str(dump_tag), __entry->tag_id, __entry->client_id,
			__get_str(queue_type_name), __entry->q_size_bytes, __entry->rd_idx,
			__entry->wr_idx, __entry->tx_wm, __entry->skip_wr_idx ? "true" : "false",
			__entry->rd_wr_idx_start, __entry->rd_wr_idx_factor)
)

TRACE_EVENT(fctl_evtlog,
	TP_PROTO(u32 index, u32 cpu, u64 hw_time, s64 hw_diff_ns, u32 cnt, u32 *data),
	TP_ARGS(index, cpu, hw_time, hw_diff_ns, cnt, data),
	TP_STRUCT__entry(
			__field(u32, index)
			__field(u32, cpu)
			__field(u64, hw_time)
			__field(s64, hw_diff_ns)
			__field(u32, cnt)
			__array(u32, data, HW_FENCE_EVENT_MAX_DATA)
	),
	TP_fast_assign(
			__entry->index = index;
			__entry->cpu = cpu;
			__entry->hw_time = hw_time;
			__entry->hw_diff_ns = hw_diff_ns;
			if (cnt > HW_FENCE_EVENT_MAX_DATA)
				cnt = HW_FENCE_EVENT_MAX_DATA;
			__entry->cnt = cnt;
			memcpy(__entry->data, data, cnt * sizeof(u32));
			memset(&__entry->data[cnt], 0,
				(HW_FENCE_EVENT_MAX_DATA - cnt) * sizeof(u32));
	),
	TP_printk("[%d][cpu:%d][hw:0x%llx][diff_ns:%lld] data[%d]:|0x%x|0x%x|0x%x|0x%x|0x%x|0x%x|0x%x|0x%x|0x%x|0x%x|0x%x|0x%x",
			__entry->index, __entry->cpu, __entry->hw_time, __entry->hw_diff_ns,
			__entry->cnt, __entry->data[0], __entry->data[1], __entry->data[2],
			__entry->data[3], __entry->data[4], __entry->data[5], __entry->data[6],
			__entry->data[7], __entry->data[8], __entry->data[9], __entry->data[10],
			__entry->data[11])
)

TRACE_EVENT(hw_fence_signal_ipcc,
	TP_PROTO(u32 client_id, u64 ctx, u64 seq, u32 hash, u32 signal_from_import,
		u32 tx_client_pid, u32 rx_client_vid, u32 signal_id, u64 timestamp),
	TP_ARGS(client_id, ctx, seq, hash, signal_from_import,
		tx_client_pid, rx_client_vid, signal_id, timestamp),
	TP_STRUCT__entry(
			__field(u32, client_id)
			__field(u64, ctx)
			__field(u64, seq)
			__field(u32, hash)
			__field(u32, signal_from_import)
			__field(u32, tx_client_pid)
			__field(u32, rx_client_vid)
			__field(u32, signal_id)
			__field(u64, timestamp)
	),
	TP_fast_assign(
			__entry->client_id = client_id;
			__entry->ctx = ctx;
			__entry->seq = seq;
			__entry->hash = hash;
			__entry->signal_from_import = signal_from_import;
			__entry->tx_client_pid = tx_client_pid;
			__entry->rx_client_vid = rx_client_vid;
			__entry->signal_id = signal_id;
			__entry->timestamp = timestamp;
	),
	TP_printk("client:%d ctx:%llu seq:%llu hash:0x%x signal_from_import:%s tx_client_pid:%d rx_client_vid:%d signal_id:%d time:0x%llx",
			__entry->client_id, __entry->ctx, __entry->seq, __entry->hash,
			__entry->signal_from_import ? "true" : "false",
			__entry->tx_client_pid, __entry->rx_client_vid, __entry->signal_id,
			__entry->timestamp)
)

TRACE_EVENT(hw_fence_set_power_vote,
	TP_PROTO(const char *tag, u32 tag_id, u32 client_id, bool req_state, int ret,
		struct hw_fence_soccp *soccp_props),
	TP_ARGS(tag, tag_id, client_id, req_state, ret, soccp_props),
	TP_STRUCT__entry(
			__string(dump_tag, tag)
			__field(u32, tag_id)
			__field(u32, client_id)
			__field(bool, req_state)
			__field(int, ret)
			__field(bool, curr_state)
			__field(bool, curr_pending)
			__field(u32, usage_cnt)
	),
	TP_fast_assign(
#if (KERNEL_VERSION(6, 10, 0) <= LINUX_VERSION_CODE)
			__assign_str(dump_tag);
#else
			__assign_str(dump_tag, tag);
#endif
			__entry->tag_id = tag_id;
			__entry->client_id = client_id;
			__entry->req_state = req_state;
			__entry->ret = ret;
			if (soccp_props) {
				__entry->curr_state = soccp_props->is_awake;
				__entry->curr_pending = soccp_props->pending_state;
				__entry->usage_cnt = refcount_read(&soccp_props->usage_cnt);
			}

	),
	TP_printk("[%s:%d] Client:%d %s vote on soccp ret:%d curr_state:%s curr_pending:%s votes:0x%x",
		__get_str(dump_tag), __entry->tag_id, __entry->client_id,
		__entry->req_state ? "added" : "removed", __entry->ret,
		HW_FENCE_STATE_TO_STRING(__entry->curr_state),
		HW_FENCE_STATE_TO_STRING(__entry->curr_pending),
		__entry->usage_cnt)
)

#define HWFNC_DBG_TRACE_FENCE(client_id, hash, hfence, arg_name, arg) \
	trace_hw_fence_update(__func__, __LINE__, client_id, hash, hfence, arg_name, arg)

#define HWFNC_DBG_TRACE_QUEUE(drv_data, client_id) \
	hw_fence_dbg_trace_queues(drv_data, client_id, __func__, __LINE__)

#define HWFNC_DBG_TRACE_POWER_VOTE(client_id, req_state, ret, soccp_props) \
	trace_hw_fence_set_power_vote(__func__, __LINE__, client_id, req_state, ret, soccp_props)

#endif /* _HW_FENCE_TRACE_H_ */
/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#include <trace/define_trace.h>
