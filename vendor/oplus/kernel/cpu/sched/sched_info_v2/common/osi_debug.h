/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022,2025 Oplus. All rights reserved.
 */

#ifndef __OSI_DEBUG_H_
#define __OSI_DEBUG_H_

#include "../osi_base.h"

struct proc_dir_entry *jank_debug_proc_init(struct proc_dir_entry *pde);
void jank_debug_proc_deinit(struct proc_dir_entry *pde);

#endif  /* __OSI_DEBUG_H_ */
