/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef _CAM_TFE662_H_
#define _CAM_TFE662_H_

#include "cam_vfe_top_ver4.h"
#include "cam_vfe_core.h"
#include "cam_vfe_bus_ver3.h"
#include "cam_irq_controller.h"

#define CAM_TFE_BUS_VER3_662_MAX_CLIENTS     28

static struct cam_vfe_top_ver4_module_desc tfe662_ipp_mod_desc[] = {
	{
		.id = 0,
		.desc = "CLC_CHANNEL_GAIN_W4",
	},
	{
		.id  = 1,
		.desc = "CLC_BPC_PDPC_W0",
	},
	{
		.id = 2,
		.desc = "CLC_LSC_W2",
	},
	{
		.id = 3,
		.desc = "CLC_SHARED_LB_W0",
	},
	{
		.id = 4,
		.desc = "CLC_WB_BDS_W0",
	},
	{
		.id = 5,
		.desc = "CLC_CROP_RND_CLAMP_POST_BDS_W5",
	},
	{
		.id = 6,
		.desc = "CLC_BLS_W2",
	},
	{
		.id = 7,
		.desc = "CLC_BAYER_GLUT_W0",
	},
	{
		.id = 8,
		.desc = "CLC_BAYER_DS4_W2",
	},
	{
		.id = 9,
		.desc = "CLC_COLOR_XFORM_DS4_W7",
	},
	{
		.id = 10,
		.desc = "CLC_CHROMA_DS2_W9",
	},
	{
		.id = 11,
		.desc = "CLC_CROP_RND_CLAMP_Y_DS4_W1",
	},
	{
		.id = 12,
		.desc = "CLC_CROP_RND_CLAMP_C_DS4_W1",
	},
	{
		.id = 13,
		.desc = "CLC_R2PD_DS4_W1",
	},
	{
		.id = 14,
		.desc = "CLC_DOWNSCALE_4TO1_Y_W1",
	},
	{
		.id  = 15,
		.desc = "CLC_DOWNSCALE_4TO1_C_W5",
	},
	{
		.id = 16,
		.desc = "CLC_CROP_RND_CLAMP_Y_DS16_W1",
	},
	{
		.id = 17,
		.desc = "CLC_CROP_RND_CLAMP_C_DS16_W1",
	},
	{
		.id = 18,
		.desc = "CLC_R2PD_DS16_W3",
	},
	{
		.id = 19,
		.desc = "CLC_WB_GAIN_W3",
	},
	{
		.id = 20,
		.desc = "CLC_BAYER_DS2_W1",
	},
	{
		.id = 21,
		.desc = "CLC_GTM_W0",
	},
	{
		.id = 22,
		.desc = "CLC_COLOR_XFORM_AI_DS_W7",
	},
	{
		.id = 23,
		.desc = "CLC_DOWNSCALE_MN_Y_W7",
	},
	{
		.id = 24,
		.desc = "CLC_DOWNSCALE_MN_C_W8",
	},
	{
		.id = 25,
		.desc = "CLC_CROP_RND_CLAMP_Y_AI_DS_W1",
	},
	{
		.id = 26,
		.desc = "CLC_CROP_RND_CLAMP_C_AI_DS_W1",
	},
	{
		.id = 27,
		.desc = "CLC_CROP_RND_CLAMP_IDEAL_RAW_W5",
	},
	{
		.id = 28,
		.desc = "CLC_ABF_W0",
	},
	{
		.id = 29,
		.desc = "CLC_STATS_BG_W0",
	},
	{
		.id = 30,
		.desc = "CLC_STATS_BHIST_W0",
	},
	{
		.id = 31,
		.desc = "CLC_STATS_AWB_BG_W0",
	},
	{
		.id = 32,
		.desc = "CLC_STATS_AEC_BG_W1",
	},
	{
		.id = 33,
		.desc = "CLC_STATS_BAF_W0",
	},
	{
		.id = 34,
		.desc = "CLC_STATS_RS_W3",
	},
	{
		.id = 35,
		.desc = "CLC_DELAY_LINE_W0",
	},
};

/*
 * Top HM registers, Offsets w.r.t top_hm_base. If top_hm_base is 0,
 * make these offsets relative core start address.
 */
static struct cam_irq_register_set tfe662_top_irq_reg_set[] = {
	{
		.mask_reg_offset   = 0x34,
		.clear_reg_offset  = 0x40,
		.status_reg_offset = 0x4C,
		.set_reg_offset    = 0x58,
		.test_set_val      = BIT(1),
		.test_sub_val      = BIT(0),
	},
	{
		.mask_reg_offset   = 0x38,
		.clear_reg_offset  = 0x44,
		.status_reg_offset = 0x50,
		.test_set_val      = BIT(1),
		.test_sub_val      = BIT(0),
	},
	{
		.mask_reg_offset   = 0x3C,
		.clear_reg_offset  = 0x48,
		.status_reg_offset = 0x54,
		.test_set_val	   = BIT(1),
		.test_sub_val	   = BIT(0),
	},
};

static struct cam_irq_controller_reg_info tfe662_top_irq_reg_info = {
	.num_registers = 3,
	.irq_reg_set = tfe662_top_irq_reg_set,
	.global_irq_cmd_offset = 0x30,
	.global_clear_bitmask  = 0x1,
	.global_set_bitmask    = 0x10,
	.clear_all_bitmask     = 0xFFFFFFFF,
};

