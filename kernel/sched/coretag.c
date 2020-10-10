// SPDX-License-Identifier: GPL-2.0-only
/*
 * kernel/sched/core-tag.c
 *
 * Core-scheduling tagging interface support.
 *
 * Copyright(C) 2020, Joel Fernandes.
 * Initial interfacing code  by Peter Ziljstra.
 */

#include "sched.h"

/*
 * A simple wrapper around refcount. An allocated sched_core_cookie's
 * address is used to compute the cookie of the task.
 */
struct sched_core_cookie {
	refcount_t refcnt;
};

static DEFINE_MUTEX(sched_core_tasks_mutex);

/*
 * sched_core_tag_requeue - Common helper for all interfaces to set a cookie.
 * @p: The task to assign a cookie to.
 * @cookie: The cookie to assign.
 * @group: is it a group interface or a per-task interface.
 *
 * This function is typically called from a stop-machine handler.
 */
void sched_core_tag_requeue(struct task_struct *p, unsigned long cookie, bool group)
{
	if (!p)
		return;

	if (group)
		p->core_group_cookie = cookie;
	else
		p->core_task_cookie = cookie;

	/* Use up half of the cookie's bits for task cookie and remaining for group cookie. */
	p->core_cookie = (p->core_task_cookie <<
				(sizeof(unsigned long) * 4)) + p->core_group_cookie;

	if (sched_core_enqueued(p)) {
		sched_core_dequeue(task_rq(p), p);
		if (!p->core_cookie)
			return;
	}

	if (sched_core_enabled(task_rq(p)) &&
			p->core_cookie && task_on_rq_queued(p))
		sched_core_enqueue(task_rq(p), p);
}

/* Per-task interface: Used by fork(2) and prctl(2). */
static unsigned long sched_core_alloc_task_cookie(void)
{
	struct sched_core_cookie *ptr =
		kmalloc(sizeof(struct sched_core_cookie), GFP_KERNEL);

	if (!ptr)
		return 0;
	refcount_set(&ptr->refcnt, 1);

	/*
	 * NOTE: sched_core_put() is not done by put_task_cookie(). Instead, it
	 * is done after the stopper runs.
	 */
	sched_core_get();
	return (unsigned long)ptr;
}

static bool sched_core_get_task_cookie(unsigned long cookie)
{
	struct sched_core_cookie *ptr = (struct sched_core_cookie *)cookie;

	/*
	 * NOTE: sched_core_put() is not done by put_task_cookie(). Instead, it
	 * is done after the stopper runs.
	 */
	sched_core_get();
	return refcount_inc_not_zero(&ptr->refcnt);
}

static void sched_core_put_task_cookie(unsigned long cookie)
{
	struct sched_core_cookie *ptr = (struct sched_core_cookie *)cookie;

	if (refcount_dec_and_test(&ptr->refcnt))
		kfree(ptr);
}

struct sched_core_task_write_tag {
	struct task_struct *tasks[2];
	unsigned long cookies[2];
};

/*
 * Ensure that the task has been requeued. The stopper ensures that the task cannot
 * be migrated to a different CPU while its core scheduler queue state is being updated.
 * It also makes sure to requeue a task if it was running actively on another CPU.
 */
static int sched_core_task_join_stopper(void *data)
{
	struct sched_core_task_write_tag *tag = (struct sched_core_task_write_tag *)data;
	int i;

	for (i = 0; i < 2; i++)
		sched_core_tag_requeue(tag->tasks[i], tag->cookies[i], false /* !group */);

	return 0;
}

