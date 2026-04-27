// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/module.h>
#include <linux/version.h>

#include "hfi_core.h"
#include "hfi_core_debug.h"
#include "hfi_core_ssr.h"
#include "hfi_queue_controller.h"
#include "hfi_core_firmware.h"
#include "hfi_core_irq.h"
#include "hfi_if_abstraction.h"
#include "hfi_swi.h"
#include "hfi_smmu.h"

/**
 * hfi_core_ssr_handler - Handle the subsystem restart of DCP/DPU core
 *
 * This function handles the subsystem restart (SSR) event for the
 * HFI (Hardware Function Interface) core. It performs the necessary
 * actions to properly manage the restart process, ensuring that the
 * DCP/DPU core can recover and resume normal operation after a subsystem
 * failure or reset.
 *
 * @drv_data: Pointer to the HFI core driver data structure
 *
 * Return: 0 on success, negative error code on failure.
 */

static int hfi_core_stop_fw_comm(struct hfi_core_drv_data *drv_data)
{
	int ret = 0;

	HFI_CORE_DBG_H("+\n");

	ret = deinit_resources(drv_data);
	if (ret)
		HFI_CORE_ERR("failed to deinit resources ret :%d\n", ret);

	ret = deinit_swi(drv_data);
	if (ret)
		HFI_CORE_ERR("failed to deinit swi ret :%d\n", ret);

	ret = deinit_smmu(drv_data);
	if (ret)
		HFI_CORE_ERR("failed to deinit smmu ret :%d\n", ret);

	ret = hfi_core_firmware_unload(drv_data);
	if (ret) {
		HFI_CORE_ERR("failed to unload firmware, ret: %d\n", ret);
		return ret;
	}

	ret = hfi_core_firmware_core_dump(drv_data);
	if (ret)
		HFI_CORE_ERR("failed to do firmware core dump, ret: %d\n", ret);

	HFI_CORE_DBG_H("-\n");
	return ret;
}

static int hfi_core_restart_fw_comm(struct hfi_core_drv_data *drv_data)
{
	int ret = 0;

	HFI_CORE_DBG_H("+\n");

	ret = hfi_core_firmware_load(drv_data);
	if (ret) {
		HFI_CORE_ERR("failed to load firmware, ret: %d\n", ret);
		return ret;
	}
	ret = hfi_core_irq_wait(drv_data, HFI_IRQ_SIGNAL_DCP_CLK_READY_BIT);
	if (ret) {
		HFI_CORE_ERR("failed to wait for DCP ready irq, ret: %d\n", ret);
		return ret;
	}

	ret = hfi_core_irq_wait(drv_data, HFI_IRQ_SIGNAL_ERR_SERVICE_READY_BIT);
	if (ret) {
		HFI_CORE_ERR("failed to wait for ERR service ready irq, ret: %d\n", ret);
		return ret;
	}

	ret = init_smmu(drv_data);
	if (ret) {
		HFI_CORE_ERR("failed to init smmu ret :%d\n", ret);
		return ret;
	}

	ret = init_swi(drv_data);
	if (ret) {
		HFI_CORE_ERR("failed to init swi ret :%d\n", ret);
		return ret;
	}

	ret = init_resources(drv_data);
	if (ret) {
		HFI_CORE_ERR("failed to init queues ret :%d\n", ret);
		return ret;
	}

	HFI_CORE_DBG_H("-\n");
	return ret;
}

