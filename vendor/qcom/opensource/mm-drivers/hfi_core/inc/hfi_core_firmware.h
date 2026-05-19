/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __HFI_CORE_FIRMWARE_H__
#define __HFI_CORE_FIRMWARE_H__

#include "hfi_core.h"

/**
 * hfi_core_firmware_load - Loads the firmware image into memory
 *
 * This function loads the firmware image into memory and unmaps the
 * memory from HLOS to make sure that the memory is not accesible to
 * HLOS. Then assign that memory to DCP to allow DCP to access it.
 *
 * @drv_data: Pointer to the HFI core driver data structure
 *
 * Return: 0 on success, negative error code on failure.
 */
int hfi_core_firmware_load(struct hfi_core_drv_data *drv_data);

/**
 * hfi_core_firmware_unload - Unloads the firmware image from memory
 *
 * This function unmaps the firmware from DCP and assign the memory back
 * to HLOS. Now HLOS can do the core dump(full firmware memory region) to
 * get the debug information for subsystem restart failure
 *
 * @drv_data: Pointer to the HFI core driver data structure
 *
 * Return: 0 on success, negative error code on failure.
 */
int hfi_core_firmware_unload(struct hfi_core_drv_data *drv_data);

/**
 * hfi_core_firmware_core_dump - Dumps the firmware memory region into buffer
 *
 * This function does the core dump(full firmware memory region) to
 * get the debug information for subsystem restart failure
 *
 * @drv_data: Pointer to the HFI core driver data structure
 *
 * Return: 0 on success, negative error code on failure.
 */
int hfi_core_firmware_core_dump(struct hfi_core_drv_data *drv_data);

/**
 * hfi_core_firmware_init() - Initialize firmware info for hfi core driver.
 *
 * This function initializes the firmware related information for the
 * HFI core. It sets up the necessary configurations and resources that is
 * needed for loading/unloading the firmware
 *
 * @drv_data: Pointer to the HFI core driver data structure
 *
 * Return: 0 on success, negative error code on failure.
 */
int hfi_core_firmware_init(struct hfi_core_drv_data *drv_data);

/**
 * hfi_core_firmware_deinit - Deinitialize the firmware for the HFI core
 *
 * This function deinitializes firmware mechanism for the HFI core
 * It cleans up any resources allocated during the initialization process.
 *
 * @drv_data: Pointer to the HFI core driver data structure
 *
 * Return: 0 on success, negative error code on failure.
 */
int hfi_core_firmware_deinit(struct hfi_core_drv_data *drv_data);

#endif // __HFI_CORE_FW_H__