static uint32_t tfe662_top_debug_reg[] = {
		0xA0,
		0xA4,
		0xA8,
		0xAC,
		0xB0,
		0xB4,
		0xB8,
		0xBC,
		0xC0,
		0xC4,
};

#define CAM_TFE_662_NUM_TOP_DBG_REG ((sizeof(tfe662_top_debug_reg))/(sizeof(uint32_t)))

static struct cam_vfe_top_ver4_debug_reg_info tfe662_top_dbg_reg_info[
	CAM_TFE_662_NUM_TOP_DBG_REG][8] = {
	VFE_DBG_INFO_ARRAY_4bit("test_bus_reserved",
		"test_bus_reserved",
		"test_bus_reserved",
		"test_bus_reserved",
		"test_bus_reserved",
		"test_bus_reserved",
		"test_bus_reserved",
		"test_bus_reserved"
	),
	VFE_DBG_INFO_ARRAY_4bit("STATS_BE_TINTLESS",
		"STATS_BHIST",
		"STATS_AEC_BG",
		"STATS_AWB_BG",
		"STATS_BAF",
		"DELAY_LINE",
		"WB_BDS",
		"CROP_RND_CLAMP_IDLE_RAW"
	),
	VFE_DBG_INFO_ARRAY_4bit("LRO_IDLE",
		"CHANNEL_GAIN",
		"WM_9_RDI2",
		"WM_8_RDI1",
		"WM_7_RDI0",
		"WM_6_STATS_BAF",
		"WM_5_STATS_AEC_BG",
		"WM_4_STATS_AWB_BG"
	),
	VFE_DBG_INFO_ARRAY_4bit("WM_3_STATS_BHIST",
		"WM_2_STATS_BE_TINTLESS",
		"WM_1_IDEAL_RAW",
		"WM_0_BAYER",
		"RDI2_CAMIF",
		"RDI1_CAMIF",
		"RDI0_CAMIF",
		"PP_CAMIF"
	),
	VFE_DBG_INFO_ARRAY_4bit("STATS_RS",
		"BPC_PDPC",
		"SHARED_LB",
		"CROP_RND_CLAMP_BAYER",
		"BLS",
		"GLUT",
		"BAYER_DS4",
		"DS4_CST"
	),
	VFE_DBG_INFO_ARRAY_4bit("CDS_2",
		"CRC_DS4_Y",
		"CRC_DS4_C",
		"DS4_R2PD_Y",
		"DS4_R2PD_C",
		"Y_DS4",
		"UV_DS4",
		"CRC_DS16_Y"
	),
	VFE_DBG_INFO_ARRAY_4bit("CRC_DS16_C",
		"DS16_R2PD_Y",
		"DS16_R2PD_C",
		"WB_GAIN",
		"Bayer_DS2",
		"GTM",
		"AI_CST",
		"AI_Y_MNDS"
	),
	VFE_DBG_INFO_ARRAY_4bit("AI_C_MNDS",
		"CRC_AI_Y",
		"CRC_AI_C",
		"ABF",
		"reserved",
		"reserved",
		"reserved",
		"reserved"
	),
	VFE_DBG_INFO_ARRAY_4bit("backpressure_at_CLC",
		"backpressure_at_CLC",
		"backpressure_at_CLC",
		"backpressure_at_CLC",
		"backpressure_at_CLC",
		"backpressure_at_CLC",
		"backpressure_at_CLC",
		"backpressure_at_CLC"
	),
	VFE_DBG_INFO_ARRAY_4bit("backpressure_at_CLC",
		"backpressure_at_CLC",
		"backpressure_at_CLC",
		"backpressure_at_CLC",
		"backpressure_at_CLC",
		"backpressure_at_CLC",
		"backpressure_at_CLC",
		"backpressure_at_CLC"
	),
};

static struct cam_vfe_top_ver4_reg_offset_common tfe662_common_reg = {
	/*
	 * Top HM registers, Offsets w.r.t top_hm_base. If top_hm_base is 0,
	 * make these offsets relative core start address.
	 */
	.hw_version                             = 0x0,
	.hw_capability                          = 0x4,
	.lens_feature                           = 0x8,
	.stats_feature                          = 0xC,
	.zoom_feature                           = 0x10,
	.global_reset_cmd                       = 0x14,
	.core_cgc_ovd_0                         = 0x18,
	.ahb_cgc_ovd                            = 0x1C,
	.core_cfg_0                             = 0x24,
	.reg_update_cmd                         = 0x2C,
	.diag_config                            = 0x60,
	.diag_sensor_status                    = {0x64, 0x68},
	.diag_frm_cnt_status                   = {0x6C},
	.ipp_violation_status                   = 0x70,
	.stats_throttle_cfg_0                   = 0x74,
	.stats_throttle_cfg_1                   = 0x78,
	.top_debug_cfg                          = 0xDC,
	.num_perf_counters                           = 2,
	.perf_count_reg = {
		{
			.perf_count_cfg         = 0xE0,
			.perf_count_cfg_mc      = 0,
			.perf_pix_count         = 0xE4,
			.perf_line_count        = 0xE8,
			.perf_stall_count       = 0xEC,
			.perf_always_count      = 0xF0,
			.perf_count_status      = 0xF4,
		},
		{
			.perf_count_cfg         = 0xF8,
			.perf_count_cfg_mc      = 0,
			.perf_pix_count         = 0xFC,
			.perf_line_count        = 0x100,
			.perf_stall_count       = 0x104,
			.perf_always_count      = 0x108,
			.perf_count_status      = 0x10C,
		},
	},

