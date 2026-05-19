// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/module.h>
#include <linux/version.h>
#include <linux/irqreturn.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>

#include "hfi_core.h"
#include "hfi_core_debug.h"


#define INVALID_IRQ                                                          -1

static irqreturn_t hfi_core_dcp_fatal_irq_handler(int irq, void *data)
{
	struct hfi_core_drv_data *drv_data;
	bool already_in_ssr = false;

	HFI_CORE_DBG_H("+\n");
	HFI_CORE_DBG_L("irq: %d\n", irq);

	if (!data)
		return IRQ_HANDLED;

	drv_data = (struct hfi_core_drv_data *)data;

	disable_irq_wake(irq);

	if (!atomic_read(&drv_data->disable_ssr_handling)) {
		spin_lock(&drv_data->ssr_info.spin_lock);
		if (drv_data->ssr_info.ssr_in_progress)
			already_in_ssr = true;
		else
			drv_data->ssr_info.ssr_in_progress = true;
		spin_unlock(&drv_data->ssr_info.spin_lock);
		if (already_in_ssr)
			return IRQ_HANDLED;

		kthread_queue_work(&drv_data->ssr_info.ssr_worker, &drv_data->ssr_info.ssr_work);
	}

	HFI_CORE_DBG_H("Queuing ssr work\n");
	atomic_or(BIT(HFI_IRQ_SIGNAL_SSR_BIT), &drv_data->irq_info.irq_wait_signal);
	wake_up_interruptible(&drv_data->irq_info.irq_wait_queue);

	HFI_CORE_DBG_H("-\n");
	return IRQ_HANDLED;
}

static irqreturn_t hfi_core_dcp_smp2p_irq_handler(int irq, void *data)
{
	struct hfi_core_drv_data *drv_data;
	struct hfi_core_irq_info *irq_info;

	HFI_CORE_DBG_H("+\n");
	HFI_CORE_DBG_H("irq: %d\n", irq);

	if (!data) {
		HFI_CORE_ERR("invalid data\n");
		return IRQ_HANDLED;
	}

	drv_data = (struct hfi_core_drv_data *)data;
	irq_info = &drv_data->irq_info;

	if (irq == irq_info->smp2p_err_service_ready_irq) {
		atomic_or(BIT(HFI_IRQ_SIGNAL_ERR_SERVICE_READY_BIT),
			&drv_data->irq_info.irq_wait_signal);
	} else if (irq == irq_info->smp2p_dcp_clock_ready_irq) {
		atomic_or(BIT(HFI_IRQ_SIGNAL_DCP_CLK_READY_BIT),
			&drv_data->irq_info.irq_wait_signal);
	} else if (irq == irq_info->smp2p_dcp_pong_irq) {
		atomic_or(BIT(HFI_IRQ_SIGNAL_PONG_BIT),
			&drv_data->irq_info.irq_wait_signal);
	} else if (irq == irq_info->smp2p_dcp_stop_ack_irq) {
		atomic_or(BIT(HFI_IRQ_SIGNAL_STOP_ACK_BIT),
			&drv_data->irq_info.irq_wait_signal);
	} else {
		HFI_CORE_ERR("Unknown irq: %d\n", irq);
		return IRQ_NONE;
	}
	wake_up_interruptible(&drv_data->irq_info.irq_wait_queue);

	HFI_CORE_DBG_H("-\n");
	return IRQ_HANDLED;
}

int hfi_core_irq_wait(struct hfi_core_drv_data *drv_data, enum hfi_core_irq_signal irq_signal)
{
	int ret = 0;
	struct hfi_core_irq_info *irq_info;

	HFI_CORE_DBG_H("+\n");

	if (!drv_data) {
		HFI_CORE_ERR("invalid params\n");
		return -EINVAL;
	}
	irq_info = &drv_data->irq_info;

	HFI_CORE_DBG_INFO("wait for irq signal %d", irq_signal);

	ret = wait_event_interruptible_timeout(irq_info->irq_wait_queue,
		(atomic_read(&irq_info->irq_wait_signal) & BIT(irq_signal)),
		msecs_to_jiffies(5000));
	if (ret == 0) {
		HFI_CORE_ERR("wait for irq timed out\n");
		return -ETIMEDOUT;
	} else if (ret == -ERESTARTSYS) {
		HFI_CORE_ERR("wait for irq interrupted by signal\n");
		return -ERESTARTSYS;
	}
	atomic_andnot(BIT(irq_signal), &irq_info->irq_wait_signal);
	HFI_CORE_DBG_H("-\n");
	return 0;
}

