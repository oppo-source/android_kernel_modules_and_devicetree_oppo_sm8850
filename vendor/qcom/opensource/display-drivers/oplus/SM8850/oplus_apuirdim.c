/***************************************************************
** Copyright (C), 2025, OPLUS Mobile Comm Corp., Ltd
** File : oplus_apuirdim.c
** Description : oplus_apuirdim source
** Version : 2.0
** Date : 2025/06/15
** Author : Display
***************************************************************/
#include "oplus_apuirdim.h"
#include <linux/pinctrl/consumer.h>
#include "oplus_adfr.h"
#include "oplus_display_device_ioctl.h"
#include "oplus_display_utils.h"
#include "oplus_display_pwm.h"
#include "oplus_display_ext.h"
#include "dsi_display.h"
#include "sde_trace.h"
#include "sde_encoder_phys.h"
#include "oplus_display_panel_cmd.h"
#include "oplus_display_effect.h"
#include <uapi/linux/sched/types.h>
enum oplus_apuir_log_level {
	OPLUS_APUIR_LOG_LEVEL_NONE = 0,
	OPLUS_APUIR_LOG_LEVEL_ERR = 1,
	OPLUS_APUIR_LOG_LEVEL_WARN = 2,
	OPLUS_APUIR_LOG_LEVEL_INFO = 3,
	OPLUS_APUIR_LOG_LEVEL_DEBUG = 4,
};
/* for nvt common defines */
#define APUIR_DS_READ_LENGTH 75
#define APUIR_DS_READ_LENGTH_NVT 75
#define APUIR_DS_READ_ROW_NVT 1
#define APUIR_OFF_FRAME_COUNT 2
#define APUIR_OFF_FRAME_COUNT_NVT 2
#define APUIR_DS_READ_ROW 1

int m_ds_read_length = APUIR_DS_READ_LENGTH_NVT;

u8 apuirregs_loading[APUIR_DS_READ_LENGTH] = {0};
u8 apuirregs_loading_mode1[APUIR_DS_READ_LENGTH] = {0};
u8 apuirregs_loading_mode2[APUIR_DS_READ_LENGTH] = {0};
u8 apuirregs_loading_mode3[APUIR_DS_READ_LENGTH] = {0};
/* for nvt end */

unsigned int oplus_apuir_log_level = OPLUS_APUIR_LOG_LEVEL_INFO;
EXPORT_SYMBOL(oplus_apuir_log_level);
unsigned int oplus_apuir_display_id = 0;
EXPORT_SYMBOL(oplus_apuir_display_id);
uint64_t m_apuirdim_ds = 0;
bool m_apuirdim_ds_update = false;
struct workqueue_struct *apuir_setcmd_wq;
static struct work_struct apuir_setcmd_work;
static bool apuir_work_inited;
static enum dsi_cmd_set_type mAPuirType = DSI_CMD_APUIR_ON;
int off_framecount = 0;
static int first_loading = 0;
/*nvt setting*/
int nvt_upnit_index_count = 4;
u32 m_nvt_upnit_index_list[4] = {40, 48, 47, 49};
int nvt_lessnit_index_count = 4;
u32 m_nvt_lessnit_index_list[4] = {37, 40, 39, 41};
int nvt_modeset_count = 3;
u32 m_nvt_modeset_list[3] = {73, 0x11, 0x17};
int nvt_apl_lhset_count = 6;
u32 m_nvt_apl_lhset_list[6] = {60, 61, 0xFF, 0xFF, 0x3F, 0x33};
/*common setting*/
u32 m_ds1x0_real = 0, m_ds1x3_real = 0, m_ds2x0_real = 0;
u32 m_ds21x0_real = 0, m_ds21x3_real = 0;
bool is_nvt_ic = false;
int apuir_enable = 0;
u32 aplratio = 0, oprratio = 0;
bool m_ret = false;
u32 m_last_dim = 0;
u32 m_last_aplds = 0;
u32 aplds_before = 0;
enum dsi_cmd_set_type m_last_type = DSI_CMD_APUIR_OFF;
bool apuir_param_from_uirloading = false;
bool apuir_gamma_icds = false;
bool apuir_sdc_band = false;
int apuir_girseedtype = 0;

void oplus_set_apuir_param_from_uirloading(bool enabled)
{
	apuir_param_from_uirloading = enabled;
	APUIR_INFO("oplus_set_apuir_param_from_uirloading enabled=%d\n", enabled);
}

void oplus_set_apuir_gamma_icds(bool enabled)
{
	apuir_gamma_icds = enabled;
	APUIR_INFO("oplus_set_apuir_gamma_icds enabled=%d\n", enabled);
}

void oplus_set_apuir_sdc_band(bool enabled)
{
	apuir_sdc_band = enabled;
	if (apuir_sdc_band) {
		m_nvt_upnit_index_list[0] = 48;
		m_nvt_upnit_index_list[1] = 49;
		m_nvt_upnit_index_list[2] = 50;
		m_nvt_upnit_index_list[3] = 0;
		m_nvt_lessnit_index_list[0] = 40;
		m_nvt_lessnit_index_list[1] = 41;
		m_nvt_lessnit_index_list[2] = 47;
		m_nvt_lessnit_index_list[3] = 0;
		APUIR_INFO("oplus_set_apuir_sdc_band m_nvt_upnit_index_list %d %d %d %d\n",
			m_nvt_upnit_index_list[0], m_nvt_upnit_index_list[1],
			m_nvt_upnit_index_list[2], m_nvt_upnit_index_list[3]);
		APUIR_INFO("oplus_set_apuir_sdc_band m_nvt_lessnit_index_list %d %d %d %d\n",
				m_nvt_lessnit_index_list[0], m_nvt_lessnit_index_list[1],
				m_nvt_lessnit_index_list[2], m_nvt_lessnit_index_list[3]);
	}
	APUIR_INFO("oplus_set_apuir_sdc_band enabled=%d\n", enabled);
}

