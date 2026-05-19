/***************************************************************
** Copyright (C), 2024, OPLUS Mobile Comm Corp., Ltd
**
** File : oplus_display_parse.c
** Description : oplus display dts parse implement
** Version : 1.1
** Date : 2024/05/09
** Author : Display
******************************************************************/
#include "oplus_display_parse.h"
#include "oplus_display_esd.h"
#include "oplus_display_bl.h"
#include "oplus_display_power.h"
#include "oplus_display_pwm.h"
#include "oplus_display_interface.h"
#include "oplus_display_device_ioctl.h"
#include "oplus_display_ffl.h"
#include "oplus_debug.h"
#include "oplus_display_ext.h"
#include "oplus_display_dfte.h"
#ifdef OPLUS_FEATURE_AP_UIR_DIMMING
#include "oplus_apuirdim.h"
#endif

extern int dynamic_osc_clock;
bool oplus_enhance_mipi_strength = false;
bool apollo_backlight_enable = false;
bool ktz8869_use = false;

static int oplus_panel_parse_common_config(struct dsi_panel *panel)
{
	int ret = 0;

	struct dsi_parser_utils *utils = &panel->utils;

		panel->oplus_panel.vendor_name = utils->get_property(utils->data,
			"oplus,mdss-dsi-vendor-name", NULL);

	if (!panel->oplus_panel.vendor_name) {
		OPLUS_DSI_ERR("Failed to found panel name, using dumming name\n");
		panel->oplus_panel.vendor_name = DSI_PANEL_OPLUS_DUMMY_VENDOR_NAME;
	}

	panel->oplus_panel.manufacture_name = utils->get_property(utils->data,
			"oplus,mdss-dsi-manufacture", NULL);

	if (!panel->oplus_panel.manufacture_name) {
		OPLUS_DSI_ERR("Failed to found panel name, using dumming name\n");
		panel->oplus_panel.manufacture_name = DSI_PANEL_OPLUS_DUMMY_MANUFACTURE_NAME;
	}

	panel->oplus_panel.gpio_pre_on = utils->read_bool(utils->data,
			"oplus,gpio-pre-on");
	OPLUS_DSI_INFO("oplus,gpio-pre-on: %s\n",
		panel->oplus_panel.gpio_pre_on ? "true" : "false");

	panel->oplus_panel.panel_id_switch_page = utils->read_bool(utils->data,
			"oplus,dsi-panel-id-switch-page");
	OPLUS_DSI_INFO("oplus,dsi-panel-id-switch-page: %s\n",
		panel->oplus_panel.panel_id_switch_page ? "true" : "false");

	panel->oplus_panel.is_osc_support = utils->read_bool(utils->data, "oplus,osc-support");
	OPLUS_DSI_INFO("osc mode support: %s\n", panel->oplus_panel.is_osc_support ? "Yes" : "Not");

	panel->oplus_panel.is_apl_read_support = utils->read_bool(utils->data, "oplus,apl-read-support");
	OPLUS_DSI_INFO("apl read support: %s\n", panel->oplus_panel.is_apl_read_support ? "Yes" : "Not");

	if (panel->oplus_panel.is_osc_support) {
		ret = utils->read_u32(utils->data, "oplus,mdss-dsi-osc-clk-mode0-rate",
				&panel->oplus_panel.osc_clk_mode0_rate);
		if (ret) {
			OPLUS_DSI_ERR("failed get panel parameter: oplus,mdss-dsi-osc-clk-mode0-rate\n");
			panel->oplus_panel.osc_clk_mode0_rate = 0;
		}
		dynamic_osc_clock = panel->oplus_panel.osc_clk_mode0_rate;

		ret = utils->read_u32(utils->data, "oplus,mdss-dsi-osc-clk-mode1-rate",
				&panel->oplus_panel.osc_clk_mode1_rate);
		if (ret) {
			OPLUS_DSI_ERR("failed get panel parameter: oplus,mdss-dsi-osc-clk-mode1-rate\n");
			panel->oplus_panel.osc_clk_mode1_rate = 0;
		}
	}

	panel->oplus_panel.gamma_compensation_support = utils->read_bool(utils->data, "oplus,gamma-compensation-support");
	OPLUS_DSI_INFO("panel gamma compensation support: %s\n", panel->oplus_panel.gamma_compensation_support ? "Yes" : "Not");

	panel->oplus_panel.gamma_ae174_compensation_support = utils->read_bool(utils->data, "oplus,panel-ae174-gamma-compensation-support");
	OPLUS_DSI_INFO("panel ae174 gamma compensation support: %s\n", panel->oplus_panel.gamma_ae174_compensation_support ? "Yes" : "Not");

	panel->oplus_panel.pl_check_enable = utils->read_bool(utils->data, "oplus,pcd-lvd-check-enable");
	OPLUS_DSI_INFO("oplus,pcd-lvd-check-enable: %s\n", panel->oplus_panel.pl_check_enable ? "Yes" : "Not");
	if (panel->oplus_panel.pl_check_enable) {
		panel->oplus_panel.pl_check_flag = true;
	}

	ret = utils->read_u32(utils->data, "oplus,pcd-lvd-check-time-gap", &panel->oplus_panel.pl_check_time_gap);
	if (ret) {
		OPLUS_DSI_INFO("oplus,pcd-lvd-check-time-gap is not config, default 0\n");
	}

	panel->oplus_panel.timing_switch_frame_delay = utils->read_bool(utils->data,
			"oplus,panel-60hz-timing-switch-frame-delay");
	OPLUS_DSI_INFO("oplus,panel-60hz-timing-switch-frame-delay: %s\n",
			panel->oplus_panel.timing_switch_frame_delay ? "true" : "false");

	panel->oplus_panel.all_timing_switch_frame_delay = utils->read_bool(utils->data,
			"oplus,panel-all-timing-switch-frame-delay");
	OPLUS_DSI_INFO("oplus,panel-all-timing-switch-frame-delay: %s\n",
			panel->oplus_panel.all_timing_switch_frame_delay ? "true" : "false");

	return 0;
}

