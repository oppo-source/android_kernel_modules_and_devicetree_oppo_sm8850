/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Oplus. All rights reserved.
 */

#ifndef _SA_HMBIRD_H_
#define _SA_HMBIRD_H_

bool is_hmbird_enabled(void);
void hmbird_update_ux_state_locked(struct task_struct *p, int ux_state);

#endif /* _SA_HMBIRD_H_ */