void oplus_set_apuir_ictype(int ictype)
{
	APUIR_INFO("oplus_set_apuir_ictype ictype=%d\n", ictype);
	switch (ictype) {
	case 2:
		is_nvt_ic = true;
		apuir_enable = 1;
		break;
	default:
		is_nvt_ic = false;
		apuir_enable = 0;
		break;
	}
}

void oplus_apuir_setenable(int enable)
{
	apuir_enable = enable;
	APUIR_INFO("apuir_enable:%d\n", apuir_enable);
}

int oplus_get_apuir_enable(void)
{
	return apuir_enable;
}

void oplus_set_apuir_gir_seedtype(int seedtype)
{
	apuir_girseedtype = seedtype;
}

void oplus_apuir_init(void *dsi_panel)
{
	struct dsi_panel *panel = dsi_panel;
	if (!panel) {
		APUIR_ERR("Invalid params\n");
		return;
	}
	if (!strcmp(panel->type, "primary")) {
		apuir_setcmd_wq = create_singlethread_workqueue("apuir_setcmd0");
	} else if (!strcmp(panel->type, "secondary")) {
		apuir_setcmd_wq = create_singlethread_workqueue("apuir_setcmd1");
	}
	INIT_WORK(&apuir_setcmd_work, oplus_apuir_setcmd_work_handler);
	apuir_work_inited = true;
}

static void oplus_apuir_ensure_workqueue(struct dsi_panel *panel)
{
	const char *type = (panel && panel->type) ? panel->type : "unknown";

	if (!apuir_work_inited) {
		INIT_WORK(&apuir_setcmd_work, oplus_apuir_setcmd_work_handler);
		apuir_work_inited = true;
	}

	if (!apuir_setcmd_wq) {
		if (!strcmp(type, "primary"))
			apuir_setcmd_wq = create_singlethread_workqueue("apuir_setcmd0");
		else if (!strcmp(type, "secondary"))
			apuir_setcmd_wq = create_singlethread_workqueue("apuir_setcmd1");
		else
			apuir_setcmd_wq = create_singlethread_workqueue("apuir_setcmd");
	}
}
/* -------------------- aod -------------------- */
void oplus_apuir_setcmd_work_handler(struct work_struct *work_item)
{
	int rc = 0;
	struct dsi_display *display = oplus_display_get_current_display();
	struct dsi_panel *panel = NULL;

	APUIR_DEBUG("start\n");

	if (!display || !display->panel || !display->panel->cur_mode) {
		APUIR_ERR("invalid display or panel params\n");
		return;
	}

	panel = display->panel;
	SDE_ATRACE_BEGIN("oplus_apuir_setcmd_work_handler");
	mutex_lock(&panel->panel_lock);
	SDE_ATRACE_BEGIN("cmdset");
	rc = dsi_panel_tx_cmd_set(display->panel, mAPuirType, false);
	SDE_ATRACE_END("cmdset");
	if (rc) {
		APUIR_ERR("[%s] failed to send DSI_CMD_POST_ON_BACKLIGHT cmd, rc=%d\n", display->name, rc);
	}
	mutex_unlock(&panel->panel_lock);
	SDE_ATRACE_END("oplus_apuir_setcmd_work_handler");

	APUIR_DEBUG("end\n");

	return;
}

bool oplus_apuir_get_uir_state(void)
{
	uint32_t aplds = m_apuirdim_ds & 0xfff;
	if ((m_apuirdim_ds != 0 && aplds == 0
		&& (off_framecount == APUIR_OFF_FRAME_COUNT))
		|| m_apuirdim_ds == 0) {
			APUIR_INFO("oplus_apuir_get_uir_state return false aplds %d m_apuirdim_ds = 0x%016llx off_framecount = %d\n",
				aplds, (unsigned long long)m_apuirdim_ds, off_framecount);
		return false;
	} else {
		APUIR_INFO("oplus_apuir_get_uir_state return true aplds %d m_apuirdim_ds = 0x%016llx off_framecount = %d\n",
			aplds, (unsigned long long)m_apuirdim_ds, off_framecount);
		return true;
	}
}

int oplus_apuir_set_ds(void *sde_enc_v)
{
	struct sde_encoder_virt *sde_enc = (struct sde_encoder_virt *)sde_enc_v;
	struct sde_encoder_phys *phys = NULL;
	struct sde_connector *c_conn = NULL;
	struct dsi_display *display = NULL;
	u64 propval;

	APUIR_DEBUG("start\n");
	if (!oplus_get_apuir_enable()) {
		return 0;
	}

	if (!sde_enc) {
		APUIR_ERR("invalid sde_encoder_virt parameters\n");
		return 0;
	}

	phys = sde_enc->phys_encs[0];
	if (!phys || !phys->connector) {
		APUIR_ERR("invalid sde_encoder_phys parameters\n");
		return 0;
	}

	c_conn = to_sde_connector(phys->connector);
	if (!c_conn) {
		APUIR_ERR("invalid sde_connector parameters\n");
		return 0;
	}

	if (c_conn->connector_type != DRM_MODE_CONNECTOR_DSI)
		return 0;

	display = c_conn->display;
	if (!display || !display->panel || !display->panel->cur_mode) {
		APUIR_ERR("invalid display or panel params\n");
		return -EINVAL;
	}
	propval = sde_connector_get_property(c_conn->base.state, CONNECTOR_PROP_UIR_DS);
	// APUIR_INFO("apuirdriver:u64 propval change:0x%016llx %llu \n", (unsigned long long)propval, (unsigned long long)propval);
	if ((m_apuirdim_ds != propval || (off_framecount > 0 && off_framecount < APUIR_OFF_FRAME_COUNT))) {
		m_apuirdim_ds = propval;
		APUIR_INFO("apuirdriver:m_apuirdim_ds:0x%016llx %llu\n",
			(unsigned long long)m_apuirdim_ds,
			(unsigned long long)m_apuirdim_ds);
		m_apuirdim_ds_update = true;
		if (m_apuirdim_ds != 0) {
			oplus_apuir_set_cmd(display, m_apuirdim_ds);
		}
	}
	return 0;
}

