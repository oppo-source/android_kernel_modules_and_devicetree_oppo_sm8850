// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#include <linux/habmm.h>
#include <linux/kthread.h>

#include "hw_fence_drv_priv.h"
#include "hw_fence_drv_virtio.h"
#include "hw_fence_drv_utils.h"
#include "hw_fence_drv_debug.h"

#define HW_FENCE_HAB_MAJOR_MMID MM_DISP_5
#define HW_FENCE_HAB_REQUEST_POWER_MMID HW_FENCE_HAB_MAJOR_MMID
#define HW_FENCE_HAB_SSR_NOTIFY_MMID HAB_MMID_CREATE(HW_FENCE_HAB_MAJOR_MMID, 0x1)
#define HW_FENCE_HAB_SOCKET_OPEN_TIMEOUT_MS -1 /* block indefinitely */
#define HW_FENCE_HAB_REQUEST_TIMEOUT_MS 1000

int hw_fence_virtio_init(struct hw_fence_driver_data *drv_data)
{
	int ret, tmp_ret;

	if (IS_ERR_OR_NULL(drv_data) || !drv_data->drv_id) {
		HWFNC_ERR("invalid input drv_data:0x%pK id:%d\n", drv_data,
			drv_data ? drv_data->drv_id : -1);
		return -EINVAL;
	}

	mutex_init(&drv_data->virtio_lock);
	ret = habmm_socket_open(&drv_data->send_socket, HW_FENCE_HAB_REQUEST_POWER_MMID,
		HW_FENCE_HAB_SOCKET_OPEN_TIMEOUT_MS, 0);
	if (ret) {
		HWFNC_ERR("failed to open hab socket for sending messages to pvm mmid:%d ret:%d\n",
			HW_FENCE_HAB_REQUEST_POWER_MMID, ret);
		return ret;
	}

	ret = habmm_socket_open(&drv_data->recv_socket, HW_FENCE_HAB_SSR_NOTIFY_MMID,
		HW_FENCE_HAB_SOCKET_OPEN_TIMEOUT_MS, 0);
	if (ret) {
		HWFNC_ERR("failed to open hab socket for receiving msg from pvm mmid:%d ret:%d\n",
			HW_FENCE_HAB_SSR_NOTIFY_MMID, ret);
		tmp_ret = habmm_socket_close(drv_data->recv_socket);
		if (tmp_ret)
			HWFNC_ERR("failed to close handle:%d for failure during bootup ret:%d\n",
				drv_data->recv_socket, ret);
		return ret;
	}

	HWFNC_DBG_INIT("successfully opened hab send_socket:%d mmid:%d recv_socket:%d mmid:%d\n",
		drv_data->send_socket, HW_FENCE_HAB_REQUEST_POWER_MMID, drv_data->recv_socket,
		HW_FENCE_HAB_SSR_NOTIFY_MMID);

	return 0;
}

int hw_fence_virtio_uninit(struct hw_fence_driver_data *drv_data)
{
	int ret = 0;

	if (IS_ERR_OR_NULL(drv_data)) {
		HWFNC_ERR("invalid input drv_data:0x%pK\n", drv_data);
		return -EINVAL;
	}

	ret = habmm_socket_close(drv_data->send_socket);
	if (ret)
		HWFNC_ERR("failed to close handle:%d to send msg to pvm\n", drv_data->send_socket);

	ret = habmm_socket_close(drv_data->recv_socket);
	if (ret)
		HWFNC_ERR("failed to close handle:%d to recv msg from pvm\n",
			drv_data->recv_socket);

	return ret;
}

static int _hw_fence_virtio_send(struct hw_fence_driver_data *drv_data,
	struct msm_hw_fence_queue_payload_base *to_send)
{
	struct msm_hw_fence_queue_payload_base *cmd_send, *cmd_recv;
	u32 size = sizeof(struct msm_hw_fence_queue_payload_base);
	u64 timestamp;
	int ret;

	if (IS_ERR_OR_NULL(drv_data)) {
		HWFNC_ERR("invalid input drv_data:0x%pK\n", drv_data);
		return -EINVAL;
	}