static int oplus_panel_parse_sequence_config(struct dsi_panel *panel)
{
	int ret = 0;
	struct dsi_parser_utils *utils = &panel->utils;

	panel->oplus_panel.enhance_mipi_strength = utils->read_bool(utils->data, "oplus,enhance_mipi_strength");
	oplus_enhance_mipi_strength = panel->oplus_panel.enhance_mipi_strength;
	OPLUS_DSI_INFO("lcm enhance_mipi_strength: %s\n", panel->oplus_panel.enhance_mipi_strength ? "true" : "false");

	ret = utils->read_u32(utils->data, "oplus,wait-te-config", &panel->oplus_panel.wait_te_config);
	if (ret) {
		OPLUS_DSI_INFO("failed to get panel parameter: oplus,wait-te-config\n");
		panel->oplus_panel.wait_te_config = 0;
	}

	panel->oplus_panel.oplus_bl_demura_dbv_support = utils->read_bool(utils->data,
			"oplus,bl_denura-dbv-switch-support");
	OPLUS_DSI_INFO("oplus,bl_denura-dbv-switch-support: %s\n",
		panel->oplus_panel.oplus_bl_demura_dbv_support ? "true" : "false");
	panel->oplus_panel.bl_demura_mode = 0;

	panel->oplus_panel.cmdq_sync_support = utils->read_bool(utils->data,
			"oplus,cmdq-sync-support");
	OPLUS_DSI_INFO("oplus,cmdq-sync-support: %s\n",
		panel->oplus_panel.cmdq_sync_support ? "true" : "false");
	panel->oplus_panel.cmdq_sync_count = 0;

	return 0;
}

