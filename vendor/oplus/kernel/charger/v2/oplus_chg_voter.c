// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2017, 2019 The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt) "[VOTE]([%s][%d]): " fmt, __func__, __LINE__

#include <linux/proc_fs.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/bitops.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/list.h>
#include <linux/hashtable.h>
#include <oplus_chg_voter.h>
#include <oplus_chg.h>
#include <oplus_chg_monitor.h>
#include <debug/oplus_debug_auth.h>

#define NUM_MAX_CLIENTS		32
#define DEBUG_FORCE_CLIENT	"DEBUG_FORCE_CLIENT"
#define HASHTABLE_BITS		4
#define TRACK_BUFFER_SIZE	256

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 17, 0))
#define pde_data(inode) PDE_DATA(inode)
#endif

static DEFINE_SPINLOCK(votable_list_slock);
static LIST_HEAD(votable_list);

static struct proc_dir_entry *debug_root;

struct client_vote {
	bool	enabled;
	int	value;
	int	(*check_func)(struct votable *votable, void *data,
			     const char *client_str, bool enabled, int val, bool step);
	void	*data;
};

struct event_check_point {
	struct list_head list;
	struct delayed_work check_work;
	struct mms_subscribe *subs;
	struct votable *votable;
	const char *name;
	struct oplus_mms *topic;
	u32 item_id;
	enum votable_check_type check_type;
	bool (*check_enabled)(union mms_msg_data *data);
	bool step;
	unsigned long delayed_ms;
};

struct votable {
	const char		*name;
	const char		*override_client;
	struct list_head	list;
	struct client_vote	votes[NUM_MAX_CLIENTS];
	int			num_clients;
	int			type;
	int			effective_client_id;
	int			effective_result;
	int			override_result;
	struct mutex		vote_lock;
	void			*data;
	int			(*callback)(struct votable *votable,
						void *data,
						int effective_result,
						const char *effective_client,
						bool step);
	int			(*callback_result)(struct votable *votable,
						void *data,
						int *result);
	char			*client_strs[NUM_MAX_CLIENTS];
	bool			voted_on;
	struct proc_dir_entry	*root;
	struct proc_dir_entry	*status_ent;
	u32			force_val;
	struct proc_dir_entry	*force_val_ent;
	bool			force_active;
	struct proc_dir_entry	*force_active_ent;
	struct proc_dir_entry	*clients_ent;
	struct proc_dir_entry	*check_points_ent;
#if IS_ENABLED(CONFIG_OPLUS_DEBUG_AUTH)
	struct proc_dir_entry	*vote_ent;
#endif
	DECLARE_HASHTABLE(hash_table, HASHTABLE_BITS);
	struct list_head check_list;
	struct mutex check_list_lock;

	bool skip_check;
	struct list_head event_check_point_list;
	struct mutex event_check_point_list_lock;
	unsigned int event_check_point_count;
	struct delayed_work timer_check_work;
	enum votable_check_type timer_check_type;
	unsigned long timer_check_interval_ms;
	bool timer_check_step;
};

struct vote_val_map {
	int original;
	int mapped;
	struct hlist_node node;
};

struct vote_check_func {
	struct list_head list;
	int (*check)(struct votable *votable,
		void *data, const char *client_str,
		bool enabled, int val, bool step);
};

enum vote_abnormal_type {
	VOTE_ABNORMAL_RESULT = 0,
	VOTE_ABNORMAL_CLIENT
};

static int vote_get_mapped_val(struct votable *votable, int original, int *mapped)
{
	struct vote_val_map *tmp;

	hash_for_each_possible(votable->hash_table, tmp, node, original) {
		if (tmp->original == original) {
			*mapped = tmp->mapped;
			return 0;
		}
	}

	return -EINVAL;
}

static bool is_client_excluded(char *client_str, const char *exclude_str)
{
	if (!client_str)
		return false;

	if (exclude_str != NULL && strcmp(client_str, exclude_str) == 0)
		return true;

	return false;
}

static void vote_abnormal_track_upload(char *buf, size_t size)
{
	struct mms_msg *msg;
	struct oplus_mms *err_topic;
	int rc;

	err_topic = oplus_mms_get_by_name("error");
	if (!err_topic)
		return;

	buf[size - 1] = 0;
	msg = oplus_mms_alloc_str_msg(MSG_TYPE_ITEM, MSG_PRIO_LOW, ERR_ITEM_VOTE_ABNORMAL, buf);
	if (msg == NULL) {
		chg_err("alloc error msg error\n");
		return;
	}
	rc = oplus_mms_publish_msg_sync(err_topic, msg);
	if (rc < 0) {
		chg_err("publish error msg error, rc=%d\n", rc);
		kfree(msg);
	}
}

static void vote_abnormal_track_upload_result(struct votable *votable, int expected_result, int actual_result)
{
	char buf[TRACK_BUFFER_SIZE] = { 0 };

	scnprintf(buf, TRACK_BUFFER_SIZE - 1, "$$type@@%d$$votable@@%s$$expected@@%d$$actual@@%d",
		VOTE_ABNORMAL_RESULT, votable->name, expected_result, actual_result);
	vote_abnormal_track_upload(buf, sizeof(buf));
}

static void vote_abnormal_track_upload_client(struct votable *votable, const char *client_str, int value)
{
	char buf[TRACK_BUFFER_SIZE] = { 0 };

	scnprintf(buf, TRACK_BUFFER_SIZE - 1, "$$type@@%d$$votable@@%s$$client@@%s$$value@@%d",
		VOTE_ABNORMAL_CLIENT, votable->name, client_str, value);
	vote_abnormal_track_upload(buf, sizeof(buf));
}

/**
 * vote_set_any()
 * @votable:	votable object
 * @client_id:	client number of the latest voter
 * @eff_res:	sets 0 or 1 based on the voting
 * @eff_id:	Always returns the client_id argument
 *
 * Note that for SET_ANY voter, the value is always same as enabled. There is
 * no idea of a voter abstaining from the election. Hence there is never a
 * situation when the effective_id will be invalid, during election.
 *
 * Context:
 *	Must be called with the votable->lock held
 */
static void vote_set_any(struct votable *votable, int client_id,
				int *eff_res, int *eff_id, const char *exclude_str)
{
	int i;

	*eff_res = 0;

	for (i = 0; i < votable->num_clients && votable->client_strs[i]; i++) {
		if (exclude_str != NULL && is_client_excluded(votable->client_strs[i], exclude_str))
			continue;
		*eff_res |= votable->votes[i].enabled;
		if (client_id == -EINVAL)
			client_id = i;
	}

	*eff_id = client_id;
}

/**
 * vote_min() -
 * @votable:	votable object
 * @client_id:	client number of the latest voter
 * @eff_res:	sets this to the min. of all the values amongst enabled voters.
 *		If there is no enabled client, this is set to INT_MAX
 * @eff_id:	sets this to the client id that has the min value amongst all
 *		the enabled clients. If there is no enabled client, sets this
 *		to -EINVAL
 *
 * Context:
 *	Must be called with the votable->lock held
 */