/*for nvt start*/
static void exchangeregs_nvt(int ds, u8* apuirregs, int index0, int index1, int index2, int index3)
{
	int temp1 = 0, temp2 = 1, temp3 = 0, temp4 = 0;
	int dsh = 0, dsl = 0;
	if (!apuirregs) {
		APUIR_ERR("apuirregs is NULL\n");
		return;
	}

	if (index0 < 0 || index0 >= APUIR_DS_READ_LENGTH ||
		index1 < 0 || index1 >= APUIR_DS_READ_LENGTH ||
		index2 < 0 || index2 >= APUIR_DS_READ_LENGTH ||
		index3 < 0 || index3 >= APUIR_DS_READ_LENGTH) {
		APUIR_ERR("Invalid index: %d, %d, %d, %d (max: %d)\n",
			 index0, index1, index2, index3, APUIR_DS_READ_LENGTH - 1);
		return;
	}


	dsh = (ds >> 8) & 0xFF;
	dsl = ds & 0xFF;

	temp1 = apuirregs[index0];
	temp2 = apuirregs[index1];
	temp3 = apuirregs[index2];
	temp4 = apuirregs[index3];

	temp1 = (temp1 & 0x0F) | (dsh << 4); /*BAND8/10[11:8]*/
	temp2 = (temp2 & 0xF0) | (dsh & 0x0F); /*BAND9/11[11:8]*/
	temp3 = dsl; /*BAND8/10[7:0]*/
	temp4 = dsl; /*BAND9/11[7:0]*/

	apuirregs[index0] = temp1;
	apuirregs[index1] = temp2;
	apuirregs[index2] = temp3;
	apuirregs[index3] = temp4;
	APUIR_DEBUG("exchangeregs_nvt apuirregs[%d] = 0x%x, apuirregs[%d] = 0x%x, apuirregs[%d] = 0x%x, apuirregs[%d] = 0x%x\n",
		index0, apuirregs[index0], index1, apuirregs[index1], index2, apuirregs[index2], index3, apuirregs[index3]);
}

static void exchangeregs_band4094_nvt(int ds, u8* apuirregs, int index1, int index3)
{
	int temp2 = 1, temp4 = 0;
	int dsh = 0, dsl = 0;
	if (!apuirregs) {
		APUIR_ERR("apuirregs is NULL\n");
		return;
	}

	if (index1 < 0 || index1 >= APUIR_DS_READ_LENGTH ||
		index3 < 0 || index3 >= APUIR_DS_READ_LENGTH) {
		APUIR_ERR("Invalid index: %d, %d(max: %d)\n",
			 index1, index3, APUIR_DS_READ_LENGTH - 1);
		return;
	}

	dsh = (ds >> 8) & 0xFF;
	dsl = ds & 0xFF;

	temp2 = apuirregs[index1];
	temp4 = apuirregs[index3];

	temp2 = (temp2 & 0xF0) | (dsh & 0x0F); /*BAND11[11:8]*/
	temp4 = dsl; /*BAND11[7:0]*/

	apuirregs[index1] = temp2;
	apuirregs[index3] = temp4;
	APUIR_DEBUG("exchangeregs_band4094_nvt apuirregs[%d] = 0x%x, apuirregs[%d] = 0x%x\n",
		index1, apuirregs[index1], index3, apuirregs[index3]);
}

/*Enter the ds value corresponding to the brightening ratio into the BAND around 3515 to make it effective.*/
static void exchangeregs_sdc(int ds, u8* apuirregs, int index0, int index1, int index2)
{
	int temp1 = 0, temp2 = 0, temp3 = 0;
	int dsh = 0, dsl = 0;

	if (!apuirregs) {
		APUIR_ERR("apuirregs is NULL\n");
		return;
	}

	if (index0 < 0 || index0 >= APUIR_DS_READ_LENGTH ||
		index1 < 0 || index1 >= APUIR_DS_READ_LENGTH ||
		index2 < 0 || index2 >= APUIR_DS_READ_LENGTH) {
		APUIR_ERR("Invalid index: %d, %d, %d (max: %d)\n",
			 index0, index1, index2, APUIR_DS_READ_LENGTH - 1);
		return;
	}

	dsh = (ds >> 8) & 0xFF;
	dsl = ds & 0xFF;

	temp1 = apuirregs[index0];
	temp2 = apuirregs[index1];
	temp3 = apuirregs[index2];

	temp1 = (dsh << 4) | (dsh & 0x0F); /*BAND10/12[11:8] | BAND9/11[11:8]*/
	temp2 = dsl; /*BAND9/11[7:0]*/
	temp3 = dsl; /*BAND10/12[7:0]*/

	apuirregs[index0] = temp1;
	apuirregs[index1] = temp2;
	apuirregs[index2] = temp3;
	APUIR_DEBUG("exchangeregs_sdc apuirregs[%d] = 0x%x, apuirregs[%d] = 0x%x, apuirregs[%d] = 0x%x\n",
		index0, apuirregs[index0], index1, apuirregs[index1], index2, apuirregs[index2]);
}