	.num_top_debug_reg        = CAM_TFE_662_NUM_TOP_DBG_REG,
	.top_debug = tfe662_top_debug_reg,
	.pdaf_violation_status    = 0,
	/*
	 * Bus Wr registers, w.r.t bus_wr_base. If bus_wr_base is 0,
	 * make these offsets relative core start address.
	 */
	.bus_violation_status     = 0x64,
	.bus_overflow_status      = 0x68,
	/* Index of top irq reg reflecting frame irqs sof, eof, epoch etc.
	 */
	.frame_timing_irq_reg_idx = CAM_IFE_IRQ_CAMIF_REG_STATUS1,
	/* HW capabilities
	 */
	.capabilities = CAM_VFE_COMMON_CAP_SKIP_CORE_CFG,
};

static struct cam_vfe_ver4_path_reg_data tfe662_ipp_common_reg_data = {
	.sof_irq_mask                    = 0x1,
	.eof_irq_mask                    = 0x2,
	.epoch0_irq_mask                 = 0x4,
	.epoch1_irq_mask                 = 0x8,
	.ipp_violation_mask              = 0x3,
	.pdaf_violation_mask             = 0,
	.bayer_violation_mask            = 0,
	.diag_violation_mask             = 0x20,
	.diag_sensor_sel_mask            = 0x0,
	.diag_frm_count_mask_0           = 0x10,
	.enable_diagnostic_hw            = 0x1,
	.top_debug_cfg_en                = 3,
	.is_mc_path                      = false,
};

static struct cam_vfe_ver4_path_reg_data tfe662_vfe_full_rdi_reg_data[3] = {
	{
		.sof_irq_mask                    = 0x10,
		.eof_irq_mask                    = 0x20,
		.epoch0_irq_mask                 = 0x40,
		.epoch1_irq_mask                 = 0x80,
		.error_irq_mask                  = 0x0,
		.diag_sensor_sel_mask            = 0x2,
		.diag_frm_count_mask_0           = 0x20,
		.enable_diagnostic_hw            = 0x1,
		.top_debug_cfg_en                = 3,
	},
	{
		.sof_irq_mask                    = 0x100,
		.eof_irq_mask                    = 0x200,
		.epoch0_irq_mask                 = 0x400,
		.epoch1_irq_mask                 = 0x800,
		.error_irq_mask                  = 0x0,
		.diag_sensor_sel_mask            = 0x4,
		.diag_frm_count_mask_0           = 0x40,
		.enable_diagnostic_hw            = 0x1,
		.top_debug_cfg_en                = 3,
	},
	{
		.sof_irq_mask                    = 0x1000,
		.eof_irq_mask                    = 0x2000,
		.epoch0_irq_mask                 = 0x4000,
		.epoch1_irq_mask                 = 0x8000,
		.error_irq_mask                  = 0x0,
		.diag_sensor_sel_mask            = 0x6,
		.diag_frm_count_mask_0           = 0x80,
		.enable_diagnostic_hw            = 0x1,
		.top_debug_cfg_en                = 3,
	},
};

struct cam_vfe_ver4_path_hw_info
	tfe662_rdi_hw_info_arr[] = {
	{
		.common_reg     = &tfe662_common_reg,
		.reg_data       = &tfe662_vfe_full_rdi_reg_data[0],
	},
	{
		.common_reg     = &tfe662_common_reg,
		.reg_data       = &tfe662_vfe_full_rdi_reg_data[1],
	},
	{
		.common_reg     = &tfe662_common_reg,
		.reg_data       = &tfe662_vfe_full_rdi_reg_data[2],
	},
};

static struct cam_vfe_top_ver4_diag_reg_info tfe662_diag_reg_info[] = {
	{
		.bitmask = 0x3FFF,
		.name    = "SENSOR_HBI",
	},
	{
		.bitmask = 0x4000,
		.name    = "SENSOR_NEQ_HBI",
	},
	{
		.bitmask = 0x8000,
		.name    = "SENSOR_HBI_MIN_ERROR",
	},
	{
		.bitmask = 0xFFFFFF,
		.name    = "SENSOR_VBI",
	},
	{
		.bitmask = 0xFF,
		.name    = "FRAME_CNT_PP_PIPE",
	},
	{
		.bitmask = 0xFF00,
		.name    = "FRAME_CNT_RDI_0_PIPE",
	},
	{
		.bitmask = 0xFF0000,
		.name    = "FRAME_CNT_RDI_1_PIPE",
	},
	{
		.bitmask = 0xFF000000,
		.name    = "FRAME_CNT_RDI_2_PIPE",
	},
};

static struct cam_vfe_top_ver4_diag_reg_fields tfe662_diag_sensor_field[] = {
	{
		.num_fields = 3,
		.field      = &tfe662_diag_reg_info[0],
	},
	{
		.num_fields = 1,
		.field      = &tfe662_diag_reg_info[3],
	},
};