static void vote_min(struct votable *votable, int client_id,
				int *eff_res, int *eff_id, const char *exclude_str)
{
	int i;
	int val;
	int mapped;

	*eff_res = INT_MAX;
	*eff_id = -EINVAL;
	for (i = 0; i < votable->num_clients && votable->client_strs[i]; i++) {
		if (exclude_str != NULL && is_client_excluded(votable->client_strs[i], exclude_str))
			continue;
		if (votable->votes[i].enabled) {
			val = votable->votes[i].value;
			if (!vote_get_mapped_val(votable, val, &mapped))
				val = mapped;
			if (*eff_res > val) {
				*eff_res = val;
				*eff_id = i;
			}
		}
	}
	if (*eff_id == -EINVAL)
		*eff_res = -EINVAL;
	else
		*eff_res = votable->votes[*eff_id].value;
}

/**
 * vote_max() -
 * @votable:	votable object
 * @client_id:	client number of the latest voter
 * @eff_res:	sets this to the max. of all the values amongst enabled voters.
 *		If there is no enabled client, this is set to -EINVAL
 * @eff_id:	sets this to the client id that has the max value amongst all
 *		the enabled clients. If there is no enabled client, sets this to
 *		-EINVAL
 *
 * Context:
 *	Must be called with the votable->lock held
 */
static void vote_max(struct votable *votable, int client_id,
				int *eff_res, int *eff_id, const char *exclude_str)
{
	int i;
	int val;
	int mapped;

	*eff_res = INT_MIN;
	*eff_id = -EINVAL;
	for (i = 0; i < votable->num_clients && votable->client_strs[i]; i++) {
		if (exclude_str != NULL && is_client_excluded(votable->client_strs[i], exclude_str))
			continue;
		if (votable->votes[i].enabled) {
			val = votable->votes[i].value;
			if (!vote_get_mapped_val(votable, val, &mapped))
				val = mapped;
			if (*eff_res < val) {
				*eff_res = val;
				*eff_id = i;
			}
		}
	}
	if (*eff_id == -EINVAL)
		*eff_res = -EINVAL;
	else
		*eff_res = votable->votes[*eff_id].value;
}

int get_effective_result_exclude_client_locked(struct votable *votable, const char *exclude_str)
{
	int effective_result = 0;
	int effective_id = -EINVAL;

	if (!votable || !exclude_str)
		return -EINVAL;

	if (votable->force_active)
		return votable->force_val;

	if (votable->override_result != -EINVAL)
		return votable->override_result;

	switch (votable->type) {
	case VOTE_MIN:
		vote_min(votable, 0, &effective_result, &effective_id, exclude_str);
		break;
	case VOTE_MAX:
		vote_max(votable, 0, &effective_result, &effective_id, exclude_str);
		break;
	case VOTE_SET_ANY:
		vote_set_any(votable, 0, &effective_result, &effective_id, exclude_str);
		break;
	default:
		chg_err("unknown votable type, type=%d\n", votable->type);
		return -EINVAL;
	}

	return effective_result;
}

int get_effective_result_exclude_client(struct votable *votable, const char *exclude_str)
{
	int value;

	if (!votable || !exclude_str)
		return -EINVAL;

	lock_votable(votable);
	value = get_effective_result_exclude_client_locked(votable, exclude_str);
	unlock_votable(votable);
	return value;
}

static int get_client_id(struct votable *votable, const char *client_str)
{
	int i;

	for (i = 0; i < votable->num_clients; i++) {
		if (votable->client_strs[i]
		 && (strcmp(votable->client_strs[i], client_str) == 0))
			return i;
	}

	/* new client */
	for (i = 0; i < votable->num_clients; i++) {
		if (!votable->client_strs[i]) {
			votable->client_strs[i]
				= kstrdup(client_str, GFP_KERNEL);
			if (!votable->client_strs[i])
				return -ENOMEM;
			return i;
		}
	}
	return -EINVAL;
}

static char *get_client_str(struct votable *votable, int client_id)
{
	if (!votable || (client_id == -EINVAL))
		return NULL;

	return votable->client_strs[client_id];
}

void lock_votable(struct votable *votable)
{
	mutex_lock(&votable->vote_lock);
}

void unlock_votable(struct votable *votable)
{
	mutex_unlock(&votable->vote_lock);
}

const char *votable_name(struct votable *votable)
{
	if (votable == NULL)
		return NULL;
	return votable->name;
}

/**
 * recheck_vote_locked() -
 * Recheck the vote for the votable
 * @votable: votable object
 * @step: step flag
 *
 * Returns:
 * 0 on success, negative value on failure
 */
static int recheck_vote_locked(struct votable *votable, bool step)
{
	int effective_result = -EINVAL;
	int effective_id = -EINVAL;
	int rc = 0;

	switch (votable->type) {
	case VOTE_MIN:
		vote_min(votable, -EINVAL, &effective_result, &effective_id, NULL);
		break;
	case VOTE_MAX:
		vote_max(votable, -EINVAL, &effective_result, &effective_id, NULL);
		break;
	case VOTE_SET_ANY:
		vote_set_any(votable, -EINVAL, &effective_result, &effective_id, NULL);
		break;
	default:
		return -EINVAL;
	}

	if (effective_result == votable->effective_result)
		return 0;

	votable->effective_client_id = effective_id;
	votable->effective_result = effective_result;
	chg_debug("%s: effective vote is now %d voted by %s,%d\n",
		votable->name, effective_result,
		get_client_str(votable, effective_id),
		effective_id);
	if (votable->callback && !votable->force_active
			&& (votable->override_result == -EINVAL))
		rc = votable->callback(votable, votable->data,
				effective_result,
				get_client_str(votable, effective_id), step);

	return rc;
}

__maybe_unused
static int recheck_vote(struct votable *votable, bool step)
{
	int rc;

	lock_votable(votable);
	rc = recheck_vote_locked(votable, step);
	unlock_votable(votable);
	return rc;
}

/**
 * votable_check_all_clients() -
 * Trigger check for all clients in the votable
 * @votable: votable object
 * @step: step flag
 *
 * Returns:
 * Number of clients that failed the check, negative value on error
 */
int votable_check_all_clients(struct votable *votable, bool step)
{
	int i;
	int rc = 0;
	int value;

	if (!votable)
		return -EINVAL;
	if (votable->skip_check)
		return 0;

	lock_votable(votable);

	for (i = 0; i < votable->num_clients; i++) {
		if (!votable->votes[i].check_func)
			continue;
		if (!votable->client_strs[i])
			continue;
		if (!votable->votes[i].enabled)
			continue;

		rc = votable->votes[i].check_func(votable, votable->votes[i].data,
						 votable->client_strs[i],
						 votable->votes[i].enabled,
						 votable->votes[i].value, step);
		if (rc < 0) {
			chg_err("%s: individual check failed for client[%s], rc=%d\n",
				votable->name, votable->client_strs[i], rc);
			value = votable->votes[i].value;
			votable->votes[i].enabled = false;
			votable->votes[i].value = 0;
			vote_abnormal_track_upload_client(votable, votable->client_strs[i], value);
		}
	}

	rc = recheck_vote_locked(votable, step);
	if (rc < 0)
		chg_err("%s: recheck vote failed, rc=%d\n", votable->name, rc);
	unlock_votable(votable);

	return rc;
}