static void exchangeregss_band4094_sdc(int ds, u8* apuirregs, int index0, int index2)
{
	int temp1 = 0, temp3 = 0;
	int dsh = 0, dsl = 0;

	if (!apuirregs) {
		APUIR_ERR("apuirregs is NULL\n");
		return;
	}

	if (index0 < 0 || index0 >= APUIR_DS_READ_LENGTH ||
		index2 < 0 || index2 >= APUIR_DS_READ_LENGTH) {
		APUIR_ERR("Invalid index: %d, %d (max: %d)\n",
			 index0, index2, APUIR_DS_READ_LENGTH - 1);
		return;
	}

	dsh = (ds >> 8) & 0xFF;
	dsl = ds & 0xFF;

	temp1 = apuirregs[index0];
	temp3 = apuirregs[index2];

	temp1 = (temp1 & 0x0F) | (dsh << 4); /*BAND12[11:8]*/
	temp3 = dsl; /*BAND12[7:0]*/

	apuirregs[index0] = temp1;
	apuirregs[index2] = temp3;
	APUIR_DEBUG("exchangeregss_band4094_sdc apuirregs[%d] = 0x%x, apuirregs[%d] = 0x%x\n",
		index0, apuirregs[index0], index2, apuirregs[index2]);
}

void oplus_apuir_set_upnit_ds_list(int count, u32* list)
{
	if (count != 3 || !list) {
		APUIR_ERR("oplus_apuir_set_upnit_ds_list %d != 3 or list is null\n", count);
		return;
	}
	m_ds1x0_real = list[0];
	m_ds1x3_real = list[1];
	m_ds2x0_real = list[2];
	APUIR_INFO("oplus_apuir_set_upnit_ds_list %d %d %d\n", m_ds1x0_real, m_ds1x3_real, m_ds2x0_real);
}

void oplus_apuir_set_lessnit_ds_list(int count, u32* list)
{
	if (count != 2 || !list) {
		APUIR_ERR("oplus_apuir_set_lessnit_ds_list %d != 2 or list is null\n", count);
		return;
	}
	m_ds21x0_real = list[0];
	m_ds21x3_real = list[1];
	APUIR_INFO("oplus_apuir_set_lessnit_ds_list %d %d\n", m_ds21x0_real, m_ds21x3_real);
}

void apuir_set_nvt_upnit_index_list(int count, u32* list)
{
	nvt_upnit_index_count = count;
	if ((nvt_upnit_index_count != 4 && nvt_upnit_index_count != 3) || !list) {
		APUIR_ERR("apuir_set_nvt_upnit_index_list %d != 4 or 3 or list is null\n", nvt_upnit_index_count);
		return;
	}
	m_nvt_upnit_index_list[0] = list[0];
	m_nvt_upnit_index_list[1] = list[1];
	m_nvt_upnit_index_list[2] = list[2];
	if (nvt_upnit_index_count == 4) {
		m_nvt_upnit_index_list[3] = list[3];
	}
	APUIR_INFO("apuir_set_nvt_upnit_index_list %d %d %d %d\n",
		m_nvt_upnit_index_list[0], m_nvt_upnit_index_list[1],
		m_nvt_upnit_index_list[2], m_nvt_upnit_index_list[3]);
}

void apuir_set_nvt_lessnit_index_list(int count, u32* list)
{
	nvt_lessnit_index_count = count;
	if ((nvt_lessnit_index_count != 4 && nvt_lessnit_index_count != 3) || !list) {
		APUIR_ERR("apuir_set_nvt_lessnit_index_list %d != 4 or list is null\n", nvt_lessnit_index_count);
		return;
	}
	m_nvt_lessnit_index_list[0] = list[0];
	m_nvt_lessnit_index_list[1] = list[1];
	m_nvt_lessnit_index_list[2] = list[2];
	if (nvt_lessnit_index_count == 4) {
		m_nvt_lessnit_index_list[3] = list[3];
	}
	APUIR_INFO("apuir_set_nvt_lessnit_index_list %d %d %d %d\n",
			m_nvt_lessnit_index_list[0], m_nvt_lessnit_index_list[1],
			m_nvt_lessnit_index_list[2], m_nvt_lessnit_index_list[3]);
}

void apuir_set_nvt_modeset_list(int count, u32* list)
{
	nvt_modeset_count = count;
	if (nvt_modeset_count != 3 || !list) {
		APUIR_ERR("apuir_set_nvt_modeset_list %d != 3 or list is null\n", nvt_modeset_count);
		return;
	}
	m_nvt_modeset_list[0] = list[0];
	m_nvt_modeset_list[1] = list[1];
	m_nvt_modeset_list[2] = list[2];
	APUIR_INFO("apuir_set_nvt_modeset_list %d %d %x %d %x\n",
			m_nvt_modeset_list[0], m_nvt_modeset_list[1], m_nvt_modeset_list[1], m_nvt_modeset_list[2], m_nvt_modeset_list[2]);
}

void apuir_set_nvt_apl_lhset_list(int count, u32* list)
{
	nvt_apl_lhset_count = count;
	if (nvt_apl_lhset_count != 6 || !list) {
		APUIR_ERR("apuir_set_nvt_apl_lhset_list %d != 6 or list is null\n", nvt_apl_lhset_count);
		return;
	}
	m_nvt_apl_lhset_list[0] = list[0];
	m_nvt_apl_lhset_list[1] = list[1];
	m_nvt_apl_lhset_list[2] = list[2];
	m_nvt_apl_lhset_list[3] = list[3];
	m_nvt_apl_lhset_list[4] = list[4];
	m_nvt_apl_lhset_list[5] = list[5];
	APUIR_INFO("apuir_set_nvt_apl_lhset_list %d %d %d %x %d %x %d %x %d %x\n",
			m_nvt_apl_lhset_list[0], m_nvt_apl_lhset_list[1], m_nvt_apl_lhset_list[2], m_nvt_apl_lhset_list[2],
			m_nvt_apl_lhset_list[3], m_nvt_apl_lhset_list[3], m_nvt_apl_lhset_list[4], m_nvt_apl_lhset_list[4],
			m_nvt_apl_lhset_list[5], m_nvt_apl_lhset_list[5]);
}

