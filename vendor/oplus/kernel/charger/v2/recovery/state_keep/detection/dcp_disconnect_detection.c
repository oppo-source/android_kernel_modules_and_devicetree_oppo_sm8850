// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025-2025 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "[REC/SK/DCP]([%s][%d]): " fmt, __func__, __LINE__

#include <linux/slab.h>
#include <linux/of.h>
#include <linux/mutex.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/list.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <linux/timer.h>

#include <oplus_chg.h>
#include <oplus_mms.h>
#include <oplus_mms_wired.h>
#include <recovery/state_keep.h>
#include <oplus_chg_cpa.h>
#include <oplus_chg_voter.h>

#define CC_DETECTED_TIMEOUT_MS			290
#define CC_DETECTED_TIMEOUT_CNT			2
#define CC_DETECTED_TIMEOUT_ICL_MA		2000
#define HW_DETECTED_CHECK_INTERVAL_MS		200
#define CHECK_MAX_TIME_MS			3500
#define CHECK_ENABLE_POWER_MW_THD		10000

struct dcp_disconnect_detection {
	struct device_node *node;
	struct state_keep_client *client;

	struct oplus_mms *keep_topic;
	struct mms_subscribe *keep_subs;
	struct oplus_mms *wired_topic;
	struct mms_subscribe *wired_subs;
	struct oplus_mms *cpa_topic;

	struct work_struct wired_online_check_work;
	struct timer_list cc_detached_check_timer;
	struct work_struct cc_detached_check_work;
	struct work_struct hw_detect_update_work;
	struct delayed_work final_check_work;
	struct completion check_complete;

	bool check_start;
	bool keep_online;
	bool wired_online;

	struct votable *wired_icl_votable;
	int cc_detached_timeout_counter;
	int dcp_power_mw;
	bool hw_detect_support;
};

static struct dcp_disconnect_detection *g_ddd;

static void ddd_cc_detached_check_timer_del(struct dcp_disconnect_detection *ddd);
static void ddd_cc_check_setup_timer(struct dcp_disconnect_detection *ddd, unsigned int ms);
static bool is_wired_icl_votable_available(struct dcp_disconnect_detection *ddd);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
static void ddd_cc_detached_check_timer_cb(unsigned long data);
#else
static void ddd_cc_detached_check_timer_cb(struct timer_list *t);
#endif

static void ddd_check_done(struct dcp_disconnect_detection *ddd, bool keep)
{
	ddd->check_start = false;
	ddd->keep_online = keep;
	complete_all(&ddd->check_complete);
	if (!keep) {
		ddd->cc_detached_timeout_counter = 0;
		if (is_wired_icl_votable_available(ddd))
			vote(ddd->wired_icl_votable, STATE_KEEP_DDD_VOTER,
				false, CC_DETECTED_TIMEOUT_ICL_MA, false);
	}
}

static void ddd_state_reset(struct dcp_disconnect_detection *ddd)
{
	ddd->check_start = false;
	ddd->keep_online = false;
	ddd->cc_detached_timeout_counter = 0;
	if (is_wired_icl_votable_available(ddd))
		vote(ddd->wired_icl_votable, STATE_KEEP_DDD_VOTER,
			false, CC_DETECTED_TIMEOUT_ICL_MA, false);
}

static int ddd_client_reset(struct state_keep_client *client)
{
	struct dcp_disconnect_detection *ddd = client->priv_data;

	if (ddd == NULL) {
		chg_err("ddd is null\n");
		return -EINVAL;
	}
	ddd_state_reset(ddd);

	return 0;
}

static int ddd_client_enable(struct state_keep_client *client)
{
	struct dcp_disconnect_detection *ddd = client->priv_data;

	if (ddd == NULL) {
		chg_err("ddd is null\n");
		return -EINVAL;
	}
	ddd_state_reset(ddd);
	client->enabled = true;

	return 0;
}

static int ddd_client_disable(struct state_keep_client *client)
{
	struct dcp_disconnect_detection *ddd = client->priv_data;

	if (ddd == NULL) {
		chg_err("ddd is null\n");
		return -EINVAL;
	}
	client->enabled = false;

	return 0;
}

