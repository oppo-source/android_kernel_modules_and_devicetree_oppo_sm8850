// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/types.h>
#include <linux/list.h>
#include <linux/of_address.h>
#include <linux/version.h>
#if (KERNEL_VERSION(6, 3, 0) <= LINUX_VERSION_CODE)
#include <linux/firmware/qcom/qcom_scm.h>
#else
#include <linux/qcom_scm.h>
#endif
#include <linux/soc/qcom/mdt_loader.h>
#include <linux/soc/qcom/smem.h>
#include <linux/devcoredump.h>
#include <linux/firmware.h>

#include "hfi_core_firmware.h"
#include "hfi_core.h"
#include "hfi_core_debug.h"

int hfi_core_firmware_load(struct hfi_core_drv_data *drv_data)
{
	const struct firmware *firmware = NULL;
	ssize_t fw_size = 0;
	void *virt = NULL;
	struct device *dev = (struct device *)drv_data->dev;
	int ret = 0;

	HFI_CORE_DBG_H("+\n");

	if (!drv_data) {
		HFI_CORE_ERR("null driver data\n");
		return -EINVAL;
	}
	dev = (struct device *)drv_data->dev;

	ret = request_firmware(&firmware, drv_data->firmware_info.firmware_name, dev);
	if (ret) {
		HFI_CORE_ERR("failed to request fw \"%s\", error %d\n",
			drv_data->firmware_info.firmware_name, ret);
		return ret;
	}

	fw_size = qcom_mdt_get_size(firmware);
	if (fw_size < 0 || drv_data->firmware_info.fw_mem_size < (size_t)fw_size) {
		ret = -EINVAL;
		HFI_CORE_ERR("out of bound fw image fw size: %ld, fw_mem_size: %lu",
			fw_size, drv_data->firmware_info.fw_mem_size);
		goto cleanup;
	}

	virt = memremap(drv_data->firmware_info.phys_fw_mem_addr,
		drv_data->firmware_info.fw_mem_size, MEMREMAP_WC);
	if (!virt) {
		HFI_CORE_ERR("failed to remap fw memory phys %llu[p]\n",
			drv_data->firmware_info.phys_fw_mem_addr);
		ret = -ENOMEM;
		goto cleanup;
	}

	/* prevent system suspend during fw_load */
	pm_stay_awake(dev->parent);

	ret = qcom_mdt_load(dev, firmware, drv_data->firmware_info.firmware_name,
		drv_data->firmware_info.pas_id, virt, drv_data->firmware_info.phys_fw_mem_addr,
		drv_data->firmware_info.fw_mem_size, NULL);

	pm_relax(dev->parent);
	if (ret) {
		HFI_CORE_ERR("error %d loading fw %s\n", ret,
			drv_data->firmware_info.firmware_name);
		goto cleanup;
	}

	ret = qcom_scm_pas_auth_and_reset(drv_data->firmware_info.pas_id);
	if (ret) {
		HFI_CORE_ERR("error %d authenticating fw \"%s\"\n", ret,
			drv_data->firmware_info.firmware_name);
		goto cleanup;
	}

	HFI_CORE_DBG_INFO("firmware \"%s\" loaded successfully\n",
		drv_data->firmware_info.firmware_name);

cleanup:
	if (virt)
		memunmap(virt);
	if (firmware)
		release_firmware(firmware);

	HFI_CORE_DBG_H("-\n");
	return ret;
}

int hfi_core_firmware_unload(struct hfi_core_drv_data *drv_data)
{
	int ret = 0;

	HFI_CORE_DBG_H("+\n");

	if (!drv_data) {
		HFI_CORE_ERR("null driver data\n");
		return -EINVAL;
	}

	ret = qcom_scm_pas_shutdown(drv_data->firmware_info.pas_id);
	if (ret) {
		HFI_CORE_ERR("Firmware unload failed ret=%d\n", ret);
		return ret;
	}

	HFI_CORE_DBG_H("-\n");
	return 0;
}