static void hfi_core_ssr_handler(struct kthread_work *work)
{
	int ret = 0;
	struct hfi_core_drv_data *drv_data;
	struct client_data *client_data;

	HFI_CORE_DBG_H("+\n");

	if (!work) {
		HFI_CORE_ERR("work ptr is null\n");
		return;
	}

	drv_data = container_of(work, struct hfi_core_drv_data, ssr_info.ssr_work);

	/* send HFI_CORE_EVENT_SSR_START event to all clients */
	for (int id = HFI_CORE_CLIENT_ID_0; id < HFI_CORE_CLIENT_ID_MAX; id++) {
		client_data = &drv_data->client_data[id];
		if (client_data->session && client_data->cb_fn) {
			client_data->cb_fn(client_data->session, client_data->cb_data,
				HFI_CORE_EVENT_SSR_START, true);
		}
	}

	ret = hfi_core_stop_fw_comm(drv_data);
	if (ret) {
		HFI_CORE_ERR("stopping fw communication failed, ret: %d\n", ret);
		return;
	}

	ret = hfi_core_restart_fw_comm(drv_data);
	if (ret) {
		HFI_CORE_ERR("restarting fw communication failed, ret: %d\n", ret);
		return;
	}

	spin_lock(&drv_data->ssr_info.spin_lock);
	drv_data->ssr_info.ssr_in_progress = false;
	spin_unlock(&drv_data->ssr_info.spin_lock);

	/* send HFI_CORE_EVENT_SSR_END event to all clients */
	for (int id = HFI_CORE_CLIENT_ID_0; id < HFI_CORE_CLIENT_ID_MAX; id++) {
		client_data = &drv_data->client_data[id];
		if (client_data->session && client_data->cb_fn) {
			client_data->cb_fn(client_data->session, client_data->cb_data,
				HFI_CORE_EVENT_SSR_END, false);
		}
	}

	HFI_CORE_DBG_H("-\n");
}

/**
 * hfi_core_ssr_init() - Initialize SSR info for hfi core driver.
 *
 * This function initializes the subsystem restart related information for the
 * HFI core. It sets up the necessary handlers and configurations to ensure
 * the HFI core can handle failure due to subsystem restart
 *
 * @drv_data: Pointer to the HFI core driver data structure
 *
 * Return: 0 on success, negative error code on failure.
 */
int hfi_core_ssr_init(struct hfi_core_drv_data *drv_data)
{
	int ret = 0;
	struct hfi_core_ssr_info *ssr_info;

	HFI_CORE_DBG_H("+\n");

	if (!drv_data) {
		HFI_CORE_ERR("null driver data\n");
		return -EINVAL;
	}

	ssr_info = &drv_data->ssr_info;
	spin_lock_init(&ssr_info->spin_lock);

	spin_lock(&ssr_info->spin_lock);
	ssr_info->ssr_in_progress = false;
	spin_unlock(&ssr_info->spin_lock);

	kthread_init_work(&ssr_info->ssr_work, hfi_core_ssr_handler);
	kthread_init_worker(&ssr_info->ssr_worker);
	ssr_info->ssr_worker_thread = kthread_run(kthread_worker_fn, &ssr_info->ssr_worker,
			"ssr_worker_thread");

	HFI_CORE_DBG_H("-\n");
	return ret;
}

/**
 * hfi_core_ssr_deinit - Deinitialize the subsystem restart for the HFI core
 *
 * This function deinitializes the subsystem restart (SSR) mechanism for the
 * HFI core. It disables the restart handlers and cleans up any resources
 * allocated during the initialization process.
 *
 * @drv_data: Pointer to the HFI core driver data structure
 *
 * Return: 0 on success, negative error code on failure.
 */
int hfi_core_ssr_deinit(struct hfi_core_drv_data *drv_data)
{
	struct hfi_core_ssr_info *ssr_info;

	HFI_CORE_DBG_H("+\n");

	if (!drv_data) {
		HFI_CORE_ERR("null driver data\n");
		return -EINVAL;
	}
	ssr_info = &drv_data->ssr_info;

	if (ssr_info->ssr_worker_thread) {
		// Ensure any pending work is completed or canceled
		kthread_flush_worker(&ssr_info->ssr_worker);
		kthread_stop(ssr_info->ssr_worker_thread);
		ssr_info->ssr_worker_thread = NULL;
	}

	spin_lock(&ssr_info->spin_lock);
	ssr_info->ssr_in_progress = false;
	spin_unlock(&ssr_info->spin_lock);

	HFI_CORE_DBG_H("-\n");
	return 0;
}