static struct cam_vfe_top_ver4_diag_reg_fields tfe662_diag_frame_field[] = {
	{
		.num_fields = 4,
		.field      = &tfe662_diag_reg_info[4],
	},
};

static struct cam_vfe_top_ver4_hw_info tfe662_top_hw_info = {
	.common_reg = &tfe662_common_reg,
	.vfe_full_hw_info = {
		.common_reg     = &tfe662_common_reg,
		.reg_data       = &tfe662_ipp_common_reg_data,
	},
	.rdi_hw_info            = tfe662_rdi_hw_info_arr,
	.ipp_module_desc        = tfe662_ipp_mod_desc,
	.bayer_module_desc      = NULL,
	.num_mux = 4,
	.mux_type = {
		CAM_VFE_CAMIF_VER_4_0,
		CAM_VFE_RDI_VER_1_0,
		CAM_VFE_RDI_VER_1_0,
		CAM_VFE_RDI_VER_1_0,
	},
	/*
	 * path_port_map should align to PPP path specific ports
	 */
	.num_path_port_map = 0,
	.path_port_map = {},
	.num_rdi                         = ARRAY_SIZE(tfe662_rdi_hw_info_arr),
	/*
	 * top_err_desc/num_top_errors will be NULL/0 for mimas arch.
	 * handled through top_status_reg_info.
	 */
	.num_top_errors                  = 0,
	.top_err_desc                    = NULL,
	.num_pdaf_violation_errors       = 0,
	.pdaf_violation_desc             = NULL,
	.top_debug_reg_info              = &tfe662_top_dbg_reg_info,
	.bayer_debug_reg_info            = NULL,
	.fcg_module_info                 = NULL,
	.fcg_mc_supported                = false,
	.diag_sensor_info                = tfe662_diag_sensor_field,
	.diag_frame_info                 = tfe662_diag_frame_field,
	.top_hm_base                     = 0x1800,
	.bayer_hm_base                   = 0x0,
	.fcg_clc_base                    = 0x0,
	.haf_clc_base                    = 0x0,
	.bus_wr_base                     = 0x3000,
	.bayer_hm_supported              = false,
};

/*
 * if bus_wr_base is defined in cam_vfe_bus_ver3_hw_info, offsets are w.r.t
 * bus start address, else these are defined w.r.t base of the core
 *
 */
static struct cam_irq_register_set tfe662_bus_irq_reg[2] = {
	{
		.mask_reg_offset   = 0x18,
		.clear_reg_offset  = 0x20,
		.status_reg_offset = 0x28,
		.set_reg_offset    = 0x50,
	},
	{
		.mask_reg_offset   = 0x1C,
		.clear_reg_offset  = 0x24,
		.status_reg_offset = 0x2C,
		.set_reg_offset    = 0x54,
	},
};

static struct cam_vfe_bus_ver3_err_irq_desc tfe662_bus_irq_err_desc[][32] = {
	{
		{
			.bitmask = BIT(28),
			.err_name = "CONS_VIOLATION",
			.desc = "Programming of software registers violated the constraints",
		},
		{
			.bitmask = BIT(30),
			.err_name = "VIOLATION",
			.desc = "Client has a violation in ccif protocol at input",
		},
		{
			.bitmask = BIT(31),
			.err_name = "IMAGE_SIZE_VIOLATION",
			.desc = "Programmed image size is not same as image size from the CCIF",
		},
	},
};