int votable_check_result_locked(struct votable *votable, bool step)
{
	int rc;
	int result;
	int expected_result;

	if (!votable || !votable->callback_result)
		return -EINVAL;
	if (votable->skip_check)
		return 0;

	rc = votable->callback_result(votable, votable->data, &result);
	if (rc < 0) {
		chg_err("failed to call callback result function\n");
	} else if (result != votable->effective_result) {
		chg_err("result[%d] != effective_result[%d], rerun election\n",
			result, votable->effective_result);
		expected_result = votable->effective_result;
		rerun_election_unlock(votable, step);
		vote_abnormal_track_upload_result(votable, expected_result, result);
	}

	return rc;
}

/**
 * votable_check_result() -
 * Check the result of the votable
 * @votable: votable object
 * @step: step flag
 *
 * Returns:
 * 0 on success, negative value on failure
 */
int votable_check_result(struct votable *votable, bool step)
{
	int rc;

	if (!votable || !votable->callback_result)
		return -EINVAL;
	if (votable->skip_check)
		return 0;

	lock_votable(votable);
	rc = votable_check_result_locked(votable, step);
	unlock_votable(votable);

	return rc;
}

/**
 * is_override_vote_enabled() -
 * is_override_vote_enabled_locked() -
 *		The unlocked and locked variants of getting whether override
		vote is enabled.
 * @votable:	the votable object
 *
 * Returns:
 *	True if the client's vote is enabled; false otherwise.
 */
bool is_override_vote_enabled_locked(struct votable *votable)
{
	if (!votable)
		return false;

	return votable->override_result != -EINVAL;
}

bool is_override_vote_enabled(struct votable *votable)
{
	bool enable;

	if (!votable)
		return false;

	lock_votable(votable);
	enable = is_override_vote_enabled_locked(votable);
	unlock_votable(votable);

	return enable;
}

/**
 * is_client_vote_enabled() -
 * is_client_vote_enabled_locked() -
 *		The unlocked and locked variants of getting whether a client's
		vote is enabled.
 * @votable:	the votable object
 * @client_str: client of interest
 *
 * Returns:
 *	True if the client's vote is enabled; false otherwise.
 */
bool is_client_vote_enabled_locked(struct votable *votable,
							const char *client_str)
{

	int client_id;

	if (!votable || !client_str)
		return false;

	client_id = get_client_id(votable, client_str);
	if (client_id < 0)
		return false;

	return votable->votes[client_id].enabled;
}

bool is_client_vote_enabled(struct votable *votable, const char *client_str)
{
	bool enabled;

	if (!votable || !client_str)
		return false;

	lock_votable(votable);
	enabled = is_client_vote_enabled_locked(votable, client_str);
	unlock_votable(votable);
	return enabled;
}

/**
 * get_client_vote() -
 * get_client_vote_locked() -
 *		The unlocked and locked variants of getting a client's voted
 *		value.
 * @votable:	the votable object
 * @client_str: client of interest
 *
 * Returns:
 *	The value the client voted for. -EINVAL is returned if the client
 *	is not enabled or the client is not found.
 */
int get_client_vote_locked(struct votable *votable, const char *client_str)
{
	int client_id;

	if (!votable || !client_str)
		return -EINVAL;

	client_id = get_client_id(votable, client_str);
	if (client_id < 0)
		return -EINVAL;

	if ((votable->type != VOTE_SET_ANY)
		&& !votable->votes[client_id].enabled)
		return -EINVAL;

	return votable->votes[client_id].value;
}

int get_client_vote(struct votable *votable, const char *client_str)
{
	int value;

	if (!votable || !client_str)
		return -EINVAL;

	lock_votable(votable);
	value = get_client_vote_locked(votable, client_str);
	unlock_votable(votable);
	return value;
}

/**
 * get_effective_result() -
 * get_effective_result_locked() -
 *		The unlocked and locked variants of getting the effective value
 *		amongst all the enabled voters.
 *
 * @votable:	the votable object
 *
 * Returns:
 *	The effective result.
 *	For MIN and MAX votable, returns -EINVAL when the votable
 *	object has been created but no clients have casted their votes or
 *	the last enabled client disables its vote.
 *	For SET_ANY votable it returns 0 when no clients have casted their votes
 *	because for SET_ANY there is no concept of abstaining from election. The
 *	votes for all the clients of SET_ANY votable is defaulted to false.
 */
int get_effective_result_locked(struct votable *votable)
{
	if (!votable)
		return -EINVAL;

	if (votable->force_active)
		return votable->force_val;

	if (votable->override_result != -EINVAL)
		return votable->override_result;

	return votable->effective_result;
}

int get_effective_result(struct votable *votable)
{
	int value;

	if (!votable)
		return -EINVAL;

	lock_votable(votable);
	value = get_effective_result_locked(votable);
	unlock_votable(votable);
	return value;
}

/**
 * get_effective_client() -
 * get_effective_client_locked() -
 *		The unlocked and locked variants of getting the effective client
 *		amongst all the enabled voters.
 *
 * @votable:	the votable object
 *
 * Returns:
 *	The effective client.
 *	For MIN and MAX votable, returns NULL when the votable
 *	object has been created but no clients have casted their votes or
 *	the last enabled client disables its vote.
 *	For SET_ANY votable it returns NULL too when no clients have casted
 *	their votes. But for SET_ANY since there is no concept of abstaining
 *	from election, the only client that casts a vote or the client that
 *	caused the result to change is returned.
 */
const char *get_effective_client_locked(struct votable *votable)
{
	if (!votable)
		return NULL;

	if (votable->force_active)
		return DEBUG_FORCE_CLIENT;

	if (votable->override_result != -EINVAL)
		return votable->override_client;

	return get_client_str(votable, votable->effective_client_id);
}

const char *get_effective_client(struct votable *votable)
{
	const char *client_str;

	if (!votable)
		return NULL;

	lock_votable(votable);
	client_str = get_effective_client_locked(votable);
	unlock_votable(votable);
	return client_str;
}

static bool vote_global_check(struct votable *votable, void *data,
	const char *client_str, bool enabled, int val, bool step)
{
	struct vote_check_func *check_info;
	bool result = true;
	int rc = 0;

	if (votable->skip_check)
		return true;

	mutex_lock(&votable->check_list_lock);
	list_for_each_entry(check_info, &votable->check_list, list) {
		if (check_info->check == NULL)
			continue;
		rc = check_info->check(votable, votable->data, client_str, enabled, val, step);
		if (rc < 0) {
			chg_err("%s: vote[%s] data(enabled=%s, val=%d) check error, rc=%d\n",
				votable->name, client_str, enabled ? "true" : "false", val, rc);
			result = false;
			break;
		}
	}
	mutex_unlock(&votable->check_list_lock);
	return result;
}

