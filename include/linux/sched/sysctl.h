/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SCHED_SYSCTL_H
#define _LINUX_SCHED_SYSCTL_H

#include <linux/types.h>

struct ctl_table;

#ifdef CONFIG_DETECT_HUNG_TASK
/* used for hung_task and block/ */
extern unsigned long sysctl_hung_task_timeout_secs;
#else
/* Avoid need for ifdefs elsewhere in the code */
enum { sysctl_hung_task_timeout_secs = 0 };
#endif

extern unsigned int sysctl_rcu_lazy;
extern unsigned int sysctl_rcu_lazy_jiffies;

enum sched_tunable_scaling {
	SCHED_TUNABLESCALING_NONE,
	SCHED_TUNABLESCALING_LOG,
	SCHED_TUNABLESCALING_LINEAR,
	SCHED_TUNABLESCALING_END,
};

extern unsigned int sysctl_iowait_reset_ticks;
extern unsigned int sysctl_iowait_apply_ticks;

#define NUMA_BALANCING_DISABLED		0x0
#define NUMA_BALANCING_NORMAL		0x1
#define NUMA_BALANCING_MEMORY_TIERING	0x2

#ifdef CONFIG_NUMA_BALANCING
extern int sysctl_numa_balancing_mode;
#else
#define sysctl_numa_balancing_mode	0
#endif

int sysctl_numa_balancing(struct ctl_table *table, int write, void *buffer,
		size_t *lenp, loff_t *ppos);

#endif /* _LINUX_SCHED_SYSCTL_H */
