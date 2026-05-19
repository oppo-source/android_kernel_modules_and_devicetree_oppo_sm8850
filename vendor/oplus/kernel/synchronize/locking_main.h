/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020-2022 Oplus. All rights reserved.
 */

#ifndef _OPLUS_LOCKING_MAIN_H_
#define _OPLUS_LOCKING_MAIN_H_

#include <linux/sched.h>

#define cond_trace_printk(cond, fmt, ...)	\
do {										\
	if (cond)								\
		trace_printk(fmt, ##__VA_ARGS__);	\
} while (0)



#define MAGIC_NUM       (0xdead0000)
#define MAGIC_MASK      (0xffff0000)
#define MAGIC_SHIFT     (16)
#define OWNER_BIT       (1 << 0)
#define THREAD_INFO_BIT (1 << 1)
#define TYPE_BIT        (1 << 2)

#define UX_FLAG_BIT       (1<<0)
#define SS_FLAG_BIT       (1<<1)
#define GRP_SHIFT         (2)
#define GRP_FLAG_MASK     (7 << GRP_SHIFT)
#define U_GRP_OTHER       (1 << GRP_SHIFT)
#define U_GRP_BACKGROUND  (2 << GRP_SHIFT)
#define U_GRP_FRONDGROUD  (3 << GRP_SHIFT)
#define U_GRP_TOP_APP     (4 << GRP_SHIFT)

#define LOCK_TYPE_SHIFT (24)
#define INVALID_TYPE    (0)
#define LOCK_ART        (1)
#define LOCK_JUC        (2)
#define LOCK_GC_COND    (4)

#define lk_err(fmt, ...) \
		pr_err("[oplus_locking][%s]"fmt, __func__, ##__VA_ARGS__)
#define lk_warn(fmt, ...) \
		pr_warn("[oplus_locking][%s]"fmt, __func__, ##__VA_ARGS__)
#define lk_info(fmt, ...) \
		pr_info("[oplus_locking][%s]"fmt, __func__, ##__VA_ARGS__)

struct futex_uinfo {
	u32 cmd;
	u32 owner_tid;
	u32 type;
	u64 inform_user;
};

#ifdef CONFIG_LOCKING_LAST_ENTITY
enum {
	LOCKING_DEFAULT = 0,
	LOCKING_PROTECTED,
	LOCKING_SET_LAST,
	LOCKING_PICK_LAST,
	LOCKING_CLEAR_LAST,
	LOCKING_CLEAR_INELIGIBLE,
};
#endif

enum rwsem_waiter_type {
	RWSEM_WAITING_FOR_WRITE,
	RWSEM_WAITING_FOR_READ
};

struct rwsem_waiter {
	struct list_head list;
	struct task_struct *task;
	enum rwsem_waiter_type type;
	unsigned long timeout;
	bool handoff_set;
};

#define LK_MUTEX_ENABLE (1 << 0)
#define LK_RWSEM_ENABLE (1 << 1)
#define LK_FUTEX_ENABLE (1 << 2)
#define LK_OSQ_ENABLE   (1 << 3)
#define LK_PIFUTEX_ENABLE (1 << 4)
#define LK_PROTECT_ENABLE	(1 << 5)
#define LK_MONITOR_ENABLE (1 << 6)
#define LK_SCHED_ENABLE (1 << 15)
#define LK_FEATURE_MASK (LK_MUTEX_ENABLE | LK_RWSEM_ENABLE | LK_FUTEX_ENABLE | \
								LK_OSQ_ENABLE | LK_PROTECT_ENABLE | LK_MONITOR_ENABLE | LK_SCHED_ENABLE)

#ifdef CONFIG_OPLUS_LOCKING_MONITOR
/*
 * The bit definitions of the g_opt_enable:
 * bit 0-7: reserved bits for other locking optimation.
 * bit8 ~ bit10(each monitor version is exclusive):
 * 1 : monitor control, level-0(internal version).
 * 2 : monitor control, level-1(trial version).
 * 3 : monitor control, level-2(official version).
 */
#define LK_MONITOR_SHIFT  (8)
#define LK_MONITOR_MASK   (7 << LK_MONITOR_SHIFT)
#define LK_MONITOR_LEVEL0 (1 << LK_MONITOR_SHIFT)
#define LK_MONITOR_LEVEL1 (2 << LK_MONITOR_SHIFT)
#define LK_MONITOR_LEVEL2 (3 << LK_MONITOR_SHIFT)
#endif

extern unsigned int g_opt_enable;
extern unsigned int g_opt_debug;

static inline bool locking_opt_enable(unsigned int enable)
{
	return g_opt_enable & enable;
}

static inline void oplus_lk_feat_enable(unsigned int lk_feat, bool enable)
{
	lk_feat &= LK_FEATURE_MASK;
	if (enable)
		g_opt_enable |= lk_feat;
	else
		g_opt_enable &= ~lk_feat;
}

#ifdef CONFIG_OPLUS_LOCKING_MONITOR
static inline bool lock_supp_level(int level)
{
	return (g_opt_enable & LK_MONITOR_MASK) == level;
}
#endif

static inline bool locking_opt_debug(int debug)
{
	return g_opt_debug & debug;
}

void register_rwsem_vendor_hooks(void);
void register_mutex_vendor_hooks(void);
void register_futex_vendor_hooks(void);
void register_monitor_vendor_hooks(void);
void lk_sysfs_init(void);
#ifdef CONFIG_OPLUS_LOCKING_MONITOR
int kern_lstat_init(void);
#endif

void unregister_rwsem_vendor_hooks(void);
void unregister_mutex_vendor_hooks(void);
void unregister_futex_vendor_hooks(void);
void unregister_monitor_vendor_hooks(void);
void lk_sysfs_exit(void);
#ifdef CONFIG_OPLUS_LOCKING_MONITOR
void kern_lstat_exit(void);
#endif

#ifdef CONFIG_LOCKING_PROTECT
int sched_assist_locking_init(void);
#endif

int krn_reliab_init(void);

enum {
	LK_DEBUG_PRINTK = 1,
	LK_DEBUG_FTRACE = 2,
	LOG_LOCK_SYSTRACE_LVL0 = 4,
	LOG_LOCK_SYSTRACE_LVL1 = 8,
};

enum {
	STATE_FUTEX_DO_NOTHING = 1,
	STATE_FUTEX_NOT_SET_UX = 2,
	STATE_FUTEX_UX_CONTRIB = 3,
	STATE_FUTEX_SET_HOLDER = 4,
	STATE_FUTEX_NOT_SET_HOLDER_OF_TGID = 5,
	STATE_FUTEX_WAITER_UNKNOWN_WHEN_NOTIFY = 6,
	STATE_FUTEX_WAITER_NOT_UX_WHEN_NOTIFY = 7,
	STATE_FUTEX_CURR_NOT_UX_WHEN_LISTADD = 8,
	STATE_FUTEX_BOOST_HOLDER_WHEN_LISTADD = 9,
	STATE_FUTEX_NO_HOLDER_WHEN_LISTADD = 10,
	STATE_FUTEX_ALREADY_ON_HB = 11,
	STATE_FUTEX_SET_UX = 100,
	STATE_FUTEX_INC_INHERIT_UX = 101,
};

void lock_ux_state_systrace(struct task_struct *from,
	struct task_struct *waiter, struct task_struct *holder,
	int ux_state, int systrace_lvl);

#endif /* _OPLUS_LOCKING_MAIN_H_ */