static bool vote_client_check(struct votable *votable, void *data,
	const char *client_str, bool enabled, int val, bool step)
{
	int rc = 0;
	int client_id;

	if (votable->skip_check)
		return true;

	client_id = get_client_id(votable, client_str);
	if (client_id < 0)
		return false;

	if (!enabled || !votable->votes[client_id].check_func)
		return true;

	rc = votable->votes[client_id].check_func(
		votable, votable->votes[client_id].data, client_str, enabled, val, step);
	if (rc < 0) {
		chg_err("%s: client[%s] individual check failed, rc=%d, vote rejected\n",
			votable->name, client_str, rc);
		vote_abnormal_track_upload_client(votable, client_str, val);
		return false;
	}

	return true;
}

/**
 * vote() -
 *
 * @votable:	the votable object
 * @client_str: the voting client
 * @enabled:	This provides a means for the client to exclude himself from
 *		election. This clients val (the next argument) will be
 *		considered only when he has enabled his participation.
 *		Note that this takes a differnt meaning for SET_ANY type, as
 *		there is no concept of abstaining from participation.
 *		Enabled is treated as the boolean value the client is voting.
 * @val:	The vote value. This is ignored for SET_ANY votable types.
 *		For MIN, MAX votable types this value is used as the
 *		clients vote value when the enabled is true, this value is
 *		ignored if enabled is false.
 *
 * The callback is called only when there is a change in the election results or
 * if it is the first time someone is voting.
 *
 * Returns:
 *	The return from the callback when present and needs to be called
 *	or zero.
 */
int vote(struct votable *votable, const char *client_str, bool enabled, int val, bool step)
{
	int effective_id = -EINVAL;
	int effective_result;
	int client_id;
	int rc = 0;
	bool similar_vote = false;

	if (!votable || !client_str)
		return -EINVAL;

	lock_votable(votable);

	client_id = get_client_id(votable, client_str);
	if (client_id < 0) {
		rc = client_id;
		goto out;
	}

	/*
	 * for SET_ANY the val is to be ignored, set it
	 * to enabled so that the election still works based on
	 * value regardless of the type
	 */
	if (votable->type == VOTE_SET_ANY)
		val = enabled;

	if ((votable->votes[client_id].enabled == enabled) &&
		(votable->votes[client_id].value == val)) {
		chg_debug("%s: %s,%d same vote %s of val=%d\n",
				votable->name,
				client_str, client_id,
				enabled ? "on" : "off",
				val);
		similar_vote = true;
	}

	if (similar_vote && votable->voted_on) {
		chg_debug("%s: %s,%d Ignoring similar vote %s of val=%d\n",
			votable->name,
			client_str, client_id, enabled ? "on" : "off", val);
		goto out;
	}

	if (!vote_global_check(votable, votable->data, client_str, enabled, val, step))
		goto out;
	if (!vote_client_check(votable, votable->data, client_str, enabled, val, step))
		goto out;

	votable->votes[client_id].enabled = enabled;
	votable->votes[client_id].value = val;

	chg_debug("%s: %s,%d voting %s of val=%d\n",
		votable->name,
		client_str, client_id, enabled ? "on" : "off", val);
	switch (votable->type) {
	case VOTE_MIN:
		vote_min(votable, client_id, &effective_result, &effective_id, NULL);
		break;
	case VOTE_MAX:
		vote_max(votable, client_id, &effective_result, &effective_id, NULL);
		break;
	case VOTE_SET_ANY:
		vote_set_any(votable, client_id,
				&effective_result, &effective_id, NULL);
		break;
	default:
		rc = -EINVAL;
		goto out;
	}

	/*
	 * Note that the callback is called with a NULL string and -EINVAL
	 * result when there are no enabled votes
	 */
	if (!votable->voted_on ||
	    (effective_result != votable->effective_result)) {
		votable->effective_client_id = effective_id;
		votable->effective_result = effective_result;
		chg_debug("%s: effective vote is now %d voted by %s,%d\n",
			votable->name, effective_result,
			get_client_str(votable, effective_id),
			effective_id);
		if (votable->callback && !votable->force_active
				&& (votable->override_result == -EINVAL))
			rc = votable->callback(votable, votable->data,
					effective_result,
					get_client_str(votable, effective_id), step);
	} else if (effective_id != votable->effective_client_id) {
		votable->effective_client_id = effective_id;
		chg_debug("%s: effective vote is now %d voted by %s,%d, \n",
			votable->name, effective_result,
			get_client_str(votable, effective_id),
			effective_id);
	}

	votable->voted_on = true;
out:
	unlock_votable(votable);
	return rc;
}

/**
 * vote_override() -
 *
 * @votable:		The votable object
 * @override_client:	The voting client that will override other client's
 *			votes, that are already present. When force_active
 *			and override votes are set on a votable, force_active's
 *			client will have the higher priority and it's vote will
 *			be the effective one.
 * @enabled:		This provides a means for the override client to exclude
 *			itself from election. This client's vote
 *			(the next argument) will be considered only when
 *			it has enabled its participation. When this is
 *			set true, this will force a value on a MIN/MAX votable
 *			irrespective of its current value.
 * @val:		The vote value. This will be effective only if enabled
 *			is set true.
 * Returns:
 *	The result of vote. 0 is returned if the vote
 *	is successfully set by the overriding client, when enabled is set.
 */
int vote_override(struct votable *votable, const char *override_client,
		  bool enabled, int val, bool step)
{
	int rc = 0;

	if (!votable || !override_client)
		return -EINVAL;

	lock_votable(votable);
	if (votable->force_active) {
		votable->override_result = enabled ? val : -EINVAL;
		goto out;
	}

	if (enabled) {
		rc = votable->callback(votable, votable->data,
					val, override_client, step);
		if (!rc) {
			votable->override_client = override_client;
			votable->override_result = val;
		}
	} else {
		rc = votable->callback(votable, votable->data,
			votable->effective_result,
			get_client_str(votable, votable->effective_client_id), step);
		votable->override_result = -EINVAL;
	}

out:
	unlock_votable(votable);
	return rc;
}

int rerun_election(struct votable *votable, bool step)
{
	int rc = 0;
	int effective_result;

	if (!votable)
		return -EINVAL;

	lock_votable(votable);
	if (votable->force_active) {
		if (votable->callback)
			rc = votable->callback(votable, votable->data,
				votable->force_val, DEBUG_FORCE_CLIENT, false);
	} else {
		effective_result = get_effective_result_locked(votable);
		if (votable->callback)
			rc = votable->callback(votable,
				votable->data,
				effective_result,
				get_client_str(votable, votable->effective_client_id), step);
	}
	unlock_votable(votable);
	return rc;
}

int rerun_election_unlock(struct votable *votable, bool step)
{
	int rc = 0;
	int effective_result;

	if (!votable)
		return -EINVAL;

	if (votable->force_active) {
		if (votable->callback)
			rc = votable->callback(votable, votable->data,
				votable->force_val, DEBUG_FORCE_CLIENT, false);
	} else {
		effective_result = get_effective_result_locked(votable);
		if (votable->callback)
			rc = votable->callback(votable,
				votable->data,
				effective_result,
				get_client_str(votable, votable->effective_client_id), step);
	}
	return rc;
}