#define FIXED_SHIFT   16
#define FIXED_ONE     (1 << FIXED_SHIFT)
#define BASE_RATIO    100000
#define GAMMA_LUT_SIZE 512
#define LUT_SCALE 511

static const u32 gamma_lut_512[GAMMA_LUT_SIZE] = {
	0, 3849, 5275, 6342, 7228, 8000, 8691, 9322, 9905, 10450, 10963, 11448, 11910, 12351, 12775, 13182, 13574, 13953, 14320, 14677,
	15023, 15360, 15688, 16008, 16321, 16627, 16926, 17219, 17506, 17787, 18063, 18335, 18601, 18863, 19121, 19374, 19624, 19870,
	20112, 20351, 20587, 20819, 21048, 21275, 21498, 21719, 21937, 22153, 22366, 22576, 22784, 22990, 23194, 23396, 23596, 23793,
	23989, 24183, 24375, 24565, 24753, 24940, 25125, 25308, 25490, 25670, 25849, 26026, 26202, 26377, 26550, 26721, 26892, 27061,
	27229, 27396, 27561, 27725, 27888, 28050, 28211, 28371, 28530, 28687, 28844, 28999, 29154, 29308, 29460, 29612, 29763, 29913,
	30061, 30210, 30357, 30503, 30649, 30793, 30937, 31080, 31223, 31364, 31505, 31645, 31784, 31923, 32061, 32198, 32334, 32470,
	32605, 32739, 32873, 33006, 33139, 33271, 33402, 33532, 33662, 33792, 33920, 34049, 34176, 34303, 34430, 34556, 34681, 34806,
	34930, 35054, 35177, 35300, 35422, 35544, 35665, 35786, 35906, 36026, 36145, 36264, 36382, 36500, 36618, 36735, 36851, 36967,
	37083, 37198, 37313, 37428, 37542, 37655, 37768, 37881, 37993, 38105, 38217, 38328, 38439, 38549, 38659, 38769, 38878, 38987,
	39095, 39204, 39311, 39419, 39526, 39633, 39739, 39845, 39951, 40057, 40162, 40266, 40371, 40475, 40579, 40682, 40785, 40888,
	40991, 41093, 41195, 41296, 41398, 41499, 41599, 41700, 41800, 41900, 41999, 42099, 42198, 42296, 42395, 42493, 42591, 42689,
	42786, 42883, 42980, 43077, 43173, 43269, 43365, 43460, 43556, 43651, 43746, 43840, 43934, 44028, 44122, 44216, 44309, 44402,
	44495, 44588, 44680, 44773, 44865, 44956, 45048, 45139, 45230, 45321, 45412, 45502, 45592, 45682, 45772, 45862, 45951, 46040,
	46129, 46218, 46306, 46395, 46483, 46571, 46659, 46746, 46834, 46921, 47008, 47094, 47181, 47267, 47354, 47440, 47525, 47611,
	47696, 47782, 47867, 47952, 48036, 48121, 48205, 48289, 48373, 48457, 48541, 48624, 48708, 48791, 48874, 48957, 49039, 49122,
	49204, 49286, 49368, 49450, 49532, 49613, 49695, 49776, 49857, 49938, 50018, 50099, 50179, 50259, 50340, 50420, 50499, 50579,
	50658, 50738, 50817, 50896, 50975, 51054, 51132, 51211, 51289, 51367, 51445, 51523, 51601, 51678, 51756, 51833, 51910, 51987,
	52064, 52141, 52218, 52294, 52370, 52447, 52523, 52599, 52675, 52750, 52826, 52901, 52977, 53052, 53127, 53202, 53277, 53351,
	53426, 53500, 53575, 53649, 53723, 53797, 53871, 53944, 54018, 54091, 54165, 54238, 54311, 54384, 54457, 54529, 54602, 54675,
	54747, 54819, 54891, 54964, 55035, 55107, 55179, 55251, 55322, 55394, 55465, 55536, 55607, 55678, 55749, 55820, 55890, 55961,
	56031, 56101, 56172, 56242, 56312, 56382, 56451, 56521, 56591, 56660, 56729, 56799, 56868, 56937, 57006, 57075, 57143, 57212,
	57281, 57349, 57418, 57486, 57554, 57622, 57690, 57758, 57826, 57893, 57961, 58029, 58096, 58163, 58231, 58298, 58365, 58432,
	58498, 58565, 58632, 58698, 58765, 58831, 58898, 58964, 59030, 59096, 59162, 59228, 59294, 59359, 59425, 59491, 59556, 59621,
	59687, 59752, 59817, 59882, 59947, 60012, 60076, 60141, 60206, 60270, 60334, 60399, 60463, 60527, 60591, 60655, 60719, 60783,
	60847, 60911, 60974, 61038, 61101, 61165, 61228, 61291, 61354, 61417, 61480, 61543, 61606, 61669, 61731, 61794, 61856, 61919,
	61981, 62044, 62106, 62168, 62230, 62292, 62354, 62416, 62478, 62539, 62601, 62662, 62724, 62785, 62847, 62908, 62969, 63030,
	63091, 63152, 63213, 63274, 63335, 63396, 63456, 63517, 63577, 63638, 63698, 63758, 63818, 63879, 63939, 63999, 64059, 64119,
	64178, 64238, 64298, 64357, 64417, 64476, 64536, 64595, 64654, 64714, 64773, 64832, 64891, 64950, 65009, 65068, 65126, 65185,
	65244, 65302, 65361, 65419, 65478, 65536
};

