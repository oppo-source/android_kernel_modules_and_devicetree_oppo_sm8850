/*****************************************************************************
* Copyright (c) 2020-2030 OPLUS Mobile Comm Corp.,Ltd. All Rights Reserved.
*
* File          : fp_event_notify.c
* Version       : 1.0
* Description   : add fp_notify_driver.
* Date          : 2025-10-30
* Author        : zhoubo
** ----------------------------Rivision History------------------------------
**  <version>      <date>       <author>       <desc>
**     1.0       2025-10-30      zhoubo     create the file
******************************************************************************/

#include <linux/export.h>
#include <linux/module.h>
#include "fp_event_notify.h"

static BLOCKING_NOTIFIER_HEAD(fp_event_notifier_list);

int fp_event_register_notifier(struct notifier_block *nb)
{
    return blocking_notifier_chain_register(&fp_event_notifier_list, nb);
}
EXPORT_SYMBOL(fp_event_register_notifier);

int fp_event_unregister_notifier(struct notifier_block *nb)
{
    return blocking_notifier_chain_unregister(&fp_event_notifier_list, nb);
}
EXPORT_SYMBOL(fp_event_unregister_notifier);

void fp_event_call_notifier(unsigned long action, void *data)
{
    blocking_notifier_call_chain(&fp_event_notifier_list, action, data);
}
EXPORT_SYMBOL(fp_event_call_notifier);

MODULE_DESCRIPTION("Fingerprint Event Notify Driver");
MODULE_LICENSE("GPL");