int hfi_core_firmware_core_dump(struct hfi_core_drv_data *drv_data)
{
	phys_addr_t fw_mem_phys;
	void *fw_mem_va = NULL;
	size_t fw_mem_size;
	void *dump = NULL;

	HFI_CORE_DBG_H("+\n");

	if (!drv_data) {
		HFI_CORE_ERR("null driver data\n");
		return -EINVAL;
	}

	fw_mem_phys = drv_data->firmware_info.phys_fw_mem_addr;
	fw_mem_size = drv_data->firmware_info.fw_mem_size;

	fw_mem_va = memremap(fw_mem_phys, fw_mem_size, MEMREMAP_WC);
	if (!fw_mem_va) {
		HFI_CORE_ERR("unable to remap firmware memory\n");
		return -ENOMEM;
	}

	dump = vmalloc(fw_mem_size);
	if (!dump) {
		memunmap(fw_mem_va);
		HFI_CORE_ERR("unable to allocate memory to dump fw mem region\n");
		return -ENOMEM;
	}

	/* copy firmware dump */
	memcpy(dump, fw_mem_va, fw_mem_size);
	memunmap(fw_mem_va);

	dev_coredumpv(drv_data->dev, dump, fw_mem_size, GFP_KERNEL);

	HFI_CORE_DBG_H("-\n");
	return 0;
}

int hfi_core_firmware_init(struct hfi_core_drv_data *drv_data)
{
	int ret = 0;
	struct device_node *mem_node = NULL;
	struct resource res = { 0 };
	struct device *dev;

	HFI_CORE_DBG_H("+\n");

	if (!drv_data) {
		HFI_CORE_ERR("invalid params\n");
		return -EINVAL;
	}
	dev = (struct device *)drv_data->dev;

	if (!IS_ENABLED(CONFIG_QCOM_MDT_LOADER) || !qcom_scm_is_available()) {
		HFI_CORE_ERR("mdt loader enable status: %d or qcom scm is unavailable\n",
			IS_ENABLED(CONFIG_QCOM_MDT_LOADER));
		return -EPROBE_DEFER;
	}

	mem_node = of_parse_phandle(dev->of_node, "memory-region", 0);
	if (!mem_node) {
		HFI_CORE_ERR("failed to read \"memory-region\"\n");
		return -EINVAL;
	}

	ret = of_address_to_resource(mem_node, 0, &res);
	if (ret) {
		HFI_CORE_ERR("failed to read \"memory-region\", error %d\n", ret);
		return ret;
	}

	drv_data->firmware_info.phys_fw_mem_addr = res.start;
	drv_data->firmware_info.fw_mem_size = (size_t)(res.end - res.start + 1);

	ret = of_property_read_u32(dev->of_node, "qcom,pas-id", &drv_data->firmware_info.pas_id);
	if (ret) {
		HFI_CORE_ERR("failed to read qcom,pas-id %d\n", ret);
		return ret;
	}

	ret = of_property_read_string(dev->of_node, "qcom,fw_image_name",
		&drv_data->firmware_info.firmware_name);
	if (ret) {
		HFI_CORE_ERR("failed to read qcom,fw_image_name %d\n", ret);
		return ret;
	}

	HFI_CORE_DBG_INFO("fw_mem_addr: %llu fw_mem_size: %zu pas_id: %d fw_name: %s\n",
		drv_data->firmware_info.phys_fw_mem_addr, drv_data->firmware_info.fw_mem_size,
		drv_data->firmware_info.pas_id, drv_data->firmware_info.firmware_name);

	HFI_CORE_DBG_H("-\n");
	return 0;
}

int hfi_core_firmware_deinit(struct hfi_core_drv_data *drv_data)
{
	HFI_CORE_DBG_H("+\n");

	drv_data->firmware_info.phys_fw_mem_addr = 0x0;
	drv_data->firmware_info.fw_mem_size = 0;
	drv_data->firmware_info.pas_id = 0;
	drv_data->firmware_info.firmware_name = NULL;

	HFI_CORE_DBG_H("-\n");
	return 0;
}