/*
 * calculate_ds_medium_precision - Calculate DS value with medium precision using Gamma LUT
 * @maxds: Maximum DS value (e.g., 4095)
 * @ratio: Brightness ratio, range 0-100000, corresponding to 1.0-2.0x brightness (BASE_RATIO=100000 means 1.0x)
 * @scale_factor: Scaling factor to control the normalization range of input ratio
 * Return: Gamma-corrected DS value, range [0, 0xFFFF]
 */
static u32 calculate_ds_medium_precision(u32 maxds, u32 ratio, u32 scale_factor)
{
	u32 numerator = BASE_RATIO + ratio;

	int64_t temp = (uint64_t)numerator << FIXED_SHIFT;
	int64_t frac = temp / scale_factor;

	APUIR_DEBUG("calculate_ds_medium_precision: maxds %d ratio=%d, scale_factor %d, numerator=%d, frac=%d", maxds, ratio, scale_factor, numerator, (u32)frac);

	if (frac < 0) {
		frac = 0;
	} else if (frac > FIXED_ONE) {
		frac = FIXED_ONE;
	}

	u32 index = (frac * (GAMMA_LUT_SIZE - 1)) >> FIXED_SHIFT;
	if (index >= GAMMA_LUT_SIZE) {
		index = GAMMA_LUT_SIZE - 1;
	}

	u32 gamma_value = gamma_lut_512[index];
	APUIR_DEBUG("calculate_ds_medium_precision: index=%d, gamma_value=%d", index, gamma_value);

	int64_t result = ((uint64_t)maxds * gamma_value + (1 << (FIXED_SHIFT - 1))) >> FIXED_SHIFT;
	APUIR_DEBUG("calculate_ds_medium_precision: maxds=%d, result=%d", maxds, (u32)result);

	return (result > 0xFFFF) ? 0xFFFF : (u32)result;
}

static void transfer_ds(u32* aplds, u32* oprds)
{
	/* hwc ds setting */
	u32 DS1X0_predic = 2801;
	u32 DS2X0_predic = 3839;
	int tempa = *aplds, tempo = *oprds;
	u32 scale_factor = 200000;

	aplds_before = *aplds;
	tempa = *aplds;

	if (*aplds != 0) {
		aplratio = (*aplds - DS1X0_predic) * 100000 / (DS2X0_predic - DS1X0_predic);
	}
	if (*oprds != 0) {
		oprratio = (*oprds - DS1X0_predic) * 100000 / (DS2X0_predic - DS1X0_predic);
	}

	/* processing aplds oprds, aplds-upnit-uir-on, oprds-upnit-gir-on*/
	if (is_nvt_ic) {
		if (apuir_gamma_icds) {
			if (*aplds != 0) {
				*aplds = calculate_ds_medium_precision(m_ds2x0_real, aplratio, scale_factor);
			}
			if (*oprds != 0) {
				*oprds = calculate_ds_medium_precision(m_ds2x0_real, oprratio, scale_factor);
			}
		} else {
			if (*aplds != 0) {
				*aplds = aplratio * (m_ds2x0_real - m_ds1x0_real) / 100000 + m_ds1x0_real;
			}
			if (*oprds != 0) {
				*oprds = oprratio * (m_ds2x0_real - m_ds1x0_real) / 100000 + m_ds1x0_real;
			}
		}
	}

	APUIR_INFO("apuirdriver aplratio %d aplds_before %d %x aplds %d->%d oprratio %d oprds %d %x->%d\n",
		aplratio, aplds_before, tempa, tempa, *aplds, oprratio, tempo, tempo, *oprds);
}

