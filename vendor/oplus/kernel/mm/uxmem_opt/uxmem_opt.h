// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2022 Oplus. All rights reserved.
 */

#ifndef _UXMEM_OPT_H
#define _UXMEM_OPT_H

#include <linux/device.h>
#include <linux/kref.h>
#include <linux/mm_types.h>
#include <linux/mutex.h>
#include <linux/shrinker.h>
#include <linux/types.h>

/* #define UXPAGEPOOL_DEBUG 1 */

enum POOL_MIGRATETYPE {
	POOL_MIGRATETYPE_UNMOVABLE,
	POOL_MIGRATETYPE_MOVABLE,
	POOL_MIGRATETYPE_TYPES_SIZE
};
struct page_pool {
	int low;
	int high;
	int count;
	struct list_head items;
	spinlock_t lock;
	unsigned int order;
	gfp_t gfp_mask;
};
#endif /* _UXMEM_OPT_H */
