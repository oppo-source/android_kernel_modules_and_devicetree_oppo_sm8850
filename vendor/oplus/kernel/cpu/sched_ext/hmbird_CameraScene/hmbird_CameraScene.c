/** Copyright (C), 2019-2024, OPLUS Mobile Comm Corp., Ltd.
* Description: Hmbird camera freq boost
* Author: Gao ZhiFeng 80407967
* Create: 2025-11-17
* Notes: Hmbird camera freq boost
*/
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/bpf.h>
#include <linux/bpf_verifier.h>
#include <linux/cpumask.h>
#include <linux/mmu_context.h>
#include <linux/jump_label.h>
#include "../../kernel/sched/sched.h"
#include <trace/hooks/sched.h>
#include <linux/sched/ext.h>
#include <linux/random.h>
#include <trace/hooks/sched.h>
#include <trace/events/task.h>
#include "hmbird_CameraScene.h"
#include "hmbird_CameraBoost.h"

int hmbird_CameraScene_init(void)
{
	hmbird_CameraScene_sysctl_init();
	return 0;
}

void hmbird_CameraScene_exit(void)
{
	hmbird_CameraScene_sysctl_deinit();
}