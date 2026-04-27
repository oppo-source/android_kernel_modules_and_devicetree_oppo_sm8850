// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/module.h>
#include <linux/suspend.h>
#include <crypto/aead.h>
#include <IClientEnv.h>
#include <hibernate_tzdata_mgr.h>
#include <smcinvoke_object.h>
#include <linux/types.h>
#include <linux/timekeeping.h>

int hibernate_key_mgr_set_state(uint8_t state)
{
	int ret = 0;
	struct Object client_env = {0};
	struct Object key_mgr_object = {0};
	size_t data_size = 0;
	struct timespec64 ts = {0};
	int64_t timestamp = 0;

	ktime_get_real_ts64(&ts);
	timestamp = (int64_t)ts.tv_sec;

	pr_debug("%s: UTC Time: %lld seconds\n", __func__, timestamp);

	ret =  get_client_env_object(&client_env);
	if (ret) {
		pr_err("%s: get_client_env_object failed, ret: %d\n", __func__, ret);
		return ret;
	}

	ret = IClientEnv_open(client_env, CHibernateTzDataMgr_UID, &key_mgr_object);
	if (ret) {
		pr_err("%s: IClientEnv_open failed, ret: %d\n", __func__, ret);
		goto free_client_env;
	}

	ret = IHibernateTzDataMgr_setHibernateState(key_mgr_object, NULL, 0, NULL, 0,
						    &data_size, timestamp, state);
	if (ret)
		pr_err("%s: setHibernateState failed, ret: %d\n", __func__, ret);

	Object_release(key_mgr_object);
free_client_env:
	Object_release(client_env);

	return ret;
}


static int hibernate_pm_notifier(struct notifier_block *nb,
				unsigned long event, void *unused)
{
	switch (event) {
	case PM_HIBERNATION_PREPARE:
		hibernate_key_mgr_set_state(IHibernateTzDataMgr_STATE_HIBERNATE_ENTRY);
		break;
	case PM_POST_HIBERNATION:
		break;
	case PM_RESTORE_PREPARE:
		hibernate_key_mgr_set_state(IHibernateTzDataMgr_STATE_HIBERNATE_EXIT);
		break;
	case PM_POST_RESTORE:
		break;
	default:
		WARN_ONCE(1, "Invalid PM Notifier\n");
		break;
	}

	return NOTIFY_DONE;
}

static struct notifier_block pm_nb = {
	.notifier_call = hibernate_pm_notifier,
};

static int __init hibernate_tzdata_mgr_init(void)
{
	int ret = 0;

	pr_info("hibernate_tzdata_mgr init called\n");
	ret = register_pm_notifier(&pm_nb);
	if (ret) {
		pr_err("%s: Failed to register nb: %d\n", __func__, ret);
		return ret;
	}
	return ret;
}

static void __exit hibernate_tzdata_mgr_exit(void)
{
	pr_info("hibernate_tzdata_mgr exit called\n");
	unregister_pm_notifier(&pm_nb);
}

module_init(hibernate_tzdata_mgr_init);
module_exit(hibernate_tzdata_mgr_exit);

MODULE_DESCRIPTION("Framework to encrypt a page using a trusted application");
MODULE_LICENSE("GPL");