static int ddd_client_start_check(struct state_keep_client *client, enum oplus_chg_protocol_type protocol)
{
	struct dcp_disconnect_detection *ddd = client->priv_data;
	int hw_detect;
	int cur_dcp_power_mw = 0;

	if (ddd == NULL) {
		chg_err("ddd is null\n");
		return -EINVAL;
	}

	if (ddd->cpa_topic)
		cur_dcp_power_mw = oplus_cpa_protocol_get_max_power_by_type(ddd->cpa_topic, CHG_PROTOCOL_BC12);

	if (cur_dcp_power_mw > ddd->dcp_power_mw)
		ddd->dcp_power_mw = cur_dcp_power_mw;
	if (ddd->dcp_power_mw <= CHECK_ENABLE_POWER_MW_THD)
		return 0;

	hw_detect = oplus_wired_get_hw_detect();
	if (hw_detect == CC_DETECT_NULL)
		ddd->hw_detect_support = false;
	else
		ddd->hw_detect_support = true;

	ddd->wired_online = false;
	ddd_cc_detached_check_timer_del(ddd);
	cancel_work_sync(&ddd->cc_detached_check_work);
	ddd->check_start = true;
	ddd->keep_online = false;
	reinit_completion(&ddd->check_complete);
	ddd_cc_check_setup_timer(ddd, CC_DETECTED_TIMEOUT_MS);

	return 0;
}

static bool ddd_client_need_keep(struct state_keep_client *client, enum oplus_chg_protocol_type protocol)
{
	struct dcp_disconnect_detection *ddd = client->priv_data;
	unsigned long left;

	if (ddd == NULL) {
		chg_err("ddd is null\n");
		return false;
	}

	if (!ddd->check_start)
		return ddd->keep_online;

	if (ddd->dcp_power_mw <= CHECK_ENABLE_POWER_MW_THD) {
		return false;
	}

	left = wait_for_completion_timeout(&ddd->check_complete,
			msecs_to_jiffies(CHECK_MAX_TIME_MS + 500));
	if (left == 0) {
		chg_err("check timeout\n");
		return false;
	}

	return ddd->keep_online;
}

static struct state_keep_client_desc g_state_keep_client_desc = {
	.name = "dcp_disconnect_detection",
	.priority = STATE_KEEP_CLIENT_DCP_DISCONNECT_DETECTION,
	.ops = {
		.reset = ddd_client_reset,
		.enable = ddd_client_enable,
		.disable = ddd_client_disable,
		.start_check = ddd_client_start_check,
		.need_keep = ddd_client_need_keep,
	}
};

static int ddd_client_register(struct dcp_disconnect_detection *ddd)
{
	struct state_keep_client *client;

	client = state_keep_client_register(ddd->keep_topic, &g_state_keep_client_desc, ddd);
	if (IS_ERR_OR_NULL(client)) {
		chg_err("register dcp_disconnect_detection client failed\n");
		return -EINVAL;
	}
	ddd->client = client;
	state_keep_client_enable(client);

	return 0;
}

static void ddd_subscribe_state_keep_topic(struct oplus_mms *topic, void *prv_data)
{
	struct dcp_disconnect_detection *ddd = prv_data;

	ddd->keep_topic = topic;
	ddd_client_register(ddd);
}

static bool is_wired_icl_votable_available(struct dcp_disconnect_detection *ddd)
{
	if (!ddd)
		return false;

	if (!ddd->wired_icl_votable)
		ddd->wired_icl_votable = find_votable("WIRED_ICL");
	return !!ddd->wired_icl_votable;
}

static void ddd_cc_detached_check_timer_del(struct dcp_disconnect_detection *ddd)
{
	if (timer_pending(&ddd->cc_detached_check_timer))
		del_timer_sync(&ddd->cc_detached_check_timer);
}

static void ddd_cc_detached_check_timer_init(struct dcp_disconnect_detection *ddd)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
	init_timer(&ddd->cc_detached_check_timer);
	ddd->cc_detached_check_timer.data = (unsigned long)ddd;
	ddd->cc_detached_check_timer.function = ddd_cc_detached_check_timer_cb;
#else
	timer_setup(&ddd->cc_detached_check_timer, ddd_cc_detached_check_timer_cb, 0);
#endif
}