	cmd_send = kzalloc(sizeof(struct msm_hw_fence_queue_payload_base), GFP_KERNEL);
	if (!cmd_send)
		return -ENOMEM;
	cmd_recv = kzalloc(sizeof(struct msm_hw_fence_queue_payload_base), GFP_KERNEL);
	if (!cmd_recv) {
		kfree(cmd_send);
		return -ENOMEM;
	}

	memcpy(cmd_send, to_send, sizeof(struct msm_hw_fence_queue_payload_base));

	HWFNC_DBG_L("request type:%u version:0x%x sz:%u drv_id:%d\n",
		cmd_send->type, cmd_send->version, cmd_send->size, drv_data->drv_id);

	mutex_lock(&drv_data->virtio_lock);
	ret = habmm_socket_send(drv_data->send_socket, cmd_send, sizeof(*cmd_send), 0);
	if (ret) {
		HWFNC_ERR("Failed to send msg type:%d ret:%d\n", cmd_send->type, ret);
		mutex_unlock(&drv_data->virtio_lock);
		goto end;
	}

	HWFNC_DBG_L("successfully sent data ret:%d waiting:%dus for return msg\n", ret,
		HW_FENCE_HAB_REQUEST_TIMEOUT_MS);

	ret = habmm_socket_recv(drv_data->send_socket, cmd_recv, &size,
		HW_FENCE_HAB_REQUEST_TIMEOUT_MS, 0);
	mutex_unlock(&drv_data->virtio_lock);

	if (ret || size < sizeof(*cmd_recv) || cmd_recv->type != cmd_send->type) {
		HWFNC_ERR("Invalid handle:%d ret:%d size:%u type:%u expected:%u return_status:%d\n",
			drv_data->send_socket, ret, size, cmd_recv->type, cmd_send->type,
			(size == sizeof(*cmd_recv)) ? cmd_recv->response : -1);
		ret = -EINVAL;
		goto end;
	}

	if (cmd_recv->response != 0)
		HWFNC_ERR("Host failed to process message type:%d ret:%d\n", cmd_send->type,
			cmd_recv->response);
	ret = cmd_recv->response;
	timestamp = (u64)cmd_recv->timestamp_hi << 32 | (u64)cmd_recv->timestamp_lo;

	HWFNC_DBG_L("recv handle:%d size:%u type:%u version:0x%x response:%d timestamp:0x%llx\n",
		drv_data->send_socket, cmd_recv->size, cmd_recv->type, cmd_recv->version,
		cmd_recv->response, timestamp);

end:
	kfree(cmd_send);
	kfree(cmd_recv);
	return ret;
}

int hw_fence_virtio_request_power(struct hw_fence_driver_data *drv_data,
	enum hw_fence_client_id client_id, bool state)
{
	struct msm_hw_fence_queue_payload_enable_power cmd_send;

	hw_fence_utils_update_power_payload(drv_data, &cmd_send, client_id, state);

	return _hw_fence_virtio_send(drv_data, (struct msm_hw_fence_queue_payload_base *)&cmd_send);
}

int hw_fence_virtio_init_client(struct hw_fence_driver_data *drv_data,
	enum hw_fence_client_id client_id)
{
	struct msm_hw_fence_queue_payload_init_client cmd_send;
	u64 timestamp;

	cmd_send.type = HW_FENCE_PAYLOAD_TYPE_35;
	cmd_send.version = HW_FENCE_PAYLOAD_REV(1, 0);
	cmd_send.size = sizeof(cmd_send);
	cmd_send.vm_id = drv_data->drv_id;
	cmd_send.client_id_ext = client_id;
	timestamp = hw_fence_get_qtime(drv_data);
	cmd_send.timestamp_lo = (u32)timestamp;
	cmd_send.timestamp_hi = timestamp >> 32;

	HWFNC_DBG_L("request type:%u version:0x%x sz:%u drv_id:%d client:%d ts:0x%llx\n",
		cmd_send.type, cmd_send.version, cmd_send.size, cmd_send.vm_id,
		cmd_send.client_id_ext, timestamp);

	return _hw_fence_virtio_send(drv_data, (struct msm_hw_fence_queue_payload_base *)&cmd_send);
}