void oplus_apuir_set_cmd(void *dsi_display, uint64_t ds)
{
	struct dsi_display *display = dsi_display;
	struct dsi_panel *panel = NULL;
	int seed_mode = 0;
	u8* apuirregs = apuirregs_loading;
	u32 aplds = ds & 0xfff, oprds = (ds & 0xfff000ULL) >> 12;
	u32 aplds_4094 = (ds & 0xfff000000ULL) >> 24;
	u32 oprds_4094 = (ds & 0xfff000000000ULL) >> 36;
	u32 modepose = m_nvt_modeset_list[0];
	u32 apl_lpose = m_nvt_apl_lhset_list[0], apl_hpose = m_nvt_apl_lhset_list[1];
	u32 aplmode = m_nvt_modeset_list[1], oprmode = m_nvt_modeset_list[2];
	int index0 = m_nvt_upnit_index_list[0], index1 = m_nvt_upnit_index_list[1], index2 = m_nvt_upnit_index_list[2], index3 = m_nvt_upnit_index_list[3];
	int index4 = m_nvt_lessnit_index_list[0], index5 = m_nvt_lessnit_index_list[1], index6 = m_nvt_lessnit_index_list[2], index7 = m_nvt_lessnit_index_list[3];
	/* ds2 */
	u32 ds2_max = m_ds21x3_real, ds2_min = m_ds21x0_real;
	u32 ratio = 0;
	u32 ds2 = 0;
	int ret = 0;
	u32 scale_factor = 0;
	int row = 0;
	int length = 0;

	enum dsi_cmd_set_type loading1type;
	enum dsi_cmd_set_type loading2type;
	enum dsi_cmd_set_type loading3type;

	/* Select command set type based on configuration */
	if (apuir_param_from_uirloading) {
		loading1type = DSI_CMD_UIR_LOADING_EFFECT_MODE1;
		loading2type = DSI_CMD_UIR_LOADING_EFFECT_MODE2;
		loading3type = DSI_CMD_UIR_LOADING_EFFECT_MODE3;
		APUIR_DEBUG("Using UIR_LOADING command sets\n");
	} else {
		loading1type = DSI_CMD_LOADING_EFFECT_MODE1;
		loading2type = DSI_CMD_LOADING_EFFECT_MODE2;
		loading3type = DSI_CMD_LOADING_EFFECT_OFF;
		APUIR_DEBUG("Using standard LOADING_EFFECT command sets\n");
	}

	if (!dsi_display) {
		APUIR_ERR("NULL dsi_display");
		return;
	}
	panel = display->panel;
	if (!dsi_panel_initialized(panel)) {
		ADFR_DEBUG("should not send cmd sets if panel is not initialized\n");
		return;
	}
	if (!m_apuirdim_ds_update) {
		APUIR_INFO("update skipped");
		return;
	}
	m_apuirdim_ds_update = false;
	if (!display->panel || !display->panel->cur_mode || !display->panel->cur_mode->priv_info) {
		APUIR_ERR("invalid panel params\n");
		return;
	}

	/* uir cmd are available in power on */
	if (display->panel->power_mode != SDE_MODE_DPMS_ON) {
		ADFR_DEBUG("should not send uir cmd when power mode is %u\n", display->panel->power_mode);
		return;
	}

	SDE_ATRACE_BEGIN("oplus_apuir_set_cmd");
	seed_mode = __oplus_get_seed_mode();
	ADFR_DEBUG("apuirdriver apuir_girseedtype %d seed_mode %d ds 0x%016llx %llu aplds %d %x oprds %d %x aplds_4094 %d %x oprds_4094 %d %x\n",
		apuir_girseedtype, seed_mode, (unsigned long long)ds, (unsigned long long)ds, aplds, aplds, oprds, oprds, aplds_4094, aplds_4094, oprds_4094, oprds_4094);
	transfer_ds(&aplds, &oprds);
	if ((apuir_girseedtype == 3 || apuir_girseedtype == 4) && seed_mode == PANEL_LOADING_EFFECT_OFF) {
		transfer_ds(&aplds_4094, &oprds_4094);
	}
	row = APUIR_DS_READ_ROW_NVT;
	length = m_ds_read_length;
	if (!first_loading) {
		APUIR_INFO("apuirdriver loading apuir cmds\n");
		ret = oplus_panel_cmd_reg_read_specific_row(panel, panel->cur_mode, loading1type, apuirregs_loading_mode1, length, row);
		if (ret < 0) {
			APUIR_ERR("apuirdriver loading apuirregs_loading_mode1 error\n");
			return;
		} else if (ret > 0) {
			if (ret > sizeof(apuirregs_loading_mode1)) {
				APUIR_ERR("m_ds_read_length %d exceeds buffer size %zu, clamping to buffer size\n",
					ret, sizeof(apuirregs_loading_mode1));
				return;
			} else {
				m_ds_read_length = ret;
			}
			m_ds_read_length = ret;
			length = m_ds_read_length;
			APUIR_INFO("reset m_ds_read_length = %d\n", m_ds_read_length);
		}
		ret = oplus_panel_cmd_reg_read_specific_row(panel, panel->cur_mode, loading2type, apuirregs_loading_mode2, length, row);
		if (ret < 0) {
			APUIR_ERR("apuirdriver loading apuirregs_loading_mode2 error\n");
			return;
		}
		ret = oplus_panel_cmd_reg_read_specific_row(panel, panel->cur_mode, loading3type, apuirregs_loading_mode3, length, row);
		if (ret < 0) {
			APUIR_ERR("apuirdriver loading apuirregs_loading_mode3 error\n");
			return;
		}
		first_loading = 1;
	}

	switch (seed_mode) {
	case PANEL_LOADING_EFFECT_MODE1:
		memcpy(apuirregs, apuirregs_loading_mode1, sizeof(apuirregs_loading_mode1));
		break;
	case PANEL_LOADING_EFFECT_MODE2:
		memcpy(apuirregs, apuirregs_loading_mode2, sizeof(apuirregs_loading_mode2));
		break;
	case PANEL_LOADING_EFFECT_OFF:
		memcpy(apuirregs, apuirregs_loading_mode3, sizeof(apuirregs_loading_mode3));
		break;
	default:
		memcpy(apuirregs, apuirregs_loading_mode1, sizeof(apuirregs_loading_mode1));
		break;
	}

	/* processing ds2, ds2-lessnit*/
	if (aplds > 0) {
		if (aplds > m_ds1x3_real) {
			ds2 = ds2_max;
			APUIR_DEBUG("apuirdriver aplds %d > %d ds2 = %d 0x%x\n", aplds, m_ds1x3_real, ds2, ds2);
		} else {
			ratio = aplratio;
			if (apuir_gamma_icds) {
				scale_factor = 130000;
				ds2 = calculate_ds_medium_precision(m_ds21x3_real, aplratio, scale_factor);
			} else {
				ds2 = ratio * (ds2_max - ds2_min) / 30000 + ds2_min;
			}
			if (ds2 > ds2_max) {
				int temp = ds2;
				ds2 = ds2_max;
				APUIR_DEBUG("apuirdriver aplds %d > 0 ds2 = %d 0x%x ratio = %d / 100000 temp = %d\n", aplds, ds2, ds2, ratio, temp);
			} else {
				APUIR_DEBUG("apuirdriver aplds %d > 0 ds2 = %d 0x%x ratio = %d / 100000\n", aplds, ds2, ds2, ratio);
			}
		}
		mAPuirType = DSI_CMD_APUIR_ON;
	} else if (off_framecount < APUIR_OFF_FRAME_COUNT_NVT - 1) {
		if (oprds > m_ds1x3_real) {
			ds2 = ds2_max;
			APUIR_DEBUG("apuirdriver oprds %d aplds == 0 oprds > %d ds2 = %d 0x%x\n", oprds, m_ds1x3_real, ds2, ds2);
		} else {
			ratio = oprratio;
			if (apuir_gamma_icds) {
				scale_factor = 130000;
				ds2 = calculate_ds_medium_precision(m_ds21x3_real, aplratio, scale_factor);
			} else {
				ds2 = ratio * (ds2_max - ds2_min) / 30000 + ds2_min;
			}
			if (ds2 > ds2_max) {
				int temp = ds2;
				ds2 = ds2_max;
				APUIR_DEBUG("apuirdriver oprds %d aplds == 0 ds2 = %d 0x%x ratio = %d / 100000 temp = %d\n", oprds, ds2, ds2, ratio, temp);
			} else {
				APUIR_DEBUG("apuirdriver oprds %d aplds == 0 ds2 = %d 0x%x ratio = %d / 100000\n", oprds, ds2, ds2, ratio);
			}
		}
	}

	/* NVT IC processing regs*/
	if (aplds == 0) {
		/* uir off start*/
		off_framecount++;
		if (off_framecount >= 1 && off_framecount < APUIR_OFF_FRAME_COUNT_NVT) {
			mAPuirType = DSI_CMD_APUIR_MIDDLE_OFF;
			apuirregs[modepose] = oprmode;
			apuirregs[apl_lpose] = m_nvt_apl_lhset_list[2];
			apuirregs[apl_hpose] = m_nvt_apl_lhset_list[3];
		} else if (off_framecount == APUIR_OFF_FRAME_COUNT_NVT) {
			mAPuirType = DSI_CMD_APUIR_OFF;
		}
		if (mAPuirType != DSI_CMD_APUIR_OFF) {
			if (apuir_sdc_band) {
				exchangeregs_sdc(oprds, apuirregs, index0, index1, index2);
				if ((apuir_girseedtype == 3 || apuir_girseedtype == 4) && seed_mode == PANEL_LOADING_EFFECT_OFF) {
					exchangeregss_band4094_sdc(oprds_4094, apuirregs, index0, index2);
				}
			} else {
				exchangeregs_nvt(oprds, apuirregs, index0, index1, index2, index3);
				if ((apuir_girseedtype == 3 || apuir_girseedtype == 4) && seed_mode == PANEL_LOADING_EFFECT_OFF) {
					exchangeregs_band4094_nvt(oprds_4094, apuirregs, index1, index3);
				}
			}
		}
	} else {
		/* uir on start aplds > 0*/
		off_framecount = 0;
		mAPuirType = DSI_CMD_APUIR_ON;
		apuirregs[modepose] = aplmode;
		apuirregs[apl_lpose] = m_nvt_apl_lhset_list[4];
		apuirregs[apl_hpose] = m_nvt_apl_lhset_list[5];
		if (apuir_sdc_band) {
			exchangeregs_sdc(aplds, apuirregs, index0, index1, index2);
			if ((apuir_girseedtype == 3 || apuir_girseedtype == 4) && seed_mode == PANEL_LOADING_EFFECT_OFF) {
				exchangeregss_band4094_sdc(aplds_4094, apuirregs, index0, index2);
			}
		} else {
			exchangeregs_nvt(aplds, apuirregs, index0, index1, index2, index3);
			if ((apuir_girseedtype == 3 || apuir_girseedtype == 4) && seed_mode == PANEL_LOADING_EFFECT_OFF) {
				exchangeregs_band4094_nvt(aplds_4094, apuirregs, index1, index3);
			}
		}
	}

	/*exchange less uir on band nit ds*/
	if (aplds > 0 || (off_framecount >= 1 && off_framecount < APUIR_OFF_FRAME_COUNT_NVT)) {
		if (apuir_sdc_band) {
			exchangeregs_sdc(ds2, apuirregs, index4, index5, index6);
		} else {
			exchangeregs_nvt(ds2, apuirregs, index4, index5, index6, index7);
		}
	}
	APUIR_INFO("offcount %d DSI_CMD_APUIR_ON %d mAPuirType = %d seed_mode = %d regs len=%d\n",
			off_framecount, DSI_CMD_APUIR_ON, mAPuirType, seed_mode, m_ds_read_length);

	/* Command filtering and sending */
	if (off_framecount > APUIR_OFF_FRAME_COUNT_NVT) {
		APUIR_DEBUG("is_nvt_ic && off_framecount > APUIR_OFF_FRAME_COUNT_NVT return\n");
		return;
	}

	SDE_ATRACE_BEGIN("oplus_apuir_set_cmd_replace");
	oplus_panel_cmd_reg_replace_specific_row(panel, panel->cur_mode, mAPuirType, apuirregs_loading, m_ds_read_length, APUIR_DS_READ_ROW_NVT);
	SDE_ATRACE_END("oplus_apuir_set_cmd_replace");

	oplus_apuir_ensure_workqueue(display->panel);
	if (unlikely(!apuir_setcmd_wq)) {
		APUIR_WARN("workqueue not ready, calling handler directly\n");
		oplus_apuir_setcmd_work_handler(&apuir_setcmd_work);
	} else {
		queue_work(apuir_setcmd_wq, &apuir_setcmd_work);
	}

	APUIR_DEBUG("type %d %d %d ret %d aplds %d %d off_framecount %d is diff set cmd\n",
		mAPuirType, m_last_type, m_last_dim, m_ret, m_last_aplds, aplds, off_framecount);
	m_last_type = mAPuirType;
	m_last_aplds = aplds;
	SDE_ATRACE_END("oplus_apuir_set_cmd");
}
/*for nvt end*/