int sched_core_share_tasks(struct task_struct *t1, struct task_struct *t2)
{
	struct sched_core_task_write_tag wr = {}; /* for stop machine. */
	bool sched_core_put_after_stopper = false;
	unsigned long cookie;
	int ret = -ENOMEM;

	mutex_lock(&sched_core_tasks_mutex);

	/*
	 * NOTE: sched_core_get() is done by sched_core_alloc_task_cookie() or
	 *       sched_core_put_task_cookie(). However, sched_core_put() is done
	 *       by this function *after* the stopper removes the tasks from the
	 *       core queue, and not before. This is just to play it safe.
	 */
	if (t2 == NULL) {
		if (t1->core_task_cookie) {
			sched_core_put_task_cookie(t1->core_task_cookie);
			sched_core_put_after_stopper = true;
			wr.tasks[0] = t1; /* Keep wr.cookies[0] reset for t1. */
		}
	} else if (t1 == t2) {
		/* Assign a unique per-task cookie solely for t1. */

		cookie = sched_core_alloc_task_cookie();
		if (!cookie)
			goto out_unlock;

		if (t1->core_task_cookie) {
			sched_core_put_task_cookie(t1->core_task_cookie);
			sched_core_put_after_stopper = true;
		}
		wr.tasks[0] = t1;
		wr.cookies[0] = cookie;
	} else
	/*
	 * 		t1		joining		t2
	 * CASE 1:
	 * before	0				0
	 * after	new cookie			new cookie
	 *
	 * CASE 2:
	 * before	X (non-zero)			0
	 * after	0				0
	 *
	 * CASE 3:
	 * before	0				X (non-zero)
	 * after	X				X
	 *
	 * CASE 4:
	 * before	Y (non-zero)			X (non-zero)
	 * after	X				X
	 */
	if (!t1->core_task_cookie && !t2->core_task_cookie) {
		/* CASE 1. */
		cookie = sched_core_alloc_task_cookie();
		if (!cookie)
			goto out_unlock;

		/* Add another reference for the other task. */
		if (!sched_core_get_task_cookie(cookie)) {
			return -EINVAL;
			goto out_unlock;
		}

		wr.tasks[0] = t1;
		wr.tasks[1] = t2;
		wr.cookies[0] = wr.cookies[1] = cookie;

	} else if (t1->core_task_cookie && !t2->core_task_cookie) {
		/* CASE 2. */
		sched_core_put_task_cookie(t1->core_task_cookie);
		sched_core_put_after_stopper = true;

		wr.tasks[0] = t1; /* Reset cookie for t1. */

	} else if (!t1->core_task_cookie && t2->core_task_cookie) {
		/* CASE 3. */
		if (!sched_core_get_task_cookie(t2->core_task_cookie)) {
			ret = -EINVAL;
			goto out_unlock;
		}

		wr.tasks[0] = t1;
		wr.cookies[0] = t2->core_task_cookie;

	} else {
		/* CASE 4. */
		if (!sched_core_get_task_cookie(t2->core_task_cookie)) {
			ret = -EINVAL;
			goto out_unlock;
		}
		sched_core_put_task_cookie(t1->core_task_cookie);
		sched_core_put_after_stopper = true;

		wr.tasks[0] = t1;
		wr.cookies[0] = t2->core_task_cookie;
	}

	stop_machine(sched_core_task_join_stopper, (void *)&wr, NULL);

	if (sched_core_put_after_stopper)
		sched_core_put();

	ret = 0;
out_unlock:
	mutex_unlock(&sched_core_tasks_mutex);
	return ret;
}

/* Called from prctl interface: PR_SCHED_CORE_SHARE */
int sched_core_share_pid(pid_t pid)
{
	struct task_struct *task;
	int err;

	if (pid == 0) { /* Recent current task's cookie. */
		/* Resetting a cookie requires privileges. */
		if (current->core_task_cookie)
			if (!capable(CAP_SYS_ADMIN))
				return -EPERM;
		task = NULL;
	} else {
		rcu_read_lock();
		task = pid ? find_task_by_vpid(pid) : current;
		if (!task) {
			rcu_read_unlock();
			return -ESRCH;
		}

		get_task_struct(task);

		/*
		 * Check if this process has the right to modify the specified
		 * process. Use the regular "ptrace_may_access()" checks.
		 */
		if (!ptrace_may_access(task, PTRACE_MODE_READ_REALCREDS)) {
			rcu_read_unlock();
			err = -EPERM;
			goto out_put;
		}
		rcu_read_unlock();
	}

	err = sched_core_share_tasks(current, task);
out_put:
	if (task)
		put_task_struct(task);
	return err;
}

/* CGroup core-scheduling interface support. */

/*
 * Helper to get the cookie in a hierarchy.
 * The cookie is a combination of a tag and color. Any ancestor
 * can have a tag/color. tag is the first-level cookie setting
 * with color being the second. Atmost one color and one tag is
 * allowed.
 */
unsigned long cpu_core_get_group_cookie(struct task_group *tg)
{
	unsigned long color = 0;

	if (!tg)
		return 0;

	for (; tg; tg = tg->parent) {
		if (tg->core_tag_color) {
			WARN_ON_ONCE(color);
			color = tg->core_tag_color;
		}

		if (tg->core_tagged) {
			unsigned long cookie = ((unsigned long)tg << 8) | color;
			cookie &= SCHED_CORE_GROUP_COOKIE_MASK;
			return cookie;
		}
	}

	return 0;
}