static int oplus_panel_parse_apollo_config(struct dsi_panel *panel)
{
	int ret = 0;
	struct dsi_parser_utils *utils = &panel->utils;

	/* Add for apollo */
	panel->oplus_panel.is_apollo_support = utils->read_bool(utils->data, "oplus,apollo_backlight_enable");
	apollo_backlight_enable = panel->oplus_panel.is_apollo_support;
	OPLUS_DSI_INFO("apollo_backlight_enable: %s\n", panel->oplus_panel.is_apollo_support ? "true" : "false");

	if (panel->oplus_panel.is_apollo_support) {
		ret = utils->read_u32(utils->data, "oplus,apollo-sync-brightness-level",
				&panel->oplus_panel.sync_brightness_level);

		if (ret) {
			OPLUS_DSI_INFO("failed to get panel parameter: oplus,apollo-sync-brightness-level\n");
			/* Default sync brightness level is set to 200 */
			panel->oplus_panel.sync_brightness_level = 200;
		}
		panel->oplus_panel.dc_apollo_sync_enable = utils->read_bool(utils->data, "oplus,dc_apollo_sync_enable");
		if (panel->oplus_panel.dc_apollo_sync_enable) {
			ret = utils->read_u32(utils->data, "oplus,dc-apollo-backlight-sync-level",
					&panel->oplus_panel.dc_apollo_sync_brightness_level);
			if (ret) {
				OPLUS_DSI_INFO("failed to get panel parameter: oplus,dc-apollo-backlight-sync-level\n");
				panel->oplus_panel.dc_apollo_sync_brightness_level = 397;
			}
			ret = utils->read_u32(utils->data, "oplus,dc-apollo-backlight-sync-level-pcc-max",
					&panel->oplus_panel.dc_apollo_sync_brightness_level_pcc);
			if (ret) {
				OPLUS_DSI_INFO("failed to get panel parameter: oplus,dc-apollo-backlight-sync-level-pcc-max\n");
				panel->oplus_panel.dc_apollo_sync_brightness_level_pcc = 30000;
			}
			ret = utils->read_u32(utils->data, "oplus,dc-apollo-backlight-sync-level-pcc-min",
					&panel->oplus_panel.dc_apollo_sync_brightness_level_pcc_min);
			if (ret) {
				OPLUS_DSI_INFO("failed to get panel parameter: oplus,dc-apollo-backlight-sync-level-pcc-min\n");
				panel->oplus_panel.dc_apollo_sync_brightness_level_pcc_min = 29608;
			}
			OPLUS_DSI_INFO("dc apollo sync enable(%d,%d,%d)\n", panel->oplus_panel.dc_apollo_sync_brightness_level,
					panel->oplus_panel.dc_apollo_sync_brightness_level_pcc, panel->oplus_panel.dc_apollo_sync_brightness_level_pcc_min);
		}
	}

	return 0;
}

static int oplus_panel_parse_serial_number_info(struct dsi_panel *panel)
{
	struct dsi_parser_utils *utils = NULL;
	int ret = 0;

	if (!panel) {
		OPLUS_DSI_ERR("Oplus Features config No panel device\n");
		return -ENODEV;
	}
	utils = &panel->utils;

	panel->oplus_panel.serial_number.serial_number_support = utils->read_bool(utils->data,
			"oplus,dsi-serial-number-enabled");
	OPLUS_DSI_INFO("oplus,dsi-serial-number-enabled: %s\n", panel->oplus_panel.serial_number.serial_number_support ? "true" : "false");

	if (panel->oplus_panel.serial_number.serial_number_support) {
		ret = utils->read_u32(utils->data, "oplus,dsi-serial-number-reg",
				&panel->oplus_panel.serial_number.serial_number_reg);
		if (ret) {
			OPLUS_DSI_INFO("failed to get oplus,dsi-serial-number-reg\n");
			panel->oplus_panel.serial_number.serial_number_reg = 0xA1;
		}

		ret = utils->read_u32(utils->data, "oplus,dsi-serial-number-index",
				&panel->oplus_panel.serial_number.serial_number_index);
		if (ret) {
			OPLUS_DSI_INFO("failed to get oplus,dsi-serial-number-index\n");
			/* Default sync start index is set 5 */
			panel->oplus_panel.serial_number.serial_number_index = 7;
		}

		ret = utils->read_u32(utils->data, "oplus,dsi-serial-number-read-count",
				&panel->oplus_panel.serial_number.serial_number_conut);
		if (ret) {
			OPLUS_DSI_INFO("failed to get oplus,dsi-serial-number-read-count\n");
			/* Default  read conut 5 */
			panel->oplus_panel.serial_number.serial_number_conut = 5;
		}

		ret = utils->read_u32(utils->data, "oplus,dsi-serial-number-base-year",
				&panel->oplus_panel.serial_number.base_year);
		if (ret) {
			OPLUS_DSI_INFO("failed to oplus,dsi-serial-number-base-year\n");
			/* Default oplus,dsi-serial-number-base-year 0 */
			panel->oplus_panel.serial_number.base_year = 0;
		}

		panel->oplus_panel.serial_number.is_switch_page = utils->read_bool(utils->data,
			"oplus,dsi-serial-number-switch-page");
		OPLUS_DSI_INFO("oplus,dsi-serial-number-switch-page: %s", panel->oplus_panel.serial_number.is_switch_page ? "true" : "false");
	}

	return 0;
}

