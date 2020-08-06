// SPDX-License-Identifier: GPL-2.0+
//
// Verification of core scheduling invariants. Namely
// safe vs unsafe contexts should not concurrently share core.
//
// Copyright (C) Google, 2020.
//
// Author: Joel Fernandes <joel@joelfernandes.org>

#define pr_fmt(fmt) fmt

#include <linux/cpu.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/percpu.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/smp.h>
#include <linux/stat.h>
#include <linux/types.h>
#include <trace/events/sched.h>
#include <trace/events/context_tracking.h>
#include <trace/events/irq.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Joel Fernandes <joel@joelfernandes.org>");

enum cpu_cs_state {
	CS_IDLE,
	CS_USER,
	CS_KERNEL
};

#define COOKIE_KERNEL ((unsigned long)-1)

DEFINE_PER_CPU(unsigned long, cpu_cookie);
DEFINE_PER_CPU(enum cpu_cs_state, cpu_state);
DEFINE_PER_CPU(bool, cpu_not_idle);  /* is the cpu idle? Required to track IRQs in idle loop. */

static raw_spinlock_t lock;

static void
probe_irq_handler_entry(void *ignore, int irq, struct irqaction *ignore2)
{
	unsigned long flags;
	trace_printk("probe_irq_handler_entry\n");
	raw_spin_lock_irqsave(&lock, flags);

	/* Context-tracking should have already made us get out of user,
	 * or we should have been in the idle loop.
	 */
	WARN_ON_ONCE(this_cpu_read(cpu_state) == CS_USER);

	/* IRQs received in the idle loop don't go through context-tracking
	 * so make them transition to kernel here.
	 */
	if (!this_cpu_read(cpu_not_idle)) {
		WARN_ON_ONCE(this_cpu_read(cpu_state) != CS_IDLE);
		this_cpu_write(cpu_state, CS_KERNEL);
		this_cpu_write(cpu_cookie, COOKIE_KERNEL);
	} else {
		if (WARN_ON_ONCE(this_cpu_read(cpu_state) != CS_KERNEL)) {
			pr_err("irq entry, this_cpu_read(cpu_state) is %d\n",
			       this_cpu_read(cpu_state));
		}
	}

	/***** Additional checks on IRQ entry follow *****/

	raw_spin_unlock_irqrestore(&lock, flags);

	trace_printk("leave probe_irq_handler_entry\n");
}

static void
probe_irq_handler_exit(void *ignore, int irq, struct irqaction *ignore2, int ignore3)
{
	unsigned long flags;

	trace_printk("probe_irq_handler_exit\n");

	raw_spin_lock_irqsave(&lock, flags);

	WARN_ON_ONCE(this_cpu_read(cpu_state) != CS_KERNEL);

	/* We interrupted idle loop, set context back to idle. */
	if (!this_cpu_read(cpu_not_idle)) {
		WARN_ON_ONCE(!is_idle_task(current));
		this_cpu_write(cpu_state, CS_IDLE);
		this_cpu_write(cpu_cookie, current->core_cookie);
	} else {
		/* Context tracking will take care of setting CS_USER. */
	}

	/* No additional checking needed here, context-tracking will take care
	 * of doing entry into user-mode checks.
	 */
	raw_spin_unlock_irqrestore(&lock, flags);

	trace_printk("leave probe_irq_handler_exit\n");
}

