// SPDX-License-Identifier: GPL-2.0-only
#include <linux/sched.h>
#include <linux/sched/cputime.h>
#include "sched.h"

int sched_set_sync_chain(pid_t pid)
{
	struct rq_flags rf;
	struct rq *rq;
	struct task_struct *task;

	rcu_read_lock();
	if (pid == 0) {
		task = current;
	} else {
		task = find_task_by_vpid(pid);
		if (!task) {
			rcu_read_unlock();
			return -ESRCH;
		}
	}
	get_task_struct(task);
	rcu_read_unlock();

	rq = task_rq_lock(task, &rf);

	trace_printk("Setting task ask=%d (set=%d) sync_chain flag", pid, task->pid);
	task->sync_chain = true;

	task_rq_unlock(rq, task, &rf);

	put_task_struct(task);
	return 0;
}