static void votable_timer_check_work_func(struct work_struct *work)
{
	struct votable *votable =
		container_of(work, struct votable, timer_check_work.work);

	chg_info("[timer]: check_type=%d, step=%s\n", votable->timer_check_type,
		votable->timer_check_step ? "true" : "false");
	if (votable->timer_check_type & VOTE_CHECK_CLIENTS)
		votable_check_all_clients(votable, votable->timer_check_step);
	if (votable->timer_check_type & VOTE_CHECK_RESULT)
		votable_check_result(votable, votable->timer_check_step);

	if (votable->timer_check_interval_ms > 0)
		schedule_delayed_work(&votable->timer_check_work,
			msecs_to_jiffies(votable->timer_check_interval_ms));
}

struct votable *find_votable(const char *name)
{
	unsigned long flags;
	struct votable *v;
	bool found = false;

	if (!name)
		return NULL;

	spin_lock_irqsave(&votable_list_slock, flags);
	if (list_empty(&votable_list))
		goto out;

	list_for_each_entry(v, &votable_list, list) {
		if (strcmp(v->name, name) == 0) {
			found = true;
			break;
		}
	}
out:
	spin_unlock_irqrestore(&votable_list_slock, flags);

	if (found)
		return v;
	else
		return NULL;
}

static ssize_t votable_status_read(struct file *file, char __user *buff,
				   size_t count, loff_t *off)
{
	struct votable *votable = pde_data(file_inode(file));
	int i;
	char *type_str = "Unkonwn";
	const char *effective_client_str;
	char *status_buf;
	size_t len = 0;
	ssize_t rc;

	status_buf = (char *)get_zeroed_page(GFP_KERNEL);
	if (!status_buf)
		return -ENOMEM;

	lock_votable(votable);

	for (i = 0; i < votable->num_clients; i++) {
		if (votable->client_strs[i]) {
			len += snprintf(status_buf + len, PAGE_SIZE - len,
					"%s: %s:\t\t\ten=%d v=%d\n",
					votable->name, votable->client_strs[i],
					votable->votes[i].enabled,
					votable->votes[i].value);
		}
	}

	switch (votable->type) {
	case VOTE_MIN:
		type_str = "Min";
		break;
	case VOTE_MAX:
		type_str = "Max";
		break;
	case VOTE_SET_ANY:
		type_str = "Set_any";
		break;
	default:
		chg_err("unknown votable type, type=%d\n", votable->type);
		return -EINVAL;
	}

	effective_client_str = get_effective_client_locked(votable);
	len += snprintf(status_buf + len, PAGE_SIZE - len,
			"%s: effective=%s type=%s v=%d\n", votable->name,
			effective_client_str ? effective_client_str : "none",
			type_str, get_effective_result_locked(votable));
	unlock_votable(votable);

	rc = simple_read_from_buffer(buff, count, off, status_buf,
				     (len < PAGE_SIZE ? len : PAGE_SIZE));
	free_page((unsigned long)status_buf);

	return rc;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
static const struct file_operations votable_status_ops =
{
	.write  = NULL,
	.read = votable_status_read,
	.owner = THIS_MODULE,
};
#else
static const struct proc_ops votable_status_ops =
{
	.proc_write  = NULL,
	.proc_read  = votable_status_read,
};
#endif

static ssize_t votable_force_active_write(struct file *file,
					  const char __user *buff, size_t len,
					  loff_t *data)
{
	struct votable *votable = pde_data(file_inode(file));
	char buf[128] = { 0 };
	u32 val;
	ssize_t rc = len;
	int effective_result;
	const char *client;

	if (len > ARRAY_SIZE(buf) - 1)
		return -EFAULT;

	if (copy_from_user(buf, buff, len))
		return -EFAULT;

	if (kstrtou32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	lock_votable(votable);
	votable->force_active = !!val;

	if (!votable->callback)
		goto out;

	if (votable->force_active) {
		rc = votable->callback(votable, votable->data,
				       votable->force_val, DEBUG_FORCE_CLIENT,
				       false);
	} else {
		if (votable->override_result != -EINVAL) {
			effective_result = votable->override_result;
			client = votable->override_client;
		} else {
			effective_result = votable->effective_result;
			client = get_client_str(votable,
						votable->effective_client_id);
		}
		rc = votable->callback(votable, votable->data, effective_result,
				       client, false);
	}
out:
	unlock_votable(votable);
	return rc < 0 ? rc : len;
}

static ssize_t votable_force_active_read(struct file *file, char __user *buff,
					 size_t count, loff_t *off)
{
	struct votable *votable = pde_data(file_inode(file));
	char buf[128] = { 0 };
	size_t len;
	ssize_t rc;

	len = snprintf(buf, ARRAY_SIZE(buf) - 1, "%d\n", votable->force_active);

	rc = simple_read_from_buffer(buff, count, off, buf,
				     (len < ARRAY_SIZE(buf) ? len : ARRAY_SIZE(buf)));

	return rc;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
static const struct file_operations votable_force_active_ops =
{
	.write  = votable_force_active_write,
	.read = votable_force_active_read,
	.owner = THIS_MODULE,
};
#else
static const struct proc_ops votable_force_active_ops =
{
	.proc_write  = votable_force_active_write,
	.proc_read  = votable_force_active_read,
};
#endif

static ssize_t votable_force_val_write(struct file *file,
					  const char __user *buff, size_t len,
					  loff_t *data)
{
	struct votable *votable = pde_data(file_inode(file));
	char buf[128] = { 0 };
	u32 val;

	if (len > ARRAY_SIZE(buf) - 1)
		return -EFAULT;

	if (copy_from_user(buf, buff, len))
		return -EFAULT;

	if (kstrtou32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	lock_votable(votable);
	votable->force_val = val;
	unlock_votable(votable);

	return len;
}

static ssize_t votable_force_val_read(struct file *file, char __user *buff,
				      size_t count, loff_t *off)
{
	struct votable *votable = pde_data(file_inode(file));
	char buf[128] = { 0 };
	size_t len;
	ssize_t rc;

	len = snprintf(buf, ARRAY_SIZE(buf) - 1, "%u\n", votable->force_val);

	rc = simple_read_from_buffer(buff, count, off, buf,
				     (len < ARRAY_SIZE(buf) ? len : ARRAY_SIZE(buf)));

	return rc;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
static const struct file_operations votable_force_val_ops =
{
	.write  = votable_force_val_write,
	.read = votable_force_val_read,
	.owner = THIS_MODULE,
};
#else
static const struct proc_ops votable_force_val_ops =
{
	.proc_write  = votable_force_val_write,
	.proc_read  = votable_force_val_read,
};
#endif

static ssize_t votable_clients_read(struct file *file, char __user *buff,
				    size_t count, loff_t *off)
{
	struct votable *votable = pde_data(file_inode(file));
	int i;
	char *status_buf;
	size_t len = 0;
	ssize_t rc;

	status_buf = (char *)get_zeroed_page(GFP_KERNEL);
	if (!status_buf)
		return -ENOMEM;

	lock_votable(votable);
	len += scnprintf(status_buf + len, PAGE_SIZE - len,
			"[votable]%s%s:\n", votable->name, list_empty(&votable->check_list) ? "" : "[C]");

	for (i = 0; i < votable->num_clients; i++) {
		if (votable->client_strs[i]) {
			len += scnprintf(status_buf + len, PAGE_SIZE - len,
					"%s%s\n",
					votable->client_strs[i],
					votable->votes[i].check_func ? "[C]" : "");
		}
	}
	unlock_votable(votable);

	rc = simple_read_from_buffer(buff, count, off, status_buf,
				     (len < PAGE_SIZE ? len : PAGE_SIZE));
	free_page((unsigned long)status_buf);

	return rc;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
static const struct file_operations votable_clients_ops =
{
	.write  = NULL,
	.read = votable_clients_read,
	.owner = THIS_MODULE,
};
#else
static const struct proc_ops votable_clients_ops =
{
	.proc_write  = NULL,
	.proc_read  = votable_clients_read,
};
#endif

static ssize_t votable_check_points_read(struct file *file, char __user *buff,
					 size_t count, loff_t *off)
{
	struct votable *votable = pde_data(file_inode(file));
	struct event_check_point *point;
	char *status_buf;
	size_t len = 0;
	ssize_t rc;

	status_buf = (char *)get_zeroed_page(GFP_KERNEL);
	if (!status_buf)
		return -ENOMEM;

	if (votable->timer_check_interval_ms > 0) {
		len += scnprintf(status_buf + len, PAGE_SIZE - len,
				"[timer]%s: interval_ms=%lu, step=%s\n",
				votable->name, votable->timer_check_interval_ms,
				votable->timer_check_step ? "true" : "false");
	}

	mutex_lock(&votable->event_check_point_list_lock);
	list_for_each_entry(point, &votable->event_check_point_list, list) {
		len += scnprintf(status_buf + len, PAGE_SIZE - len,
			"[event]%s: topic=%s, item_id=%u, type=%d, delayed_ms=%lu, step=%s, enable=%s\n",
			point->name, point->topic->desc->name, point->item_id,
			point->check_type, point->delayed_ms, point->step ? "true" : "false",
			point->check_enabled ? "condition_check" : "always_on");
	}
	mutex_unlock(&votable->event_check_point_list_lock);

	rc = simple_read_from_buffer(buff, count, off, status_buf,
				     (len < PAGE_SIZE ? len : PAGE_SIZE));
	free_page((unsigned long)status_buf);

	return rc;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
static const struct file_operations votable_check_points_ops =
{
	.write  = NULL,
	.read = votable_check_points_read,
	.owner = THIS_MODULE,
};
#else
static const struct proc_ops votable_check_points_ops =
{
	.proc_write  = NULL,
	.proc_read  = votable_check_points_read,
};
#endif

#if IS_ENABLED(CONFIG_OPLUS_DEBUG_AUTH)
static ssize_t votable_vote_write(struct file *file,
	const char __user *buff, size_t len,
	loff_t *data)
{
	struct votable *votable = pde_data(file_inode(file));
	char buf[256] = { 0 };
	char client[256] = { 0 };
	int enabled;
	int skip_check;
	int val;
	int step;
	int rc = 0;
	const char *data_buf;
	ssize_t data_size;

	if (len >= ARRAY_SIZE(buf))
		return -EFAULT;

	if (copy_from_user(buf, buff, len))
		return -EFAULT;

	data_size = oplus_debug_auth_get_data(buf, len, &data_buf);
	if (data_size < 0) {
		chg_err("get data error, rc=%zd\n", data_size);
		return data_size;
	}

	if (sscanf(data_buf, "%d %d %d %d %s", &skip_check, &enabled, &val, &step, client) != 5) {
		chg_err("buf error\n");
		return -EINVAL;
	}
	chg_info("votable[%s]: skip_check[%d], client[%s], val[%d], step[%d]\n",
		 votable->name, skip_check, client, val, step);

	votable->skip_check = !!skip_check;
	rc = vote(votable, client, !!enabled, val, !!step);
	if (rc < 0) {
		votable->skip_check = false;
		chg_err("vote failed\n");
		return rc;
	}
	votable->skip_check = false;

	return len;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
static const struct file_operations votable_vote_ops =
{
	.write  = votable_vote_write,
	.owner = THIS_MODULE,
};
#else
static const struct proc_ops votable_vote_ops =
{
	.proc_write  = votable_vote_write,
};
#endif
#endif /* IS_ENABLED(CONFIG_OPLUS_DEBUG_AUTH) */

static int votable_add_debug_files(struct votable *votable)
{
	votable->root = proc_mkdir(votable->name, debug_root);
	if (!votable->root) {
		chg_err("Couldn't create debug dir %s\n", votable->name);
		return -ENOMEM;
	}

	votable->status_ent =
		proc_create_data("status", S_IFREG | 0444, votable->root,
				 &votable_status_ops, votable);
	if (!votable->status_ent)
		chg_err("Couldn't create status dbg file for %s\n", votable->name);

	votable->force_val_ent =
		proc_create_data("force_val", S_IFREG | 0644, votable->root,
				 &votable_force_val_ops, votable);
	if (!votable->force_val_ent)
		chg_err("Couldn't create force_val dbg file for %s\n", votable->name);

	votable->force_active_ent =
		proc_create_data("force_active", S_IFREG | 0444, votable->root,
				 &votable_force_active_ops, votable);
	if (!votable->force_active_ent)
		chg_err("Couldn't create force_active dbg file for %s\n", votable->name);

	votable->clients_ent =
		proc_create_data("clients", S_IFREG | 0444, votable->root,
				 &votable_clients_ops, votable);
	if (!votable->clients_ent)
		chg_err("Couldn't create clients dbg file for %s\n", votable->name);

	votable->check_points_ent =
		proc_create_data("check_points", S_IFREG | 0444, votable->root,
				 &votable_check_points_ops, votable);
	if (!votable->check_points_ent)
		chg_err("Couldn't create check_points dbg file for %s\n", votable->name);

#if IS_ENABLED(CONFIG_OPLUS_DEBUG_AUTH)
	votable->vote_ent =
		proc_create_data("vote", S_IFREG | 0200, votable->root,
				 &votable_vote_ops, votable);
	if (!votable->vote_ent)
		chg_err("Couldn't create vote_ent dbg file for %s\n", votable->name);
#endif

	return 0;
}

struct votable *create_votable(const char *name,
				int votable_type,
				int (*callback)(struct votable *votable,
					void *data,
					int effective_result,
					const char *effective_client, bool step),
				void *data)
{
	struct votable *votable;
	unsigned long flags;
	int rc;

	if (!name)
		return ERR_PTR(-EINVAL);

	votable = find_votable(name);
	if (votable)
		return ERR_PTR(-EEXIST);

	if (debug_root == NULL) {
		debug_root = proc_mkdir("oplus-votable", NULL);
		if (!debug_root) {
			chg_err("Couldn't create debug dir\n");
			return ERR_PTR(-ENOMEM);
		}
	}

	if (votable_type >= NUM_VOTABLE_TYPES) {
		chg_err("Invalid votable_type specified for voter\n");
		return ERR_PTR(-EINVAL);
	}

	votable = kzalloc(sizeof(struct votable), GFP_KERNEL);
	if (!votable)
		return ERR_PTR(-ENOMEM);

	votable->name = kstrdup(name, GFP_KERNEL);
	if (!votable->name) {
		kfree(votable);
		return ERR_PTR(-ENOMEM);
	}

	votable->num_clients = NUM_MAX_CLIENTS;
	votable->callback = callback;
	votable->type = votable_type;
	votable->data = data;
	votable->override_result = -EINVAL;
	mutex_init(&votable->vote_lock);
	hash_init(votable->hash_table);
	mutex_init(&votable->check_list_lock);
	INIT_LIST_HEAD(&votable->check_list);
	mutex_init(&votable->event_check_point_list_lock);
	INIT_LIST_HEAD(&votable->event_check_point_list);
	INIT_DELAYED_WORK(&votable->timer_check_work, votable_timer_check_work_func);

	/*
	 * Because effective_result and client states are invalid
	 * before the first vote, initialize them to -EINVAL
	 */
	votable->effective_result = -EINVAL;
	if (votable->type == VOTE_SET_ANY)
		votable->effective_result = 0;
	votable->effective_client_id = -EINVAL;

	spin_lock_irqsave(&votable_list_slock, flags);
	list_add(&votable->list, &votable_list);
	spin_unlock_irqrestore(&votable_list_slock, flags);

	rc = votable_add_debug_files(votable);
	if (rc < 0) {
		if (votable->root)
			remove_proc_entry(votable->name, debug_root);
		kfree(votable->name);
		kfree(votable);
		return ERR_PTR(rc);
	}

	return votable;
}

int votable_add_map(struct votable *votable, int original, int mapped)
{
	struct vote_val_map *map;
	struct vote_val_map *tmp;

	if (votable == NULL) {
		chg_err("votable is NULL\n");
		return -EINVAL;
	}

	hash_for_each_possible(votable->hash_table, tmp, node, original) {
		if (tmp->original == original) {
			chg_err("duplicate mapping: %d -> %d\n", original, mapped);
			return 0;
		}
	}

	map = kzalloc(sizeof(struct vote_val_map), GFP_KERNEL);
	if (map == NULL) {
		chg_err("alloc map buf error\n");
		return -EINVAL;
	}

	map->original = original;
	map->mapped = mapped;
	INIT_HLIST_NODE(&map->node);
	hash_add(votable->hash_table, &map->node, map->original);

	return 0;
}

/**
 * votable_add_check_func() -
 * Register global check callback function for votable
 * @votable: votable object
 * @func: check callback function pointer
 *
 * Returns:
 * 0 on success, negative value on failure
 */
int votable_add_check_func(struct votable *votable,
	int (*func)(struct votable *votable,
		void *data, const char *client_str,
		bool enabled, int val, bool step))
{
	struct vote_check_func *info;

	if (votable == NULL) {
		chg_err("votable is NULL\n");
		return -EINVAL;
	}
	if (func == NULL) {
		chg_err("func is NULL\n");
		return -EINVAL;
	}

	info = kzalloc(sizeof(struct vote_check_func), GFP_KERNEL);
	if (info == NULL) {
		chg_err("alloc vote_check_func struct buffer error\n");
		return -ENOMEM;
	}

	info->check = func;
	mutex_lock(&votable->check_list_lock);
	list_add(&info->list, &votable->check_list);
	mutex_unlock(&votable->check_list_lock);

	return 0;
}

/**
 * votable_remove_check_func() -
 * Remove specified global check callback function from votable
 * @votable: votable object
 * @func: check function pointer to remove
 *
 * Returns:
 * 0 on success, negative value on failure
 */
int votable_remove_check_func(struct votable *votable,
	int (*func)(struct votable *votable,
		void *data, const char *client_str,
		bool enabled, int val, bool step))
{
	struct vote_check_func *info, *tmp;

	if (votable == NULL) {
		chg_err("votable is NULL\n");
		return -EINVAL;
	}
	if (func == NULL) {
		chg_err("func is NULL\n");
		return -EINVAL;
	}

	mutex_lock(&votable->check_list_lock);
	list_for_each_entry_safe(info, tmp, &votable->check_list, list) {
		if (info->check == func) {
			list_del(&info->list);
			kfree(info);
			mutex_unlock(&votable->check_list_lock);
			return 0;
		}
	}
	mutex_unlock(&votable->check_list_lock);

	chg_err("check function not found\n");
	return -ENOENT;
}

/**
 * votable_add_client_check_func() -
 * Register check callback function for specified client
 * @votable: votable object
 * @client_str: client name
 * @data: private data pointer
 * @func: check callback function pointer
 *
 * Returns:
 * 0 on success, negative value on failure
 */
int votable_add_client_check_func(struct votable *votable, const char *client_str,
	void *data,
	int (*func)(struct votable *votable, void *data,
		    const char *client_str, bool enabled, int val, bool step))
{
	int client_id;

	if (votable == NULL) {
		chg_err("votable is NULL\n");
		return -EINVAL;
	}
	if (client_str == NULL) {
		chg_err("client_str is NULL\n");
		return -EINVAL;
	}
	if (func == NULL) {
		chg_err("func is NULL\n");
		return -EINVAL;
	}

	lock_votable(votable);

	client_id = get_client_id(votable, client_str);
	if (client_id < 0) {
		unlock_votable(votable);
		chg_err("client[%s] not found\n", client_str);
		return -ENOENT;
	}

	votable->votes[client_id].check_func = func;
	votable->votes[client_id].data = data;

	unlock_votable(votable);

	chg_debug("%s: added check function for client[%s]\n", votable->name, client_str);
	return 0;
}

/**
 * votable_remove_client_check_func() -
 * Remove check callback function for specified client
 * @votable: votable object
 * @client_str: client name
 *
 * Returns:
 * 0 on success, negative value on failure
 */
int votable_remove_client_check_func(struct votable *votable, const char *client_str)
{
	int client_id;

	if (votable == NULL) {
		chg_err("votable is NULL\n");
		return -EINVAL;
	}
	if (client_str == NULL) {
		chg_err("client_str is NULL\n");
		return -EINVAL;
	}

	lock_votable(votable);

	client_id = get_client_id(votable, client_str);
	if (client_id < 0) {
		unlock_votable(votable);
		chg_err("client[%s] not found\n", client_str);
		return -ENOENT;
	}

	votable->votes[client_id].check_func = NULL;
	votable->votes[client_id].data = NULL;

	unlock_votable(votable);

	chg_debug("%s: removed check function for client[%s]\n", votable->name, client_str);
	return 0;
}

int votable_add_callback_result(struct votable *votable,
	int (*func)(struct votable *votable, void *data, int *result))
{
	if (votable == NULL) {
		chg_err("votable is NULL\n");
		return -EINVAL;
	}
	if (func == NULL) {
		chg_err("func is NULL\n");
		return -EINVAL;
	}

	lock_votable(votable);
	votable->callback_result = func;
	unlock_votable(votable);

	return 0;
}

static void votable_check_work_func(struct work_struct *work)
{
	struct event_check_point *point =
		container_of(work, struct event_check_point, check_work.work);

	if (point->votable == NULL) {
		chg_err("votable is NULL\n");
		return;
	}

	chg_info("%s[[event]%s]: check_type=%d, step=%s\n", point->votable->name,
		point->name, point->check_type, point->step ? "true" : "false");
	if (point->check_type & VOTE_CHECK_CLIENTS)
		votable_check_all_clients(point->votable, point->step);
	if (point->check_type & VOTE_CHECK_RESULT)
		votable_check_result(point->votable, point->step);
}

static void votable_subs_callback(struct mms_subscribe *subs,
	enum mms_msg_type type, u32 id, bool sync)
{
	struct event_check_point *point = subs->priv_data;
	union mms_msg_data data = { 0 };
	int rc;

	if (type != MSG_TYPE_ITEM)
		return;
	if (id != point->item_id)
		return;
	rc = oplus_mms_get_item_data(point->topic, id, &data, false);
	if (rc < 0) {
		chg_err("failed to get item[%d] data for %s\n", id, point->topic->desc->name);
		return;
	}

	if (point->check_enabled == NULL || point->check_enabled(&data))
		schedule_delayed_work(&point->check_work,
			msecs_to_jiffies(point->delayed_ms));
}

static bool event_check_point_desc_verify(struct event_check_point_desc *desc)
{
	if (desc == NULL) {
		chg_err("desc is NULL\n");
		return false;
	}
	if (desc->name == NULL) {
		chg_err("name is NULL\n");
		return false;
	}
	if (desc->topic == NULL) {
		chg_err("topic is NULL\n");
		return false;
	}
	if (desc->check_enabled == NULL) {
		chg_err("check_enabled is NULL\n");
		return false;
	}
	if (desc->check_type == 0) {
		chg_err("check_type is 0\n");
		return false;
	}
	return true;
}

struct event_check_point *votable_add_event_check_point(
	struct votable *votable,
	struct event_check_point_desc *desc)
{
	struct event_check_point *point;

	if (votable == NULL) {
		chg_err("votable is NULL\n");
		return NULL;
	}
	if (!event_check_point_desc_verify(desc))
		return NULL;

	point = kzalloc(sizeof(struct event_check_point), GFP_KERNEL);
	if (point == NULL) {
		chg_err("alloc event_check_point struct buffer error\n");
		return NULL;
	}

	point->name = kstrdup(desc->name, GFP_KERNEL);
	if (point->name == NULL) {
		chg_err("alloc event_check_point name error\n");
		kfree(point);
		return NULL;
	}

	point->votable = votable;
	point->topic = desc->topic;
	point->item_id = desc->item_id;
	point->check_enabled = desc->check_enabled;
	point->check_type = desc->check_type;
	point->step = desc->step;
	point->delayed_ms = desc->delayed_ms;
	INIT_LIST_HEAD(&point->list);
	INIT_DELAYED_WORK(&point->check_work, votable_check_work_func);

	point->subs = oplus_mms_subscribe(desc->topic, point, votable_subs_callback,
		"%s-%d", votable->name, votable->event_check_point_count);
	if (point->subs == NULL) {
		chg_err("failed to subscribe to %s\n", desc->topic->desc->name);
		kfree(point->name);
		kfree(point);
		return NULL;
	}

	mutex_lock(&votable->event_check_point_list_lock);
	list_add(&point->list, &votable->event_check_point_list);
	votable->event_check_point_count++;
	mutex_unlock(&votable->event_check_point_list_lock);

	return point;
}

void votable_remove_event_check_point(struct event_check_point *point)
{
	if (point == NULL) {
		chg_err("point is NULL\n");
		return;
	}
	if (point->votable == NULL) {
		chg_err("votable is NULL\n");
		return;
	}

	cancel_delayed_work_sync(&point->check_work);
	oplus_mms_unsubscribe(point->subs);
	mutex_lock(&point->votable->event_check_point_list_lock);
	list_del(&point->list);
	if (point->votable->event_check_point_count > 0)
		point->votable->event_check_point_count--;
	mutex_unlock(&point->votable->event_check_point_list_lock);
	kfree(point->name);
	kfree(point);
}

int votable_add_timer_check_point(struct votable *votable,
	int interval_ms, enum votable_check_type check_type, bool step)
{
	if (votable == NULL) {
		chg_err("votable is NULL\n");
		return -EINVAL;
	}
	if (interval_ms <= 0)
		return -EINVAL;

	votable->timer_check_interval_ms = interval_ms;
	votable->timer_check_type = check_type;
	votable->timer_check_step = step;

	schedule_delayed_work(&votable->timer_check_work, msecs_to_jiffies(interval_ms));

	return 0;
}

void votable_remove_timer_check_point(struct votable *votable)
{
	if (votable == NULL) {
		chg_err("votable is NULL\n");
		return;
	}

	votable->timer_check_interval_ms = 0;
	votable->timer_check_type = 0;
	votable->timer_check_step = false;

	cancel_delayed_work_sync(&votable->timer_check_work);
}

void destroy_votable(struct votable *votable)
{
	unsigned long flags;
	int i;
	struct hlist_node *tmp;
	struct vote_val_map *map;
	struct vote_check_func *func, *tmp_func;
	struct event_check_point *point, *tmp_point;

	if (!votable)
		return;

	spin_lock_irqsave(&votable_list_slock, flags);
	list_del(&votable->list);
	spin_unlock_irqrestore(&votable_list_slock, flags);

	remove_proc_entry(votable->name, debug_root);

	for (i = 0; i < votable->num_clients && votable->client_strs[i]; i++)
		kfree(votable->client_strs[i]);

	hash_for_each_safe(votable->hash_table, i, tmp, map, node) {
		hash_del(&map->node);
		kfree(map);
	}
	cancel_delayed_work_sync(&votable->timer_check_work);

	mutex_lock(&votable->check_list_lock);
	list_for_each_entry_safe(func, tmp_func, &votable->check_list, list) {
		list_del(&func->list);
		kfree(func);
	}
	mutex_unlock(&votable->check_list_lock);

	mutex_lock(&votable->event_check_point_list_lock);
	list_for_each_entry_safe(point, tmp_point, &votable->event_check_point_list, list) {
		list_del(&point->list);
		cancel_delayed_work_sync(&point->check_work);
		oplus_mms_unsubscribe(point->subs);
		kfree(point->name);
		kfree(point);
	}
	votable->event_check_point_count = 0;
	mutex_unlock(&votable->event_check_point_list_lock);

	kfree(votable->name);
	kfree(votable);
}