static void ddd_cc_check_setup_timer(struct dcp_disconnect_detection *ddd, unsigned int ms)
{
	mod_timer(&ddd->cc_detached_check_timer, jiffies + msecs_to_jiffies(ms));
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
static void ddd_cc_detached_check_timer_cb(unsigned long data)
{
	struct dcp_disconnect_detection *ddd = (struct dcp_disconnect_detection *)data;
#else
static void ddd_cc_detached_check_timer_cb(struct timer_list *t)
{
	struct dcp_disconnect_detection *ddd = from_timer(ddd, t, cc_detached_check_timer);
#endif
	schedule_work(&ddd->cc_detached_check_work);
}

static bool ddd_is_need_to_disconnect(struct dcp_disconnect_detection *ddd)
{
	union mms_msg_data data = { 0 };
	unsigned char cc1 = 0, cc2 = 0;
	int hw_detect = CC_DETECT_NULL;
	bool disconnected = false;
	int rc;

	if (ddd->hw_detect_support) {
		hw_detect = oplus_wired_get_hw_detect();
	} else if (ddd->wired_topic) {
		rc = oplus_mms_get_item_data(ddd->wired_topic, WIRED_ITEM_CC_STATE, &data, true);
		if (rc == 0) {
			cc1 = data.intval & 0xff;
			cc2 = (data.intval >> 8) & 0xff;
		} else {
			chg_err("can't get cc state, rc=%d\n", rc);
		}
	}

	chg_err("hw_detect = %d, cc1 = %d, cc2 = %d\n",
			hw_detect, cc1, cc2);
	if (ddd->hw_detect_support && hw_detect != CC_DETECT_PLUGIN) {
		disconnected = true;
	} else if (!ddd->hw_detect_support &&
			cc1 == 0 && cc2 == 0) {
		disconnected = true;
	}

	return disconnected;
}

static void ddd_cc_detached_check_work(struct work_struct *work)
{
	struct dcp_disconnect_detection *ddd =
		container_of(work, struct dcp_disconnect_detection, cc_detached_check_work);

	if (ddd_is_need_to_disconnect(ddd)) {
		ddd->cc_detached_timeout_counter = 0;
		if (is_wired_icl_votable_available(ddd))
			vote(ddd->wired_icl_votable, STATE_KEEP_DDD_VOTER,
				false, CC_DETECTED_TIMEOUT_ICL_MA, false);
		ddd_check_done(ddd, false);
	} else {
		ddd->cc_detached_timeout_counter++;
		chg_err("cc_detached_timeout_counter = %d\n",
				ddd->cc_detached_timeout_counter);
		if (ddd->cc_detached_timeout_counter >= CC_DETECTED_TIMEOUT_CNT) {
			if (is_wired_icl_votable_available(ddd))
				vote(ddd->wired_icl_votable, STATE_KEEP_DDD_VOTER,
					true, CC_DETECTED_TIMEOUT_ICL_MA, true);
		}
		schedule_delayed_work(&ddd->final_check_work,
				msecs_to_jiffies(CHECK_MAX_TIME_MS - CC_DETECTED_TIMEOUT_MS));
	}
}

static void ddd_final_check_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct dcp_disconnect_detection *ddd =
		container_of(dwork, struct dcp_disconnect_detection, final_check_work);

	if (ddd->wired_online == false) {
		ddd->cc_detached_timeout_counter = 0;
		if (is_wired_icl_votable_available(ddd))
			vote(ddd->wired_icl_votable, STATE_KEEP_DDD_VOTER,
				false, CC_DETECTED_TIMEOUT_ICL_MA, false);
	}
	ddd_check_done(ddd, ddd->wired_online);
}

static void ddd_wired_online_check_work(struct work_struct *work)
{
	struct dcp_disconnect_detection *ddd =
		container_of(work, struct dcp_disconnect_detection, wired_online_check_work);
	union mms_msg_data data = { 0 };
	int rc;

	rc = oplus_mms_get_item_data(ddd->wired_topic, WIRED_ITEM_ONLINE, &data, false);
	if (rc < 0) {
		chg_err("failed to get wired online, rc=%d\n", rc);
		return;
	}
	ddd->wired_online = !!data.intval;
	chg_info("wired online: %d\n", ddd->wired_online);

	if (ddd->wired_online) {
		if (timer_pending(&ddd->cc_detached_check_timer)) {
			del_timer_sync(&ddd->cc_detached_check_timer);
			ddd_check_done(ddd, true);
		} else if (delayed_work_pending(&ddd->final_check_work)) {
			cancel_delayed_work_sync(&ddd->final_check_work);
			ddd_check_done(ddd, true);
		}
	}
}

static void ddd_hw_detect_update_work(struct work_struct *work)
{
	struct dcp_disconnect_detection *ddd =
		container_of(work, struct dcp_disconnect_detection, hw_detect_update_work);
	union mms_msg_data data = { 0 };
	int hw_detect;
	int rc;

	rc = oplus_mms_get_item_data(ddd->wired_topic, WIRED_ITEM_CC_DETECT, &data, false);
	if (rc < 0) {
		chg_err("failed to get wired hw_detect, rc=%d\n", rc);
		return;
	}
	hw_detect = data.intval;
	chg_info("hw_detect: %d\n", hw_detect);

	if (hw_detect == CC_DETECT_PLUGIN)
		return;

	if (timer_pending(&ddd->cc_detached_check_timer)) {
		del_timer_sync(&ddd->cc_detached_check_timer);
		ddd_check_done(ddd, false);
	} else if (delayed_work_pending(&ddd->final_check_work)) {
		cancel_delayed_work_sync(&ddd->final_check_work);
		ddd_check_done(ddd, false);
	}
}

static void ddd_wired_subs_callback(struct mms_subscribe *subs,
				    enum mms_msg_type type, u32 id, bool sync)
{
	struct dcp_disconnect_detection *ddd = subs->priv_data;

	switch (type) {
	case MSG_TYPE_ITEM:
		switch (id) {
		case WIRED_ITEM_ONLINE:
			schedule_work(&ddd->wired_online_check_work);
			break;
		case WIRED_ITEM_CC_DETECT:
			schedule_work(&ddd->hw_detect_update_work);
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

static void ddd_subscribe_wired_topic(struct oplus_mms *topic, void *prv_data)
{
	struct dcp_disconnect_detection *ddd = prv_data;

	ddd->wired_topic = topic;
	ddd->wired_subs = oplus_mms_subscribe(ddd->wired_topic, ddd,
			ddd_wired_subs_callback,
			"dcp_disconnect_detection");

	if (IS_ERR_OR_NULL(ddd->wired_subs)) {
		chg_err("subscribe wired topic error, rc=%ld\n",
			PTR_ERR(ddd->wired_subs));
		return;
	}
}

static void ddd_subscribe_cpa_topic(struct oplus_mms *topic, void *prv_data)
{
	struct dcp_disconnect_detection *ddd = prv_data;

	ddd->cpa_topic = topic;
	ddd->dcp_power_mw = oplus_cpa_protocol_get_max_power_by_type(ddd->cpa_topic, CHG_PROTOCOL_BC12);
}

int dcp_disconnect_detection_init(struct device_node *node)
{
	struct dcp_disconnect_detection *ddd;

	chg_info("dcp_disconnect_detection_init\n");
	ddd = kzalloc(sizeof(struct dcp_disconnect_detection), GFP_KERNEL);
	if (ddd == NULL) {
		chg_err("failed to alloc memory for ddd");
		return -ENOMEM;
	}
	g_ddd = ddd;
	ddd->node = node;

	init_completion(&ddd->check_complete);
	ddd_state_reset(ddd);

	INIT_WORK(&ddd->wired_online_check_work, ddd_wired_online_check_work);
	INIT_WORK(&ddd->cc_detached_check_work, ddd_cc_detached_check_work);
	INIT_WORK(&ddd->hw_detect_update_work, ddd_hw_detect_update_work);
	INIT_DELAYED_WORK(&ddd->final_check_work, ddd_final_check_work);
	ddd_cc_detached_check_timer_init(ddd);

	oplus_mms_wait_topic("state_keep", ddd_subscribe_state_keep_topic, ddd);
	oplus_mms_wait_topic("wired", ddd_subscribe_wired_topic, ddd);
	oplus_mms_wait_topic("cpa", ddd_subscribe_cpa_topic, ddd);

	return 0;
}

void dcp_disconnect_monitor_exit(void)
{
	if (!g_ddd)
		return;

	ddd_cc_detached_check_timer_del(g_ddd);
	cancel_delayed_work_sync(&g_ddd->final_check_work);

	if (!IS_ERR_OR_NULL(g_ddd->wired_subs))
		oplus_mms_unsubscribe(g_ddd->wired_subs);
	if (!IS_ERR_OR_NULL(g_ddd->keep_subs))
		oplus_mms_unsubscribe(g_ddd->keep_subs);
	if (g_ddd->client)
		state_keep_client_unregister(g_ddd->client);

	kfree(g_ddd);
	g_ddd = NULL;
}