static void
probe_user_enter(void *ignore, int ignore2)
{
	unsigned long flags;
	int i, cpu;

	trace_printk("probe_user_enter\n");

	raw_spin_lock_irqsave(&lock, flags);

	/* Entry into user should happen only from kernel. */
	WARN_ON_ONCE(this_cpu_read(cpu_state) != CS_KERNEL ||
		     this_cpu_read(cpu_cookie) != COOKIE_KERNEL);

	this_cpu_write(cpu_state, CS_USER);
	this_cpu_write(cpu_cookie, current->core_cookie);

	/* Context tracking is never called for idle task. */
	WARN_ON_ONCE(is_idle_task(current));

	/* All return to user-mode checks go here. */
	cpu = smp_processor_id();
	for_each_cpu(i, cpu_smt_mask(cpu)) {
		enum cpu_cs_state state;
		unsigned long cookie;

		if (i == cpu)
			continue;

		state = per_cpu(cpu_state, i);
		cookie = per_cpu(cpu_cookie, i);

		WARN_ON_ONCE(state == CS_IDLE && cookie != 0);
		WARN_ON_ONCE(state == CS_KERNEL && cookie != COOKIE_KERNEL);

		/* idle CPU is always compatible and cannot leak to / attack us. */
		if (state == CS_IDLE)
			continue;

		/* If current->cookie 0, then we are entering a trusted task
		 * which cannot be an attacker.  If the destination CPU is in
		 * kernel mode, then it cannot attack us so we are good.
		 */
		if (!current->core_cookie && state == CS_KERNEL)
			continue;

		/* Enforce compatiblity of task entering user mode with CPU i. */
		WARN_ON_ONCE(cookie != current->core_cookie);
		if (cookie != current->core_cookie) {
			trace_printk("cpu %d, cookie %lu current->cookie %lu, current->pid %d\n",
				i, cookie, current->core_cookie, current->pid);
			// panic("oops\n");
		}
	}

	raw_spin_unlock_irqrestore(&lock, flags);

	trace_printk("Leave probe_user_enter\n");
}

static void
probe_user_exit(void *ignore, int ignore2)
{
	unsigned long flags;

	trace_printk("probe_user_exit\n");

	raw_spin_lock_irqsave(&lock, flags);

	/* Exiting from user always enters kernel first, then enters idle loop
	 * or returns to user.
	 */
	this_cpu_write(cpu_state, CS_KERNEL);

	/* Makes other kernel mode entries compatible with this CPU. */
	this_cpu_write(cpu_cookie, COOKIE_KERNEL);

	raw_spin_unlock_irqrestore(&lock, flags);

	trace_printk("Leave probe_user_exit\n");
}

/* Called before actually context-switch, but after task selection.
 * The only job we have here is to track entry into idle.
 */
static void
probe_sched_switch(void *ignore, bool preempt,
		   struct task_struct *prev, struct task_struct *next)
{
	unsigned long flags;

	trace_printk("probe_sched_switch\n");
	raw_spin_lock_irqsave(&lock, flags);
	this_cpu_write(cpu_not_idle, !is_idle_task(next));

	/* We can be switching from either usermode or kthreads, into idle,
	 * or viceversa.
	 *
	 * When switch away from idle task to usermode, we momentarily set kernel
	 * and context tracking will set it as user before we enter usermode.
	 *
	 * NOTE: If switching from idle/kernel to usermode, we let context-tracking
	 * set it up as CS_USER and don't need to do it here.
	 */
	if (is_idle_task(next)) {
		this_cpu_write(cpu_state, CS_IDLE);
		this_cpu_write(cpu_cookie, next->core_cookie);
	} else if (is_idle_task(prev)) {
		this_cpu_write(cpu_state, CS_KERNEL);
		this_cpu_write(cpu_cookie, COOKIE_KERNEL);
	}

	raw_spin_unlock_irqrestore(&lock, flags);

	trace_printk("Leave probe_sched_switch\n");
}

static int __init core_sched_init(void)
{
	int ret;

	raw_spin_lock_init(&lock);

	ret = register_trace_sched_switch(probe_sched_switch, NULL);
	if (ret) {
		pr_info(" Couldn't activate probe.\n");
		return ret;
	}

	ret = register_trace_user_enter(probe_user_enter, NULL);
	if (ret) {
		pr_info(" Couldn't activate probe.\n");
		return ret;
	}

	ret = register_trace_user_exit(probe_user_exit, NULL);
	if (ret) {
		pr_info(" Couldn't activate probe.\n");
		return ret;
	}

	ret = register_trace_irq_handler_entry(probe_irq_handler_entry, NULL);
	if (ret) {
		pr_info(" Couldn't activate probe.\n");
		return ret;
	}

	ret = register_trace_irq_handler_exit(probe_irq_handler_exit, NULL);
	if (ret) {
		pr_info(" Couldn't activate probe.\n");
		return ret;
	}

	pr_err("coresched: Testing starts...\n");

	return 0;
}

static void core_sched_cleanup(void)
{
	unregister_trace_sched_switch(probe_sched_switch, NULL);
}

module_init(core_sched_init);
module_exit(core_sched_cleanup);