static int oplus_panel_parse_btb_sn_info(struct dsi_panel *panel)
{
	struct dsi_parser_utils *utils = NULL;
	int ret = 0;

	if (!panel) {
		OPLUS_DSI_ERR("Oplus Features config No panel device\n");
		return -ENODEV;
	}
	utils = &panel->utils;

	panel->oplus_panel.btb_sn.btb_sn_support = utils->read_bool(utils->data,
			"oplus,dsi-btb-sn-enabled");
	OPLUS_DSI_INFO("oplus,dsi-btb-sn-enabled: %s\n", panel->oplus_panel.btb_sn.btb_sn_support ? "true" : "false");

	if (panel->oplus_panel.btb_sn.btb_sn_support) {
		ret = utils->read_u32(utils->data, "oplus,dsi-btb-sn-reg",
				&panel->oplus_panel.btb_sn.btb_sn_reg);
		if (ret) {
			OPLUS_DSI_INFO("failed to get oplus,dsi-btb-sn-reg\n");
			panel->oplus_panel.btb_sn.btb_sn_reg = 0xA1;
		}

		ret = utils->read_u32(utils->data, "oplus,dsi-btb-sn-index",
				&panel->oplus_panel.btb_sn.btb_sn_index);
		if (ret) {
			OPLUS_DSI_INFO("failed to get oplus,dsi-btb-sn-index\n");
			/* Default sync start index is set 1 */
			panel->oplus_panel.btb_sn.btb_sn_index = 1;
		}

		ret = utils->read_u32(utils->data, "oplus,dsi-btb-sn-read-count",
				&panel->oplus_panel.btb_sn.btb_sn_conut);
		if (ret) {
			OPLUS_DSI_INFO("failed to get oplus,dsi-btb-sn-read-count\n");
			/* Default  read conut 32 */
			panel->oplus_panel.btb_sn.btb_sn_conut = 32;
		}

		panel->oplus_panel.btb_sn.is_switch_page = utils->read_bool(utils->data,
			"oplus,dsi-btb-sn-switch-page");
		OPLUS_DSI_INFO("oplus,dsi-btb-sn-switch-page: %s", panel->oplus_panel.btb_sn.is_switch_page ? "true" : "false");
	}

	return 0;
}

int oplus_panel_parse_features_config(struct dsi_panel *panel)
{
	struct dsi_parser_utils *utils = NULL;
	if (!panel) {
		OPLUS_DSI_ERR("Oplus Features config No panel device\n");
		return -ENODEV;
	}

	utils = &panel->utils;
	panel->oplus_panel.dp_support = utils->get_property(utils->data,
			"oplus,dp-enabled", NULL);

	if (!panel->oplus_panel.dp_support) {
		OPLUS_DSI_INFO("Failed to found panel dp support, using null dp config\n");
		panel->oplus_panel.dp_support = false;
	}

	panel->oplus_panel.cabc_enabled = utils->read_bool(utils->data,
			"oplus,dsi-cabc-enabled");
	OPLUS_DSI_INFO("oplus,dsi-cabc-enabled: %s\n", panel->oplus_panel.cabc_enabled ? "true" : "false");

	panel->oplus_panel.dre_enabled = utils->read_bool(utils->data,
			"oplus,dsi-dre-enabled");
	OPLUS_DSI_INFO("oplus,dsi-dre-enabled: %s\n", panel->oplus_panel.dre_enabled ? "true" : "false");

	panel->oplus_panel.panel_init_compatibility_enable = utils->read_bool(utils->data,
			"oplus,panel_init_compatibility_enable");
	OPLUS_DSI_INFO("oplus,panel_init_compatibility_enable: %s\n",
			panel->oplus_panel.panel_init_compatibility_enable ? "true" : "false");

	panel->oplus_panel.vid_timming_switch_enabled = utils->read_bool(utils->data,
			"oplus,dsi-vid-timming-switch_enable");
	OPLUS_DSI_INFO("oplus,panel_init_compatibility_enable: %s\n",
			panel->oplus_panel.vid_timming_switch_enabled ? "true" : "false");

	panel->oplus_panel.change_voltage_before_panel_bl_0 = utils->read_bool(utils->data,
			"oplus,change-voltage-before-panel-bl-0-enable");
	OPLUS_DSI_INFO("oplus,change-voltage-before-panel-bl-0-enable: %s\n",
			panel->oplus_panel.change_voltage_before_panel_bl_0 ? "true" : "false");

	panel->oplus_panel.interval_time_nolp_pre = utils->read_bool(utils->data,
			"oplus,interval-time-nolp-pre");
	OPLUS_DSI_INFO("oplus,interval-time-nolp-pre: %s\n",
			panel->oplus_panel.interval_time_nolp_pre ? "true" : "false");

	panel->oplus_panel.bl_ic_ktz8869_used = utils->read_bool(utils->data,
		"oplus,bl-use-ktz8869-ic-ctrl");
	OPLUS_DSI_INFO("oplus,bl-use-ktz8869-ic-ctrl: %s\n",
		panel->oplus_panel.bl_ic_ktz8869_used ? "true" : "false");
	ktz8869_use = panel->oplus_panel.bl_ic_ktz8869_used;

	panel->oplus_panel.white_point_compensation_enabled = utils->read_bool(utils->data,
			"oplus,dsi-white-point-compensation-enabled");
	OPLUS_DSI_INFO("oplus,dsi-white-point-compensation-enabled: %s\n", panel->oplus_panel.white_point_compensation_enabled ? "true" : "false");

	panel->oplus_panel.custom_reset = utils->read_bool(utils->data,
		"oplus,reset_custom");
	OPLUS_DSI_INFO("oplus,reset_custom: %s\n",
		panel->oplus_panel.custom_reset ? "true" : "false");

	panel->oplus_panel.ltpo_low_pwm_full_screen_aod_enable = utils->read_bool(utils->data,
                "oplus,ofp-ltpo-low-pwm-full-screen-aod-enable");
        OPLUS_DSI_INFO("oplus,ofp-ltpo-low-pwm-full-screen-aod-enable: %s\n",
                panel->oplus_panel.ltpo_low_pwm_full_screen_aod_enable ? "true" : "false");

	panel->oplus_panel.interval_time_fps_to_esd_flag = utils->read_bool(utils->data,
			"oplus,interval-time-switch-fps-to-esd");
	OPLUS_DSI_INFO("oplus,interval-time-switch-fps-to-esd: %s\n",
			panel->oplus_panel.interval_time_fps_to_esd_flag ? "true" : "false");

	panel->oplus_panel.timing_switch_compatible = utils->read_bool(utils->data,
			"oplus,timing_switch_compatible");
	OPLUS_DSI_INFO("oplus,timing_switch_compatible: %s\n", panel->oplus_panel.timing_switch_compatible ? "true" : "false");

	panel->oplus_panel.lgd_support = utils->read_bool(utils->data,
			"oplus,panel-lgd-support");
	OPLUS_DSI_INFO("oplus,panel-lgd-support: %s\n", panel->oplus_panel.lgd_support ? "true" : "false");

	return 0;
}

