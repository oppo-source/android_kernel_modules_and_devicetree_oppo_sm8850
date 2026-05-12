/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __HFI_CORE_SSR_H__
#define __HFI_CORE_SSR_H__

struct hfi_core_drv_data;

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
int hfi_core_ssr_init(struct hfi_core_drv_data *drv_data);

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
int hfi_core_ssr_deinit(struct hfi_core_drv_data *drv_data);

#endif // __HFI_CORE_SSR_H__