static struct cam_vfe_bus_ver3_hw_info tfe662_bus_hw_info = {
	.common_reg = {
	    /*
	     * if bus_wr_base is defined in cam_vfe_bus_ver3_hw_info, offsets are w.r.t
	     * bus start address, else these are defined w.r.t base of the core
	     *
	     */
		.hw_version                       = 0x0,
		.cgc_ovd                          = 0x8,
		.comp_cfg_0                       = 0xC,
		.comp_cfg_1                       = 0x10,
		.if_frameheader_cfg  = {
			0x34,
			0x38,
			0x3C,
			0x40,
		},
		.pwr_iso_cfg                      = 0x5C,
		.overflow_status_clear            = 0x60,
		.ccif_violation_status            = 0x64,
		.overflow_status                  = 0x68,
		.image_size_violation_status      = 0x70,
		.debug_status_top_cfg             = 0xD4,
		.debug_status_top                 = 0xD8,
		.test_bus_ctrl                    = 0xDC,
		.wm_mode_shift                    = 16,
		.wm_mode_val                      = { 0x0, 0x1, 0x2 },
		.wm_en_shift                      = 0,
		.frmheader_en_shift               = 2,
		.virtual_frm_en_shift             = 1,
		.irq_reg_info = {
			.num_registers            = 2,
			.irq_reg_set              = tfe662_bus_irq_reg,
			.global_irq_cmd_offset    = 0x30,
			.global_clear_bitmask     = 0x1,
		},
		.num_perf_counters                = 8,
		.perf_cnt_status                  = 0xB4,
		.perf_cnt_reg = {
			{
				.perf_cnt_cfg = 0x74,
				.perf_cnt_val = 0x94,
			},
			{
				.perf_cnt_cfg = 0x78,
				.perf_cnt_val = 0x98,
			},
			{
				.perf_cnt_cfg = 0x7C,
				.perf_cnt_val = 0x9C,
			},
			{
				.perf_cnt_cfg = 0x80,
				.perf_cnt_val = 0xA0,
			},
			{
				.perf_cnt_cfg = 0x84,
				.perf_cnt_val = 0xA4,
			},
			{
				.perf_cnt_cfg = 0x88,
				.perf_cnt_val = 0xA8,
			},
			{
				.perf_cnt_cfg = 0x8C,
				.perf_cnt_val = 0xAC,
			},
			{
				.perf_cnt_cfg = 0x90,
				.perf_cnt_val = 0xB0,
			},
		},
	},
	.bus_wr_base                              = 0x3000,
	.support_dyn_offset                       = true,
	/*
	 * client_base is w.r.t bus_wr_base. If bus_wr_base is 0,
	 * make client_base relative core start address.
	 */
	.client_base                              = 0x200,
	.client_reg_size                          = 0x100,
	.client_offsets = {
			.cfg                      = 0x0,
			.image_addr               = 0x4,
			.frame_incr               = 0x8,
			.image_cfg_0              = 0xC,
			.image_cfg_1              = 0x10,
			.image_cfg_2              = 0x14,
			.packer_cfg               = 0x18,
			.bw_limiter_addr          = 0x1C,
			.frame_header_addr        = 0x20,
			.frame_header_incr        = 0x24,
			.frame_header_cfg         = 0x28,
			.irq_subsample_period     = 0x30,
			.irq_subsample_pattern    = 0x34,
			.framedrop_period         = 0x38,
			.framedrop_pattern        = 0x3C,
			.addr_status_0            = 0x68,
			.addr_status_1            = 0x6C,
			.addr_status_2            = 0x70,
			.addr_status_3            = 0x74,
			.debug_status_cfg         = 0x78,
			.debug_status_0           = 0x7C,
			.debug_status_1           = 0x80,
			.ubwc_regs                = NULL,
	},
	.bus_client_reg = {
		/*
		 * For clients with Meta outputs, META MID is programmed in [31 : 16]
		 * Image MID is programmed in [16 : 0]
		 */
		/* BUS Client 0 BAYER */
		{
			.source_group             = CAM_VFE_BUS_VER3_SRC_GRP_0,
			.comp_group               = CAM_VFE_BUS_VER3_COMP_GRP_0,
			.supported_pack_formats   = BIT_ULL(PACKER_FMT_VER3_TP_10) |
				BIT_ULL(PACKER_FMT_VER3_MIPI10) |
				BIT_ULL(PACKER_FMT_VER3_MIPI12) |
				BIT_ULL(PACKER_FMT_VER3_MIPI14) |
				BIT_ULL(PACKER_FMT_VER3_PLAIN_8) |
				BIT_ULL(PACKER_FMT_VER3_PLAIN_128) |
				BIT_ULL(PACKER_FMT_VER3_PLAIN_16_10BPP) |
				BIT_ULL(PACKER_FMT_VER3_PLAIN_16_12BPP) |
				BIT_ULL(PACKER_FMT_VER3_PLAIN_16_14BPP) |
				BIT_ULL(PACKER_FMT_VER3_PLAIN_16_16BPP),
			.rcs_en_mask              = 0x0,
			.name                     = "BAYER",
			.line_based               = 1,
			.mid                      = {16},
			.num_mid                  = 1,
			.out_type                 = CAM_VFE_BUS_VER3_VFE_OUT_FULL,
			.mc_based                 = false,
			.pid_mask                 = BIT_ULL(6) | BIT_ULL(7),
		},
		/* BUS Client 1 IDEAL_RAW */
		{
			.source_group             = CAM_VFE_BUS_VER3_SRC_GRP_0,
			.comp_group               = CAM_VFE_BUS_VER3_COMP_GRP_1,
			.supported_pack_formats   = BIT_ULL(PACKER_FMT_VER3_TP_10) |
				BIT_ULL(PACKER_FMT_VER3_MIPI10) |
				BIT_ULL(PACKER_FMT_VER3_MIPI12) |
				BIT_ULL(PACKER_FMT_VER3_MIPI14) |
				BIT_ULL(PACKER_FMT_VER3_PLAIN_8) |
				BIT_ULL(PACKER_FMT_VER3_PLAIN_128) |
				BIT_ULL(PACKER_FMT_VER3_PLAIN_16_10BPP) |
				BIT_ULL(PACKER_FMT_VER3_PLAIN_16_12BPP) |
				BIT_ULL(PACKER_FMT_VER3_PLAIN_16_14BPP) |
				BIT_ULL(PACKER_FMT_VER3_PLAIN_16_16BPP),
			.name                     = "IDEAL_RAW",
			.line_based               = 1,
			.mid                      = {19},
			.num_mid                  = 1,
			.out_type                 = CAM_VFE_BUS_VER3_VFE_OUT_IDEAL_RAW,
			.mc_based                 = false,
			.pid_mask                 = BIT_ULL(4) | BIT_ULL(5),
		},
		/* BUS Client 2 STATS BE TINTLESS */
		{
			.source_group             = CAM_VFE_BUS_VER3_SRC_GRP_0,
			.comp_group               = CAM_VFE_BUS_VER3_COMP_GRP_2,
			.supported_pack_formats   = BIT_ULL(PACKER_FMT_VER3_PLAIN_128) |
				BIT_ULL(PACKER_FMT_VER3_PLAIN_64),
			.name                     = "STATS BE TINTLESS",
			.mid                      = {17},
			.num_mid                  = 1,
			.out_type                 = CAM_VFE_BUS_VER3_VFE_OUT_STATS_TL_BG,
			.mc_based                 = false,
			.pid_mask                 = BIT_ULL(6) | BIT_ULL(7),
		},
		/* BUS Client 3 STATS BHIST */
		{
			.source_group             = CAM_VFE_BUS_VER3_SRC_GRP_0,
			.comp_group               = CAM_VFE_BUS_VER3_COMP_GRP_2,
			.supported_pack_formats   = BIT_ULL(PACKER_FMT_VER3_PLAIN_128) |
				BIT_ULL(PACKER_FMT_VER3_PLAIN_64),
			.name                     = "STATS BHIST",
			.mid                      = {18},
			.num_mid                  = 1,
			.out_type                 = CAM_VFE_BUS_VER3_VFE_OUT_STATS_BHIST,
			.mc_based                 = false,
			.pid_mask                 = BIT_ULL(6) | BIT_ULL(7),
		},
		/* BUS Client 4 STATS AWB BG */
		{
			.source_group             = CAM_VFE_BUS_VER3_SRC_GRP_0,
			.comp_group               = CAM_VFE_BUS_VER3_COMP_GRP_3,
			.supported_pack_formats   = BIT_ULL(PACKER_FMT_VER3_PLAIN_128) |
				BIT_ULL(PACKER_FMT_VER3_PLAIN_64),
			.name                     = "STATS AWB BG",
			.mid                      = {19},
			.num_mid                  = 1,
			.out_type                 = CAM_VFE_BUS_VER3_VFE_OUT_STATS_AWB_BG,
			.mc_based                 = false,
			.pid_mask                 = BIT_ULL(6) | BIT_ULL(7),
		},
		/* BUS Client 5 STATS AEC BG */
		{
			.source_group             = CAM_VFE_BUS_VER3_SRC_GRP_0,
			.comp_group               = CAM_VFE_BUS_VER3_COMP_GRP_3,
			.supported_pack_formats   = BIT_ULL(PACKER_FMT_VER3_PLAIN_128) |
				BIT_ULL(PACKER_FMT_VER3_PLAIN_64),
			.rcs_en_mask              = 0x0,
			.name                     = "STATS AEC BG",
			.mid                      = {20},
			.num_mid                  = 1,
			.out_type                 = CAM_VFE_BUS_VER3_VFE_OUT_STATS_AEC_BE,
			.mc_based                 = false,
			.pid_mask                 = BIT_ULL(6) | BIT_ULL(7),
		},
		/* BUS Client 6 STATS BAF */
		{
			.source_group             = CAM_VFE_BUS_VER3_SRC_GRP_0,
			.comp_group               = CAM_VFE_BUS_VER3_COMP_GRP_4,
			.supported_pack_formats   = BIT_ULL(PACKER_FMT_VER3_PLAIN_128) |
				BIT_ULL(PACKER_FMT_VER3_PLAIN_64),
			.rcs_en_mask              = 0x0,
			.name                     = "STATS BAF",
			.mid                      = {21},
			.early_done_mask          = BIT(28),
			.num_mid                  = 1,
			.out_type                 = CAM_VFE_BUS_VER3_VFE_OUT_STATS_BF,
			.mc_based                 = false,
			.pid_mask                 = BIT_ULL(6) | BIT_ULL(7),
		},
		/* BUS Client 7 RDI0 */
		{
			.source_group             = CAM_VFE_BUS_VER3_SRC_GRP_1,
			.comp_group               = CAM_VFE_BUS_VER3_COMP_GRP_5,
			.supported_pack_formats   = BIT_ULL(PACKER_FMT_VER3_PLAIN_128),
			.name                     = "RDI0",
			.line_based               = 1,
			.mid                      = {16},
			.num_mid                  = 1,
			.out_type                 = CAM_VFE_BUS_VER3_VFE_OUT_RDI0,
			.cntxt_cfg_except         = false,
			.pid_mask                 = BIT_ULL(4) | BIT_ULL(5),
		},
		/* BUS Client 8 RDI1 */
		{
			.source_group             = CAM_VFE_BUS_VER3_SRC_GRP_2,
			.comp_group               = CAM_VFE_BUS_VER3_COMP_GRP_6,
			.supported_pack_formats   = BIT_ULL(PACKER_FMT_VER3_PLAIN_128),
			.name                     = "RDI1",
			.line_based               = 1,
			.mid                      = {17},
			.num_mid                  = 1,
			.out_type                 = CAM_VFE_BUS_VER3_VFE_OUT_RDI1,
			.cntxt_cfg_except         = false,
			.pid_mask                 = BIT_ULL(4) | BIT_ULL(5),
		},
		/* BUS Client 9 RDI2 */
		{
			.source_group             = CAM_VFE_BUS_VER3_SRC_GRP_3,
			.comp_group               = CAM_VFE_BUS_VER3_COMP_GRP_7,
			.supported_pack_formats   = BIT_ULL(PACKER_FMT_VER3_PLAIN_128),
			.name                     = "RDI2",
			.line_based               = 1,
			.mid                      = {18},
			.num_mid                  = 1,
			.out_type                 = CAM_VFE_BUS_VER3_VFE_OUT_RDI2,
			.cntxt_cfg_except         = false,
			.pid_mask                 = BIT_ULL(4) | BIT_ULL(5),
		},
		/* BUS Client 10 PDAF */
		{
			.source_group             = CAM_VFE_BUS_VER3_SRC_GRP_0,
			.comp_group               = CAM_VFE_BUS_VER3_COMP_GRP_8,
			.supported_pack_formats   = BIT_ULL(PACKER_FMT_VER3_PLAIN_8) |
				BIT_ULL(PACKER_FMT_VER3_PLAIN_128) |
				BIT_ULL(PACKER_FMT_VER3_PLAIN_16_10BPP),
			.name                     = "PDAF",
			.mid                      = {26},
			.num_mid                  = 1,
			.out_type                 = CAM_VFE_BUS_VER3_VFE_OUT_PDAF,
			.mc_based                 = false,
			.pid_mask                 = BIT_ULL(6) | BIT_ULL(7),
		},
		/* BUS Client 11 DS4 */
		{
			.source_group             = CAM_VFE_BUS_VER3_SRC_GRP_0,
			.comp_group               = CAM_VFE_BUS_VER3_COMP_GRP_0,
			.supported_pack_formats   = BIT_ULL(PACKER_FMT_VER3_PLAIN_128),
			.name                     = "DS4",
			.line_based               = 1,
			.mid                      = {22},
			.num_mid                  = 1,
			.out_type                 = CAM_VFE_BUS_VER3_VFE_OUT_DS4,
			.mc_based                 = false,
			.pid_mask                 = BIT_ULL(6) | BIT_ULL(7),
		},
		/* BUS Client 12 DS16 */
		{
			.source_group             = CAM_VFE_BUS_VER3_SRC_GRP_0,
			.comp_group               = CAM_VFE_BUS_VER3_COMP_GRP_0,
			.supported_pack_formats   = BIT_ULL(PACKER_FMT_VER3_PLAIN_128),
			.name                     = "DS16",
			.line_based               = 1,
			.mid                      = {23},
			.num_mid                  = 1,
			.out_type                 = CAM_VFE_BUS_VER3_VFE_OUT_DS16,
			.mc_based                 = false,
			.pid_mask                 = BIT_ULL(6) | BIT_ULL(7),
		},
		/* BUS Client 13 AI-Y = FD_Y */
		{
			.source_group             = CAM_VFE_BUS_VER3_SRC_GRP_0,
			.comp_group               = CAM_VFE_BUS_VER3_COMP_GRP_9,
			.supported_pack_formats   = BIT_ULL(PACKER_FMT_VER3_TP_10) |
				BIT_ULL(PACKER_FMT_VER3_MIPI10) |
				BIT_ULL(PACKER_FMT_VER3_MIPI12) |
				BIT_ULL(PACKER_FMT_VER3_MIPI14) |
				BIT_ULL(PACKER_FMT_VER3_PLAIN_8) |
				BIT_ULL(PACKER_FMT_VER3_PLAIN_128) |
				BIT_ULL(PACKER_FMT_VER3_PLAIN_16_10BPP) |
				BIT_ULL(PACKER_FMT_VER3_PLAIN_16_12BPP) |
				BIT_ULL(PACKER_FMT_VER3_PLAIN_16_14BPP) |
				BIT_ULL(PACKER_FMT_VER3_PLAIN_16_16BPP),
			.name                     = "FD_Y",
			.line_based               = 1,
			.mid                      = {24},
			.num_mid                  = 1,
			.out_type                 = CAM_VFE_BUS_VER3_VFE_OUT_FD,
			.mc_based                 = false,
			.pid_mask                 = BIT_ULL(6) | BIT_ULL(7),
		},
		/* BUS Client 14 AI-C = FD_C */
		{
			.source_group             = CAM_VFE_BUS_VER3_SRC_GRP_0,
			.comp_group               = CAM_VFE_BUS_VER3_COMP_GRP_9,
			.supported_pack_formats   = BIT_ULL(PACKER_FMT_VER3_TP_10) |
				BIT_ULL(PACKER_FMT_VER3_MIPI10) |
				BIT_ULL(PACKER_FMT_VER3_MIPI12) |
				BIT_ULL(PACKER_FMT_VER3_MIPI14) |
				BIT_ULL(PACKER_FMT_VER3_PLAIN_8) |
				BIT_ULL(PACKER_FMT_VER3_PLAIN_128) |
				BIT_ULL(PACKER_FMT_VER3_PLAIN_16_10BPP) |
				BIT_ULL(PACKER_FMT_VER3_PLAIN_16_12BPP) |
				BIT_ULL(PACKER_FMT_VER3_PLAIN_16_14BPP) |
				BIT_ULL(PACKER_FMT_VER3_PLAIN_16_16BPP),
			.name                     = "FD_C",
			.line_based               = 1,
			.mid                      = {25},
			.num_mid                  = 1,
			.out_type                 = CAM_VFE_BUS_VER3_VFE_OUT_FD,
			.mc_based                 = false,
			.pid_mask                 = BIT_ULL(6) | BIT_ULL(7),
		},
		/* BUS Client 15 STATS RS */
		{
			.source_group             = CAM_VFE_BUS_VER3_SRC_GRP_0,
			.comp_group               = CAM_VFE_BUS_VER3_COMP_GRP_10,
			.supported_pack_formats   = BIT_ULL(PACKER_FMT_VER3_PLAIN_32),
			.name                     = "STATS RS",
			.mid                      = {27},
			.num_mid                  = 1,
			.out_type                 = CAM_VFE_BUS_VER3_VFE_OUT_STATS_RS,
			.mc_based                 = false,
			.pid_mask                 = BIT_ULL(6) | BIT_ULL(7),
		},
	},
	.valid_wm_mask   = 0xFFFF,
	/* constraint error description not available */
	.num_cons_err = 26,
	.constraint_error_list = {
		{
			.bitmask = BIT(0),
			.error_description = "PPC 1x1 input Not Supported"
		},
		{
			.bitmask = BIT(1),
			.error_description = "PPC 1x2 input Not Supported"
		},
		{
			.bitmask = BIT(2),
			.error_description = "PPC 2x1 input Not Supported"
		},
		{
			.bitmask = BIT(3),
			.error_description = "PPC 2x2 input Not Supported"
		},
		{
			.bitmask = BIT(4),
			.error_description = "Pack 8 BPP format Not Supported"
		},
		{
			.bitmask = BIT(5),
			.error_description = "Pack 16 BPP format Not Supported"
		},
		{
			.bitmask = BIT(6),
			.error_description = "Pack 32 BPP format Not Supported"
		},
		{
			.bitmask = BIT(7),
			.error_description = "Pack 64 BPP format Not Supported"
		},
		{
			.bitmask = BIT(8),
			.error_description = "Pack MIPI 12 format Not Supported"
		},
		{
			.bitmask = BIT(9),
			.error_description = "Pack MIPI 10 format Not Supported"
		},
		{
			.bitmask = BIT(10),
			.error_description = "Pack 128 BPP format Not Supported"
		},
		{
			.bitmask = BIT(11),
			.error_description = "UBWC NV12 format Not Supported"
		},
		{
			.bitmask = BIT(12),
			.error_description = "UBWC NV12 4R format Not Supported"
		},
		{
			.bitmask = BIT(13),
			.error_description = "UBWC TP10 format Not Supported"
		},
		{
			.bitmask = BIT(14),
			.error_description = "Frame based Mode Not Supported"
		},
		{
			.bitmask = BIT(15),
			.error_description = "Index based Mode Not Supported"
		},
		{
			.bitmask = BIT(16),
			.error_description = "FIFO image addr unalign"
		},
		{
			.bitmask = BIT(17),
			.error_description = "FIFO ubwc addr unalign"
		},
		{
			.bitmask = BIT(18),
			.error_description = "FIFO framehdr addr unalign"
		},
		{
			.bitmask = BIT(19),
			.error_description = "Image address unalign"
		},
		{
			.bitmask = BIT(20),
			.error_description = "UBWC address unalign"
		},
		{
			.bitmask = BIT(21),
			.error_description = "Frame Header address unalign"
		},
		{
			.bitmask = BIT(22),
			.error_description = "X Initialization unalign"
		},
		{
			.bitmask = BIT(23),
			.error_description = "Image Width unalign",
		},
		{
			.bitmask = BIT(24),
			.error_description = "Image Height unalign",
		},
		{
			.bitmask = BIT(25),
			.error_description = "Meta Stride unalign",
		},
	},
	.bus_err_irq_mask      = { 0xD0000000, 0x0},
	.num_bus_errors        = 1,
	.bus_err_desc          = &tfe662_bus_irq_err_desc,
	.num_comp_grp          = 11,
	.support_consumed_addr = true,
	.comp_done_mask = { BIT(8), BIT(9), BIT(10), BIT(11), BIT(12), BIT(13),
			BIT(14), BIT(15), BIT(16), BIT(17), BIT(18),
	},
	.top_irq_shift         = 1,
	.max_out_res           = CAM_ISP_IFE_OUT_RES_IDEAL_RAW + 1,
	.pack_align_shift      = 4,
	.max_bw_counter_limit  = 0xFF,
	.skip_regdump          = false,
	.skip_regdump_start_offset = 0,
	.skip_regdump_stop_offset = 0,
};

static struct cam_vfe_irq_hw_info tfe662_irq_hw_info = {
	.reset_mask    = 0,
	.supported_irq = CAM_VFE_HW_IRQ_CAP_BUF_DONE | CAM_VFE_HW_IRQ_CAP_RUP,
	.top_irq_reg   = &tfe662_top_irq_reg_info,
};

static struct cam_vfe_hw_info cam_tfe662_hw_info = {
	.irq_hw_info                  = &tfe662_irq_hw_info,

	.bus_version                   = CAM_VFE_BUS_VER_3_0,
	.bus_hw_info                   = &tfe662_bus_hw_info,

	.top_version                   = CAM_VFE_TOP_VER_4_0,
	.top_hw_info                   = &tfe662_top_hw_info,
};
#endif /* _CAM_TFE662_H_ */