int oplus_panel_parse_vsync_config(
				struct dsi_display_mode *mode,
				struct dsi_parser_utils *utils)
{
	int rc;
	struct dsi_display_mode_priv_info *priv_info;

	priv_info = mode->priv_info;

	rc = utils->read_u32(utils->data, "oplus,apollo-panel-vsync-period",
				  &priv_info->oplus_priv_info.vsync_period);
	if (rc) {
		OPLUS_DSI_DEBUG("panel prefill lines are not defined rc=%d\n", rc);
		priv_info->oplus_priv_info.vsync_period = 1000000 / mode->timing.refresh_rate;
	}

	rc = utils->read_u32(utils->data, "oplus,apollo-panel-vsync-width",
				  &priv_info->oplus_priv_info.vsync_width);
	if (rc) {
		OPLUS_DSI_DEBUG("panel vsync width not defined rc=%d\n", rc);
		priv_info->oplus_priv_info.vsync_width = priv_info->oplus_priv_info.vsync_period >> 1;
	}

	rc = utils->read_u32(utils->data, "oplus,apollo-panel-async-bl-delay",
				  &priv_info->oplus_priv_info.async_bl_delay);
	if (rc) {
		OPLUS_DSI_DEBUG("panel async backlight delay to bottom of frame was disabled rc=%d\n", rc);
		priv_info->oplus_priv_info.async_bl_delay = 0;
	} else {
		if (priv_info->oplus_priv_info.async_bl_delay >= priv_info->oplus_priv_info.vsync_period) {
			OPLUS_DSI_ERR("async backlight delay value was out of vsync period\n");
			priv_info->oplus_priv_info.async_bl_delay = priv_info->oplus_priv_info.vsync_width;
		}
	}

