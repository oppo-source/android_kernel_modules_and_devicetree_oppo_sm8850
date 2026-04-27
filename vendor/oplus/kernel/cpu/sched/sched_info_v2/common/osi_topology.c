// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Oplus. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/completion.h>
#include <uapi/linux/sched/types.h>

#include "osi_topology.h"

struct cluster_info *cluster;
unsigned int cluster_num;
unsigned int cpu_nums;
struct cpumask all_cpu;
struct cpumask silver_cpu;
struct cpumask gold_cpu;


void update_cpu_mask(void)
{
	unsigned int cpu, min_capacity = arch_scale_cpu_capacity(0);

	for_each_possible_cpu(cpu) {
		if (arch_scale_cpu_capacity(cpu) <= min_capacity)
			min_capacity = arch_scale_cpu_capacity(cpu);
		cpumask_set_cpu(cpu, &gold_cpu);
	}
	for_each_possible_cpu(cpu) {
		if (arch_scale_cpu_capacity(cpu) == min_capacity)
			cpumask_clear_cpu(cpu, &gold_cpu);
	}
	cpumask_andnot(&silver_cpu, &all_cpu, &gold_cpu);
	pr_info("cpu_highcap_mask:%lx, %lx, %lx\n", cpumask_bits(&all_cpu)[0],
		cpumask_bits(&silver_cpu)[0], cpumask_bits(&gold_cpu)[0]);
}

void cluster_init(void)
{
	unsigned int cpu;
	unsigned int cluster_id;
	struct cpu_topology *cpu_topo;

	cpu_nums = num_possible_cpus();
	if (cpu_nums <= 0) {
		pr_err("cpu_nums is invalid: %d\n", cpu_nums);
		return;
	}

	pr_info("cpu_nums: %d\n", cpu_nums);

	cluster = kzalloc(sizeof(struct cluster_info) * cpu_nums, GFP_ATOMIC);
	if (!cluster) {
		pr_err("Failed to allocate memory for cluster_info\n");
		return;
	}

	for_each_possible_cpu(cpu) {
		cpu_topo = &cpu_topology[cpu];
		cpumask_set_cpu(cpu, &all_cpu);
		cluster_id = topology_cluster_id(cpu);
		cluster_num = max(cluster_id, cluster_num);
		if (cluster_id >= cpu_nums)
			continue;
		cluster[cluster_id].cpu_nr = cpumask_weight(&cpu_topo->cluster_sibling);
		cluster[cluster_id].start_cpu = cpumask_first(&cpu_topo->cluster_sibling);
	}
	cluster_num += 1;
	update_cpu_mask();
}

unsigned int get_start_cpu(unsigned int cpu)
{
	unsigned int start_cpu;
	struct cpu_topology *cpu_topo;

	cpu_topo = &cpu_topology[cpu];
	start_cpu = cpumask_first(&cpu_topo->cluster_sibling);
	return start_cpu;
}

unsigned int get_cluster_id(unsigned int cpu)
{
	unsigned int cluster_id;

	cluster_id = topology_cluster_id(cpu);
	return cluster_id;
}

bool is_cluster_cpu(unsigned int cpu)
{
	unsigned int start_cpu;
	struct cpu_topology *cpu_topo;

	cpu_topo = &cpu_topology[cpu];
	start_cpu = cpumask_first(&cpu_topo->cluster_sibling);
	if (start_cpu == cpu)
		return true;
	return false;
}

unsigned int cpu_nums_get(void)
{
	return cpu_nums;
}