int hfi_core_ssr_irq_init(struct hfi_core_drv_data *drv_data)
{
	int ret = 0;
	struct platform_device *pdev;
	struct hfi_core_irq_info *irq_info;

	HFI_CORE_DBG_H("-\n");

	if (!drv_data) {
		HFI_CORE_ERR("null driver data\n");
		return -EINVAL;
	}

	pdev = to_platform_device(drv_data->dev);
	init_waitqueue_head(&drv_data->irq_info.irq_wait_queue);
	irq_info = &drv_data->irq_info;

	/* DCP Fatal ERR IRQ setup */
	irq_info->dcp_wdog_bus_error_irq = platform_get_irq_byname(pdev, "dcp_wdog_bus_error_irq");
	if (irq_info->dcp_wdog_bus_error_irq < 0) {
		HFI_CORE_DBG_INFO("failed to setup dcp_wdog_bus_error_irq\n");
		return irq_info->dcp_wdog_bus_error_irq;
	}
	HFI_CORE_DBG_H("dcp fatal irq: %d\n", irq_info->dcp_wdog_bus_error_irq);

	ret = devm_request_irq(drv_data->dev, irq_info->dcp_wdog_bus_error_irq,
		hfi_core_dcp_fatal_irq_handler, 0, "dcp_wdog_bus_error_irq", drv_data);
	if (ret) {
		HFI_CORE_ERR("failed to acquire dcp fatal irq, ret: %d\n", ret);
		return ret;
	}

	/* SMP2P Fatal IRQ setup */
	irq_info->smp2p_fatal_irq = platform_get_irq_byname(pdev, "smp2p_fatal_irq");
	if (irq_info->smp2p_fatal_irq < 0) {
		HFI_CORE_ERR("failed to setup smp2p_fatal_irq\n");
		return irq_info->smp2p_fatal_irq;
	}
	HFI_CORE_DBG_H("smp2p fatal irq: %d\n", irq_info->smp2p_fatal_irq);

	ret = devm_request_threaded_irq(drv_data->dev, irq_info->smp2p_fatal_irq, NULL,
		hfi_core_dcp_fatal_irq_handler, IRQF_TRIGGER_RISING | IRQF_ONESHOT,
		"smp2p_fatal_irq", drv_data);
	if (ret) {
		HFI_CORE_ERR("failed to acquire smp2p_fatal IRQ, ret: %d\n", ret);
		return ret;
	}

	/* SMP2P Service Ready IRQ setup */
	irq_info->smp2p_err_service_ready_irq =
		platform_get_irq_byname(pdev, "smp2p_err_service_ready_irq");
	if (irq_info->smp2p_err_service_ready_irq < 0) {
		HFI_CORE_ERR("failed to setup smp2p_err_service_ready_irq\n");
		return irq_info->smp2p_err_service_ready_irq;
	}
	HFI_CORE_DBG_H("smp2p service ready irq: %d\n", irq_info->smp2p_err_service_ready_irq);

	ret = devm_request_threaded_irq(drv_data->dev, irq_info->smp2p_err_service_ready_irq, NULL,
		hfi_core_dcp_smp2p_irq_handler, IRQF_TRIGGER_RISING | IRQF_ONESHOT,
		"smp2p_err_service_ready_irq", drv_data);
	if (ret) {
		HFI_CORE_ERR("failed to acquire error service ready IRQ, ret: %d\n", ret);
		return ret;
	}

	/* SMP2P DCP Clock Ready IRQ setup */
	irq_info->smp2p_dcp_clock_ready_irq =
		platform_get_irq_byname(pdev, "smp2p_dcp_clock_ready_irq");
	if (irq_info->smp2p_dcp_clock_ready_irq < 0) {
		HFI_CORE_ERR("failed to setup smp2p_dcp_clock_ready_irq\n");
		return irq_info->smp2p_dcp_clock_ready_irq;
	}
	HFI_CORE_DBG_H("smp2p dcp clock ready irq: %d\n", irq_info->smp2p_dcp_clock_ready_irq);

	ret = devm_request_threaded_irq(drv_data->dev, irq_info->smp2p_dcp_clock_ready_irq, NULL,
		hfi_core_dcp_smp2p_irq_handler, IRQF_TRIGGER_RISING | IRQF_ONESHOT,
		"smp2p_dcp_clock_ready_irq", drv_data);
	if (ret) {
		HFI_CORE_ERR("failed to acquire dcp clock ready IRQ, ret: %d\n", ret);
		return ret;
	}
	disable_irq_wake(irq_info->smp2p_dcp_clock_ready_irq);

	/* SMP2P Pong IRQ setup */
	irq_info->smp2p_dcp_pong_irq = platform_get_irq_byname(pdev, "smp2p_dcp_pong_irq");
	if (irq_info->smp2p_dcp_pong_irq < 0) {
		HFI_CORE_ERR("failed to setup smp2p_dcp_pong_irq\n");
		return irq_info->smp2p_dcp_pong_irq;
	}
	HFI_CORE_DBG_H("smp2p dcp pong irq: %d\n", irq_info->smp2p_dcp_pong_irq);

	ret = devm_request_threaded_irq(drv_data->dev, irq_info->smp2p_dcp_pong_irq, NULL,
		hfi_core_dcp_smp2p_irq_handler, IRQF_TRIGGER_RISING | IRQF_ONESHOT,
		"smp2p_dcp_pong_irq", drv_data);
	if (ret) {
		HFI_CORE_ERR("failed to acquire dcp pong IRQ, ret: %d\n", ret);
		return ret;
	}

	/* SMP2P Stop ACK IRQ setup */
	irq_info->smp2p_dcp_stop_ack_irq = platform_get_irq_byname(pdev, "smp2p_dcp_stop_ack_irq");
	if (irq_info->smp2p_dcp_stop_ack_irq < 0) {
		HFI_CORE_ERR("failed to setup smp2p_dcp_stop_ack_irq\n");
		return irq_info->smp2p_dcp_stop_ack_irq;
	}
	HFI_CORE_DBG_H("smp2p dcp stop ack irq: %d\n", irq_info->smp2p_dcp_stop_ack_irq);

	ret = devm_request_threaded_irq(drv_data->dev, irq_info->smp2p_dcp_stop_ack_irq, NULL,
		hfi_core_dcp_smp2p_irq_handler, IRQF_TRIGGER_RISING | IRQF_ONESHOT,
		"smp2p_dcp_stop_ack_irq", drv_data);
	if (ret) {
		HFI_CORE_ERR("failed to acquire dcp stop ack IRQ, ret: %d\n", ret);
		return ret;
	}

	HFI_CORE_DBG_H("-\n");
	return 0;
}

int hfi_core_ssr_irq_deinit(struct hfi_core_drv_data *drv_data)
{
	HFI_CORE_DBG_H("+\n");

	if (!drv_data) {
		HFI_CORE_ERR("null driver data\n");
		return -EINVAL;
	}
	struct hfi_core_irq_info *irq_info = &drv_data->irq_info;

	/*
	 * devm_* requested irq's are freed automatically during the destruction of device.
	 * Hence, no need to specifically call devm_free_irq() to destroy these irqs
	 */
	irq_info->dcp_wdog_bus_error_irq = INVALID_IRQ;
	irq_info->smp2p_fatal_irq = INVALID_IRQ;
	irq_info->smp2p_err_service_ready_irq = INVALID_IRQ;
	irq_info->smp2p_dcp_clock_ready_irq = INVALID_IRQ;
	irq_info->smp2p_dcp_pong_irq = INVALID_IRQ;
	irq_info->smp2p_dcp_stop_ack_irq = INVALID_IRQ;

	HFI_CORE_DBG_H("-\n");
	return 0;
}
