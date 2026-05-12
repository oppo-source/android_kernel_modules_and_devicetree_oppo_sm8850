/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __HFI_CORE_IRQ_H__
#define __HFI_CORE_IRQ_H__

#include "hfi_core.h"

/**
 * hfi_core_irq_wait - Wait for a specific IRQ signal
 *
 * This function waits for the specified IRQ signal to be triggered.
 *
 * @drv_data: Pointer to the driver data structure
 * @irq_signal: The IRQ signal to wait for
 *
 * Return: 0 on success, negative error code on failure
 */
int hfi_core_irq_wait(struct hfi_core_drv_data *drv_data, enum hfi_core_irq_signal irq_signal);

/**
 * hfi_core_ssr_irq_init() - Initialize IRQs of hfi core driver required for SSR.
 *
 * This call registers interrupts for ssr notification, error service ready notification
 * dcp clock ready notification and dcp alive notification. It sets up the necessary
 * interrupt handlers and enables the IRQs required for the HFI core
 * to function correctly.
 *
 * @drv_data: Pointer to the HFI core driver data structure
 *
 * Return: 0 on success, negative error code on failure.
 */
int hfi_core_ssr_irq_init(struct hfi_core_drv_data *drv_data);

/**
 * hfi_core_ssr_irq_deinit - Deinitialize the IRQ for the HFI core required for SSR.
 *
 * This function deinitializes the interrupt request (IRQ) handling for the
 * HFI core. It free up IRQs and cleans up any resources allocated during
 * the initialization process.
 *
 * @drv_data: Pointer to the HFI core driver data structure
 *
 * Return: 0 on success, negative error code on failure.
 */
int hfi_core_ssr_irq_deinit(struct hfi_core_drv_data *drv_data);

#endif // __HFI_CORE_IRQ_H__
