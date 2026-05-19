/*****************************************************************************
* Copyright (c) 2020-2030 OPLUS Mobile Comm Corp.,Ltd. All Rights Reserved.
*
* File          : fp_event_notify.h
* Version       : 1.0
* Description   : add fp_notify_driver.
* Date          : 2025-10-30
* Author        : zhoubo
** ----------------------------Rivision History------------------------------
**  <version>      <date>       <author>       <desc>
**     1.0       2025-10-30      zhoubo     create the file
******************************************************************************/

#ifndef _FP_EVENT_NOTIFY_H
#define _FP_EVENT_NOTIFY_H

#include <linux/notifier.h>

#define FP_EVENT_ACTION_READ_LIGHT_REG_VERIFY 0x01

/* caller API */
int fp_event_register_notifier(struct notifier_block *nb);
int fp_event_unregister_notifier(struct notifier_block *nb);

/* callee API */
void fp_event_call_notifier(unsigned long action, void *data);

#endif /* _FP_EVENT_NOTIFY_H */
