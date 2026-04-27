#ifndef _HMBIRD_II_EXPORT_H_
#define _HMBIRD_II_EXPORT_H_
#include "../../kernel/sched/sched.h"

#define SCX_RQ_BYPASS_HOOK		(1 << 18)

#define scx_enabled()	static_branch_unlikely(&__scx_ops_enabled)
static inline bool hmbird_bypass_hooks(void)
{
	return scx_enabled();
}

static inline unsigned int hmbird_flt_get_mode(void)
{
	return 0;
}

#endif /*_HMBIRD_II_EXPORT_H_*/