	rc = utils->read_u32(utils->data, "oplus,panel-pwm-switch-frame-delay",
				  &priv_info->oplus_priv_info.pwm_switch_frame_delay);
	if (rc) {
		OPLUS_DSI_DEBUG("pwm_switch cmd being sent in the second half of the frame is disabled rc=%d\n", rc);
		priv_info->oplus_priv_info.pwm_switch_frame_delay = 0;
	} else {
		if(priv_info->oplus_priv_info.pwm_switch_frame_delay >= priv_info->oplus_priv_info.vsync_period) {
			OPLUS_DSI_ERR("pwm_switch_frame_delay value was out of vsync period\n");
			priv_info->oplus_priv_info.pwm_switch_frame_delay = priv_info->oplus_priv_info.vsync_width;
		}
	}

	priv_info->oplus_priv_info.refresh_rate = mode->timing.refresh_rate;

	OPLUS_DSI_INFO("vsync width = %d, vsync period = %d, refresh rate = %d\n", \
			priv_info->oplus_priv_info.vsync_width, priv_info->oplus_priv_info.vsync_period, priv_info->oplus_priv_info.refresh_rate);

	return 0;
}

#ifdef OPLUS_FEATURE_AP_UIR_DIMMING
void oplus_panel_parse_apuir_ds_list(struct dsi_panel *panel) {
	int rc = 0;
	struct dsi_parser_utils *utils = &panel->utils;
	char payload[128] = "";
	u32 cnt = 0;
	int upnit_ds_count = 0;
	u32 *upnit_ds_list = NULL;
	int lessnit_ds_count = 0;
	u32 *lessnit_ds_list = NULL;
	u32 ic_type = 0;
	int upnit_index_count = 0;
	u32 *upnit_index_list = NULL;
	int lessnit_index_count = 0;
	u32 *lessnit_index_list = NULL;
	int modeset_count = 0;
	u32 *modeset_list = NULL;
	int apl_lhset_count = 0;
	u32 *apl_lhset_list = NULL;
	int girseedtype = 0;

	/* get ic type */
	rc = utils->read_u32(utils->data, "oplus,apuir-ic-type", &ic_type);
	if (rc) {
		OPLUS_DSI_INFO("apuir oplus,apuir-ic-type not specified, defaulting to 0 (disabled)\n");
		ic_type = 0;
	} else {
		OPLUS_DSI_INFO("apuir ic_type: %u (1=ILI, 2=NVT)\n", ic_type);
	}
	oplus_set_apuir_ictype(ic_type);

	if (of_property_read_bool(utils->data, "oplus,apuir-sdc-band")){
		oplus_set_apuir_sdc_band(true);
		OPLUS_DSI_INFO("apuir sdc panel\n");
	} else {
		oplus_set_apuir_sdc_band(false);
		OPLUS_DSI_INFO("apuir nvt panel\n");
	}

	if (of_property_read_bool(utils->data, "oplus,apuir-gamma-icds")){
		oplus_set_apuir_gamma_icds(true);
		OPLUS_DSI_INFO("apuir ic the relationship between ds and ratio is gamma-based, not linear.\n");
	} else {
		oplus_set_apuir_gamma_icds(false);
		OPLUS_DSI_INFO("apuir ic the relationship between ds and ratio is linear, not gamma-based.\n");
	}

	if (utils->read_u32(utils->data, "oplus,apuir-gir-seedtype", &girseedtype)){
		oplus_set_apuir_gir_seedtype(0);
		OPLUS_DSI_INFO("apuir set default gir_seedtype %d\n", girseedtype);
	} else {
		oplus_set_apuir_gir_seedtype(girseedtype);
		OPLUS_DSI_INFO("apuir set gir_seedtype %d\n", girseedtype);
	}

	/* check if use uir loading params */
	if (of_property_read_bool(utils->data, "oplus,apuir-param-from-uirloading")) {
		oplus_set_apuir_param_from_uirloading(true);
		OPLUS_DSI_INFO("apuir using param from uir loading\n");
	} else {
		oplus_set_apuir_param_from_uirloading(false);
		OPLUS_DSI_INFO("apuir using standard loading param\n");
	}

	/* get upnit_ds_list */
	upnit_ds_count = utils->count_u32_elems(utils->data,
		"oplus,apuir-upnit-ds-list");

	/* Backward compatibility: try old property name if new one not found */
	if (upnit_ds_count < 1) {
		upnit_ds_count = utils->count_u32_elems(utils->data,
			"oplus,apuir-up800nit-ds-list");
		if (upnit_ds_count > 0) {
			OPLUS_DSI_INFO("apuir using legacy property name: oplus,apuir-up800nit-ds-list\n");
		}
	}

	if (upnit_ds_count < 1) {
		OPLUS_DSI_INFO("apuir upnit-ds-list is NULL! oplus_apuir_setenable 0\n");
		upnit_ds_count = 0;
		oplus_apuir_setenable(0);
		goto cleanup;
	} else {
		oplus_apuir_setenable(1);
		if (ic_type == 0) {
			ic_type = 2;
		}
		oplus_set_apuir_ictype(ic_type);
	}

	upnit_ds_list = kcalloc(upnit_ds_count,
			sizeof(u32), GFP_KERNEL);
	if (!upnit_ds_list) {
		OPLUS_DSI_ERR("apuir oplus,apuir-upnit-ds-list alloc failed!\n");
		goto cleanup;
	}

	/* Try new property name first, then fall back to old one */
	rc = utils->read_u32_array(utils->data,
		"oplus,apuir-upnit-ds-list",
		upnit_ds_list,
		upnit_ds_count);

	if (rc) {
		rc = utils->read_u32_array(utils->data,
			"oplus,apuir-up800nit-ds-list",
			upnit_ds_list,
			upnit_ds_count);
	}

	if (rc) {
		OPLUS_DSI_ERR("apuir upnit_ds_list parse failed!\n");
		goto cleanup;
	}
	oplus_apuir_set_upnit_ds_list(upnit_ds_count, upnit_ds_list);

	cnt = 0;
	for (int i = 0; i < upnit_ds_count; i++) {
		cnt += scnprintf(payload + cnt, sizeof(payload) - cnt, "[%u]", upnit_ds_list[i]);
	}
	OPLUS_DSI_INFO("apuir upnit_ds_list count: %d, mode_list: %s\n", upnit_ds_count, payload);

	/* get lessnit_ds_list */
	lessnit_ds_count = utils->count_u32_elems(utils->data,
		"oplus,apuir-lessnit-ds-list");

	/* Backward compatibility: try old property name if new one not found */
	if (lessnit_ds_count < 1) {
		lessnit_ds_count = utils->count_u32_elems(utils->data,
			"oplus,apuir-less800nit-ds-list");
		if (lessnit_ds_count > 0) {
			OPLUS_DSI_INFO("apuir using legacy property name: oplus,apuir-less800nit-ds-list\n");
		}
	}

	if (lessnit_ds_count < 1) {
		OPLUS_DSI_INFO("apuir oplus,apuir-lessnit-ds-list is NULL!\n");
		lessnit_ds_count = 0;
		goto cleanup;
	}

	lessnit_ds_list = kcalloc(lessnit_ds_count,
			sizeof(u32), GFP_KERNEL);
	if (!lessnit_ds_list) {
		OPLUS_DSI_ERR("apuir oplus,apuir-lessnit-ds-list alloc failed!\n");
		goto cleanup;
	}

	/* Try new property name first, then fall back to old one */
	rc = utils->read_u32_array(utils->data,
			"oplus,apuir-lessnit-ds-list",
			lessnit_ds_list,
			lessnit_ds_count);

	if (rc) {
		rc = utils->read_u32_array(utils->data,
			"oplus,apuir-less800nit-ds-list",
			lessnit_ds_list,
			lessnit_ds_count);
	}

	if (rc) {
		OPLUS_DSI_ERR("apuir lessnit_ds_list parse failed!\n");
		goto cleanup;
	}
	oplus_apuir_set_lessnit_ds_list(lessnit_ds_count, lessnit_ds_list);

	cnt = 0;
	for (int i = 0; i < lessnit_ds_count; i++) {
		cnt += scnprintf(payload + cnt, sizeof(payload) - cnt, "[%u]", lessnit_ds_list[i]);
	}
	OPLUS_DSI_INFO("apuir lessnit_ds_list count: %d, mode_list: %s\n", lessnit_ds_count, payload);

	/* Parse IC-specific configurations */
	if (ic_type == 2) {
		/* NVT specific configurations */
		/* get upnit_index_list */
		upnit_index_count = utils->count_u32_elems(utils->data, "oplus,apuir-upnit-index-list");
		if (upnit_index_count > 0) {
			upnit_index_list = kcalloc(upnit_index_count, sizeof(u32), GFP_KERNEL);
			if (!upnit_index_list) {
				OPLUS_DSI_ERR("apuir oplus,apuir-upnit-index-list alloc failed!\n");
				goto cleanup;
			}

			rc = utils->read_u32_array(utils->data, "oplus,apuir-upnit-index-list", upnit_index_list, upnit_index_count);
			if (rc) {
				OPLUS_DSI_ERR("apuir upnit_index_count parse failed!\n");
				goto cleanup;
			}
			apuir_set_nvt_upnit_index_list(upnit_index_count, upnit_index_list);
			kfree(upnit_index_list);
			upnit_index_list = NULL;
		}

		/* get lessnit_index_list */
		lessnit_index_count = utils->count_u32_elems(utils->data, "oplus,apuir-lessnit-index-list");
		if (lessnit_index_count > 0) {
			lessnit_index_list = kcalloc(lessnit_index_count, sizeof(u32), GFP_KERNEL);
			if (!lessnit_index_list) {
				OPLUS_DSI_ERR("apuir oplus,apuir-lessnit-index-list alloc failed!\n");
				goto cleanup;
			}

			rc = utils->read_u32_array(utils->data, "oplus,apuir-lessnit-index-list", lessnit_index_list, lessnit_index_count);
			if (rc) {
				OPLUS_DSI_ERR("apuir lessnit_index_count parse failed!\n");
				goto cleanup;
			}
			apuir_set_nvt_lessnit_index_list(lessnit_index_count, lessnit_index_list);
			kfree(lessnit_index_list);
			lessnit_index_list = NULL;
		}

		/* get modeset_list */
		modeset_count = utils->count_u32_elems(utils->data, "oplus,apuir-modeset-list");
		if (modeset_count > 0) {
			modeset_list = kcalloc(modeset_count, sizeof(u32), GFP_KERNEL);
			if (!modeset_list) {
				OPLUS_DSI_ERR("apuir oplus,apuir-modeset-list alloc failed!\n");
				goto cleanup;
			}

			rc = utils->read_u32_array(utils->data, "oplus,apuir-modeset-list", modeset_list, modeset_count);
			if (rc) {
				OPLUS_DSI_ERR("apuir modeset_count parse failed!\n");
				goto cleanup;
			}
			apuir_set_nvt_modeset_list(modeset_count, modeset_list);
			kfree(modeset_list);
			modeset_list = NULL;
		}

		/* get apl_lhset_list */
		apl_lhset_count = utils->count_u32_elems(utils->data, "oplus,apuir-apl_lhset-list");
		if (apl_lhset_count > 0) {
			apl_lhset_list = kcalloc(apl_lhset_count, sizeof(u32), GFP_KERNEL);
			if (!apl_lhset_list) {
				OPLUS_DSI_ERR("apuir oplus,apuir-apl_lhset-list alloc failed!\n");
				goto cleanup;
			}

			rc = utils->read_u32_array(utils->data, "oplus,apuir-apl_lhset-list", apl_lhset_list, apl_lhset_count);
			if (rc) {
				OPLUS_DSI_ERR("apuir apl_lhset_count parse failed!\n");
				goto cleanup;
			}
			apuir_set_nvt_apl_lhset_list(apl_lhset_count, apl_lhset_list);
			kfree(apl_lhset_list);
			apl_lhset_list = NULL;
		}
	}

cleanup:
	if (upnit_ds_list)
		kfree(upnit_ds_list);
	if (lessnit_ds_list)
		kfree(lessnit_ds_list);
	if (upnit_index_list)
		kfree(upnit_index_list);
	if (lessnit_index_list)
		kfree(lessnit_index_list);
	if (modeset_list)
		kfree(modeset_list);
	if (apl_lhset_list)
		kfree(apl_lhset_list);
}
#endif

int oplus_panel_parse_config(struct dsi_panel *panel)
{
	if (!panel) {
		OPLUS_DSI_ERR("Oplus features config no panel device\n");
		return -ENODEV;
	}

	/* parse common config */
	oplus_panel_parse_common_config(panel);

	/* parse feature config */
	oplus_panel_parse_power_config(panel);
	oplus_panel_parse_sequence_config(panel);
	oplus_panel_parse_apollo_config(panel);
	oplus_panel_parse_ffc_config(panel);
	oplus_panel_parse_pwm_config(panel);
	oplus_panel_parse_serial_number_info(panel);
	oplus_panel_parse_btb_sn_info(panel);
	oplus_panel_parse_power_sequence_config(panel);
	oplus_dsi_panel_parse_mipi_err(panel);
	oplus_dsi_panel_parse_pcd(panel);
	oplus_dsi_panel_parse_lvd(panel);
	oplus_dsi_panel_parse_lut(panel);
	oplus_panel_dynamic_float_te_config(panel);
#ifdef OPLUS_FEATURE_AP_UIR_DIMMING
	oplus_panel_parse_apuir_ds_list(panel);
#endif

	return 0;
}