/* Determine if any group in @tg's children are tagged or colored. */
static bool cpu_core_check_descendants(struct task_group *tg, bool check_tag,
				       bool check_color)
{
	struct task_group *child;

	rcu_read_lock();
	list_for_each_entry_rcu(child, &tg->children, siblings) {
		if ((child->core_tagged && check_tag) ||
		    (child->core_tag_color && check_color)) {
			rcu_read_unlock();
			return true;
		}

		rcu_read_unlock();
		return cpu_core_check_descendants(child, check_tag, check_color);
	}

	rcu_read_unlock();
	return false;
}

u64 cpu_core_tag_read_u64(struct cgroup_subsys_state *css,
			  struct cftype *cft)
{
	struct task_group *tg = css_tg(css);

	return !!tg->core_tagged;
}

u64 cpu_core_tag_color_read_u64(struct cgroup_subsys_state *css,
				struct cftype *cft)
{
	struct task_group *tg = css_tg(css);

	return tg->core_tag_color;
}

#ifdef CONFIG_SCHED_DEBUG
u64 cpu_core_group_cookie_read_u64(struct cgroup_subsys_state *css,
				   struct cftype *cft)
{
	return cpu_core_get_group_cookie(css_tg(css));
}
#endif

struct write_core_tag {
	struct cgroup_subsys_state *css;
	unsigned long cookie;
};

static int __sched_write_tag(void *data)
{
	struct write_core_tag *tag = (struct write_core_tag *) data;
	struct task_struct *p;
	struct cgroup_subsys_state *css;

	rcu_read_lock();
	css_for_each_descendant_pre(css, tag->css) {
		struct css_task_iter it;

		css_task_iter_start(css, 0, &it);
		/*
		 * Note: css_task_iter_next will skip dying tasks.
		 * There could still be dying tasks left in the core queue
		 * when we set cgroup tag to 0 when the loop is done below.
		 */
		while ((p = css_task_iter_next(&it)))
			sched_core_tag_requeue(p, tag->cookie, true /* group */);

		css_task_iter_end(&it);
	}
	rcu_read_unlock();

	return 0;
}

int cpu_core_tag_write_u64(struct cgroup_subsys_state *css, struct cftype *cft,
			   u64 val)
{
	struct task_group *tg = css_tg(css);
	struct write_core_tag wtag;

	if (val > 1)
		return -ERANGE;

	if (!static_branch_likely(&sched_smt_present))
		return -EINVAL;

	if (!tg->core_tagged && val) {
		/* Tag is being set. Check ancestors and descendants. */
		if (cpu_core_get_group_cookie(tg) ||
		    cpu_core_check_descendants(tg, true /* tag */, true /* color */))
			return -EBUSY;
	} else if (tg->core_tagged && !val) {
		/* Tag is being reset. Check descendants. */
		if (cpu_core_check_descendants(tg, true /* tag */, true /* color */))
			return -EBUSY;
	} else {
		return 0;
	}

	if (!!val)
		sched_core_get();

	wtag.css = css;
	wtag.cookie = (unsigned long)tg << 8; /* Reserve lower 8 bits for color. */

	/* Truncate the upper 32-bits - those are used by the per-task cookie. */
	wtag.cookie &= (1UL << (sizeof(unsigned long) * 4)) - 1;

	tg->core_tagged = val;

	stop_machine(__sched_write_tag, (void *) &wtag, NULL);
	if (!val)
		sched_core_put();

	return 0;
}

int cpu_core_tag_color_write_u64(struct cgroup_subsys_state *css,
				 struct cftype *cft, u64 val)
{
	struct task_group *tg = css_tg(css);
	struct write_core_tag wtag;
	u64 cookie;

	if (val > 255)
		return -ERANGE;

	if (!static_branch_likely(&sched_smt_present))
		return -EINVAL;

	cookie = cpu_core_get_group_cookie(tg);
	/* Can't set color if nothing in the ancestors were tagged. */
	if (!cookie)
		return -EINVAL;

	/*
	 * Something in the ancestors already colors us. Can't change the color
	 * at this level.
	 */
	if (!tg->core_tag_color && (cookie & 255))
		return -EINVAL;

	/*
	 * Check if any descendants are colored. If so, we can't recolor them.
	 * Don't need to check if descendants are tagged, since we don't allow
	 * tagging when already tagged.
	 */
	if (cpu_core_check_descendants(tg, false /* tag */, true /* color */))
		return -EINVAL;

	cookie &= ~255;
	cookie |= val;
	wtag.css = css;
	wtag.cookie = cookie;
	tg->core_tag_color = val;

	stop_machine(__sched_write_tag, (void *) &wtag, NULL);

	return 0;
}

void sched_tsk_free(struct task_struct *tsk)
{
	if (!tsk->core_task_cookie)
		return;
	sched_core_put_task_cookie(tsk->core_task_cookie);
	sched_core_put();
}
