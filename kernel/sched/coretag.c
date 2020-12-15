// SPDX-License-Identifier: GPL-2.0-only
/*
 * kernel/sched/core-tag.c
 *
 * Core-scheduling tagging interface support.
 *
 * Copyright(C) 2020, Joel Fernandes.
 * Initial interfacing code  by Peter Ziljstra.
 */

#include <linux/prctl.h>
#include "sched.h"

/*
 * Wrapper representing a complete cookie. The address of the cookie is used as
 * a unique identifier. Each cookie has a unique permutation of the internal
 * cookie fields.
 */
struct sched_core_cookie {
	unsigned long task_cookie;
	unsigned long group_cookie;

	struct rb_node node;
	refcount_t refcnt;
};

/*
 * A simple wrapper around refcount. An allocated sched_core_task_cookie's
 * address is used to compute the cookie of the task.
 */
struct sched_core_task_cookie {
	refcount_t refcnt;
	struct work_struct work; /* to free in WQ context. */;
};

static DEFINE_MUTEX(sched_core_tasks_mutex);

/* All active sched_core_cookies */
static struct rb_root sched_core_cookies = RB_ROOT;
static DEFINE_RAW_SPINLOCK(sched_core_cookies_lock);

/*
 * Returns the following:
 * a < b  => -1
 * a == b => 0
 * a > b  => 1
 */
static int sched_core_cookie_cmp(const struct sched_core_cookie *a,
				 const struct sched_core_cookie *b)
{
#define COOKIE_CMP_RETURN(field) do {		\
	if (a->field < b->field)		\
		return -1;			\
	else if (a->field > b->field)		\
		return 1;			\
} while (0)					\

	COOKIE_CMP_RETURN(task_cookie);
	COOKIE_CMP_RETURN(group_cookie);

	/* all cookie fields match */
	return 0;

#undef COOKIE_CMP_RETURN
}

static inline void __sched_core_erase_cookie(struct sched_core_cookie *cookie)
{
	lockdep_assert_held(&sched_core_cookies_lock);

	/* Already removed */
	if (RB_EMPTY_NODE(&cookie->node))
		return;

	rb_erase(&cookie->node, &sched_core_cookies);
	RB_CLEAR_NODE(&cookie->node);
}

/* Called when a task no longer points to the cookie in question */
static void sched_core_put_cookie(struct sched_core_cookie *cookie)
{
	unsigned long flags;

	if (!cookie)
		return;

	if (refcount_dec_and_test(&cookie->refcnt)) {
		raw_spin_lock_irqsave(&sched_core_cookies_lock, flags);
		__sched_core_erase_cookie(cookie);
		raw_spin_unlock_irqrestore(&sched_core_cookies_lock, flags);
		kfree(cookie);
	}
}

/*
 * A task's core cookie is a compound structure composed of various cookie
 * fields (task_cookie, group_cookie). The overall core_cookie is
 * a pointer to a struct containing those values. This function either finds
 * an existing core_cookie or creates a new one, and then updates the task's
 * core_cookie to point to it. Additionally, it handles the necessary reference
 * counting.
 *
 * REQUIRES: task_rq(p) lock or called from cpu_stopper.
 * Doing so ensures that we do not cause races/corruption by modifying/reading
 * task cookie fields.
 */
static void __sched_core_update_cookie(struct task_struct *p)
{
	struct rb_node *parent, **node;
	struct sched_core_cookie *node_core_cookie, *match;
	static const struct sched_core_cookie zero_cookie;
	struct sched_core_cookie temp = {
		.task_cookie	= p->core_task_cookie,
		.group_cookie	= p->core_group_cookie,
	};
	const bool is_zero_cookie =
		(sched_core_cookie_cmp(&temp, &zero_cookie) == 0);
	struct sched_core_cookie *const curr_cookie =
		(struct sched_core_cookie *)p->core_cookie;
	unsigned long flags;

	/*
	 * Already have a cookie matching the requested settings? Nothing to
	 * do.
	 */
	if ((curr_cookie && sched_core_cookie_cmp(curr_cookie, &temp) == 0) ||
	    (!curr_cookie && is_zero_cookie))
		return;

	raw_spin_lock_irqsave(&sched_core_cookies_lock, flags);

	if (is_zero_cookie) {
		match = NULL;
		goto finish;
	}

retry:
	match = NULL;

	node = &sched_core_cookies.rb_node;
	parent = *node;
	while (*node) {
		int cmp;

		node_core_cookie =
			container_of(*node, struct sched_core_cookie, node);
		parent = *node;

		cmp = sched_core_cookie_cmp(&temp, node_core_cookie);
		if (cmp < 0) {
			node = &parent->rb_left;
		} else if (cmp > 0) {
			node = &parent->rb_right;
		} else {
			match = node_core_cookie;
			break;
		}
	}

	if (!match) {
		/* No existing cookie; create and insert one */
		match = kmalloc(sizeof(struct sched_core_cookie), GFP_ATOMIC);

		/* Fall back to zero cookie */
		if (WARN_ON_ONCE(!match))
			goto finish;

		match->task_cookie = temp.task_cookie;
		match->group_cookie = temp.group_cookie;
		refcount_set(&match->refcnt, 1);

		rb_link_node(&match->node, parent, node);
		rb_insert_color(&match->node, &sched_core_cookies);
	} else {
		/*
		 * Cookie exists, increment refcnt. If refcnt is currently 0,
		 * we're racing with a put() (refcnt decremented but cookie not
		 * yet removed from the tree). In this case, we can simply
		 * perform the removal ourselves and retry.
		 * sched_core_put_cookie() will still function correctly.
		 */
		if (unlikely(!refcount_inc_not_zero(&match->refcnt))) {
			__sched_core_erase_cookie(match);
			goto retry;
		}
	}

finish:
	/*
	 * Set the core_cookie under the cookies lock. This guarantees that
	 * p->core_cookie cannot be freed while the cookies lock is held in
	 * sched_core_fork().
	 */
	p->core_cookie = (unsigned long)match;

	raw_spin_unlock_irqrestore(&sched_core_cookies_lock, flags);

	sched_core_put_cookie(curr_cookie);
}

/*
 * sched_core_update_cookie - Common helper to update a task's core cookie. This
 * updates the selected cookie field and then updates the overall cookie.
 * @p: The task whose cookie should be updated.
 * @cookie: The new cookie.
 * @cookie_type: The cookie field to which the cookie corresponds.
 *
 * Here we acquire task_rq(p)->lock to ensure we do not cause races/corruption
 * by modifying/reading task cookie fields.
 */
static void sched_core_update_cookie(struct task_struct *p, unsigned long cookie,
				     enum sched_core_cookie_type cookie_type)
{
	struct rq_flags rf;
	struct rq *rq;

	if (!p)
		return;

	rq = task_rq_lock(p, &rf);

	switch (cookie_type) {
	case sched_core_no_update:
		break;
	case sched_core_task_cookie_type:
		p->core_task_cookie = cookie;
		break;
	case sched_core_group_cookie_type:
		p->core_group_cookie = cookie;
		break;
	default:
		WARN_ON_ONCE(1);
	}

	/* Set p->core_cookie, which is the overall cookie */
	__sched_core_update_cookie(p);

	if (sched_core_enqueued(p)) {
		sched_core_dequeue(rq, p);
		if (!p->core_cookie)
			return;
	}

	if (sched_core_enabled(rq) &&
	    p->core_cookie && task_on_rq_queued(p))
		sched_core_enqueue(task_rq(p), p);

	/*
	 * If task is currently running or waking, it may not be compatible
	 * anymore after the cookie change, so enter the scheduler on its CPU
	 * to schedule it away.
	 */
	if (task_running(rq, p) || p->state == TASK_WAKING)
		resched_curr(rq);

	task_rq_unlock(rq, p, &rf);
}

#ifdef CONFIG_CGROUP_SCHED
void cpu_core_get_group_cookie(struct task_group *tg,
			       unsigned long *group_cookie_ptr);

void sched_core_change_group(struct task_struct *p, struct task_group *new_tg)
{
	unsigned long new_group_cookie;

	cpu_core_get_group_cookie(new_tg, &new_group_cookie);

	if (p->core_group_cookie == new_group_cookie)
		return;

	p->core_group_cookie = new_group_cookie;

	__sched_core_update_cookie(p);
}
#endif

/* Per-task interface: Used by fork(2) and prctl(2). */
static void sched_core_put_cookie_work(struct work_struct *ws);

/* Caller has to call sched_core_get() if non-zero value is returned. */
static unsigned long sched_core_alloc_task_cookie(void)
{
	struct sched_core_task_cookie *ck =
		kmalloc(sizeof(struct sched_core_task_cookie), GFP_KERNEL);

	if (!ck)
		return 0;
	refcount_set(&ck->refcnt, 1);
	INIT_WORK(&ck->work, sched_core_put_cookie_work);

	return (unsigned long)ck;
}

static void sched_core_get_task_cookie(unsigned long cookie)
{
	struct sched_core_task_cookie *ptr =
		(struct sched_core_task_cookie *)cookie;

	refcount_inc(&ptr->refcnt);
}

static void sched_core_put_task_cookie(unsigned long cookie)
{
	struct sched_core_task_cookie *ptr =
		(struct sched_core_task_cookie *)cookie;

	if (refcount_dec_and_test(&ptr->refcnt))
		kfree(ptr);
}

static void sched_core_put_cookie_work(struct work_struct *ws)
{
	struct sched_core_task_cookie *ck =
		container_of(ws, struct sched_core_task_cookie, work);

	sched_core_put_task_cookie((unsigned long)ck);
	sched_core_put();
}

static inline void sched_core_update_task_cookie(struct task_struct *t,
						 unsigned long c)
{
	sched_core_update_cookie(t, c, sched_core_task_cookie_type);
}

int sched_core_share_tasks(struct task_struct *t1, struct task_struct *t2)
{
	unsigned long cookie;
	int ret = -ENOMEM;

	mutex_lock(&sched_core_tasks_mutex);

	if (!t2) {
		if (t1->core_task_cookie) {
			sched_core_put_task_cookie(t1->core_task_cookie);
			sched_core_update_task_cookie(t1, 0);
			sched_core_put();
		}
	} else if (t1 == t2) {
		/* Assign a unique per-task cookie solely for t1. */

		cookie = sched_core_alloc_task_cookie();
		if (!cookie)
			goto out_unlock;
		sched_core_get();

		if (t1->core_task_cookie)
			sched_core_put_task_cookie(t1->core_task_cookie);
		sched_core_update_task_cookie(t1, cookie);
		sched_core_put();
	} else if (!t1->core_task_cookie && !t2->core_task_cookie) {
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

		/* CASE 1. */
		cookie = sched_core_alloc_task_cookie();
		if (!cookie)
			goto out_unlock;
		sched_core_get(); /* For the alloc. */

		/* Add another reference for the other task. */
		sched_core_get_task_cookie(cookie);
		sched_core_get(); /* For the other task. */

		sched_core_update_task_cookie(t1, cookie);
		sched_core_update_task_cookie(t2, cookie);
	} else if (t1->core_task_cookie && !t2->core_task_cookie) {
		/* CASE 2. */
		sched_core_put_task_cookie(t1->core_task_cookie);
		sched_core_update_task_cookie(t1, 0);
		sched_core_put();
	} else if (!t1->core_task_cookie && t2->core_task_cookie) {
		/* CASE 3. */
		sched_core_get_task_cookie(t2->core_task_cookie);
		sched_core_get();
		sched_core_update_task_cookie(t1, t2->core_task_cookie);

	} else {
		/* CASE 4. */
		sched_core_get_task_cookie(t2->core_task_cookie);
		sched_core_get();

		sched_core_put_task_cookie(t1->core_task_cookie);
		sched_core_update_task_cookie(t1, t2->core_task_cookie);
		sched_core_put();
	}

	ret = 0;
out_unlock:
	mutex_unlock(&sched_core_tasks_mutex);
	return ret;
}

/* Called from prctl interface: PR_SCHED_CORE_SHARE */
int sched_core_share_pid(unsigned long flags, pid_t pid)
{
	struct task_struct *to;
	struct task_struct *from;
	struct task_struct *task;
	int err;

	rcu_read_lock();
	task = find_task_by_vpid(pid);
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
		goto out;
	}
	rcu_read_unlock();

	if (flags == PR_SCHED_CORE_CLEAR) {
		to = task;
		from = NULL;
	} else if (flags == PR_SCHED_CORE_SHARE_TO) {
		to = task;
		from = current;
	} else if (flags == PR_SCHED_CORE_SHARE_FROM) {
		to = current;
		from = task;
	} else {
		err = -EINVAL;
		goto out;
	}

	err = sched_core_share_tasks(to, from);
out:
	if (task)
		put_task_struct(task);
	return err;
}

/* CGroup core-scheduling interface support. */
#ifdef CONFIG_CGROUP_SCHED
/*
 * Helper to get the group cookie in a hierarchy. Any ancestor can have a
 * cookie.
 *
 * Sets *group_cookie_ptr to the hierarchical group cookie.
 */
void cpu_core_get_group_cookie(struct task_group *tg,
			       unsigned long *group_cookie_ptr)
{
	unsigned long group_cookie = 0UL;

	if (!tg)
		goto out;

	for (; tg; tg = tg->parent) {

		if (tg->core_tagged) {
			group_cookie = (unsigned long)tg;
			break;
		}
	}

out:
	*group_cookie_ptr = group_cookie;
}

/* Determine if any group in @tg's children are tagged. */
static bool cpu_core_check_descendants(struct task_group *tg, bool check_tag)
{
	struct task_group *child;

	rcu_read_lock();
	list_for_each_entry_rcu(child, &tg->children, siblings) {
		if ((child->core_tagged && check_tag)) {
			rcu_read_unlock();
			return true;
		}

		rcu_read_unlock();
		return cpu_core_check_descendants(child, check_tag);
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

#ifdef CONFIG_SCHED_DEBUG
u64 cpu_core_group_cookie_read_u64(struct cgroup_subsys_state *css,
				   struct cftype *cft)
{
	unsigned long group_cookie;

	cpu_core_get_group_cookie(css_tg(css), &group_cookie);

	return group_cookie;
}
#endif

struct write_core_tag {
	struct cgroup_subsys_state *css;
	unsigned long cookie;
	enum sched_core_cookie_type cookie_type;
};

static int __sched_write_tag(void *data)
{
	struct write_core_tag *tag = (struct write_core_tag *)data;
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
			sched_core_update_cookie(p, tag->cookie,
						 tag->cookie_type);

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
	unsigned long group_cookie;

	if (val > 1)
		return -ERANGE;

	if (!static_branch_likely(&sched_smt_present))
		return -EINVAL;

	if (!tg->core_tagged && val) {
		/* Tag is being set. Check ancestors and descendants. */
		cpu_core_get_group_cookie(tg, &group_cookie);
		if (group_cookie ||
		    cpu_core_check_descendants(tg, true /* tag */))
			return -EBUSY;
	} else if (tg->core_tagged && !val) {
		/* Tag is being reset. Check descendants. */
		if (cpu_core_check_descendants(tg, true /* tag */))
			return -EBUSY;
	} else {
		return 0;
	}

	if (!!val)
		sched_core_get();

	wtag.css = css;
	wtag.cookie = (unsigned long)tg;
	wtag.cookie_type = sched_core_group_cookie_type;

	tg->core_tagged = val;

	stop_machine(__sched_write_tag, (void *)&wtag, NULL);
	if (!val)
		sched_core_put();

	return 0;
}
#endif

/*
 * Tagging support when fork(2) is called:
 * If it is a CLONE_THREAD fork, share parent's tag. Otherwise assign a unique per-task tag.
 */
static int sched_update_core_tag_stopper(void *data)
{
	struct task_struct *p = (struct task_struct *)data;

	/* Recalculate core cookie */
	sched_core_update_cookie(p, 0, sched_core_no_update);

	return 0;
}

/* Called from sched_fork() */
int sched_core_fork(struct task_struct *p, unsigned long clone_flags)
{
	struct sched_core_cookie *parent_cookie =
		(struct sched_core_cookie *)current->core_cookie;

	/*
	 * core_cookie is ref counted; avoid an uncounted reference.
	 * If p should have a cookie, it will be set below.
	 */
	p->core_cookie = 0UL;

	/*
	 * If parent is tagged via per-task cookie, tag the child (either with
	 * the parent's cookie, or a new one).
	 *
	 * We can return directly in this case, because sched_core_share_tasks()
	 * will set the core_cookie (so there is no need to try to inherit from
	 * the parent). The cookie will have the proper sub-fields (ie. group
	 * cookie, etc.), because these come from p's task_struct, which is
	 * dup'd from the parent.
	 */
	if (current->core_task_cookie) {
		int ret;

		/* If it is not CLONE_THREAD fork, assign a unique per-task tag. */
		if (!(clone_flags & CLONE_THREAD)) {
			ret = sched_core_share_tasks(p, p);
		} else {
			/* Otherwise share the parent's per-task tag. */
			ret = sched_core_share_tasks(p, current);
		}

		if (ret)
			return ret;

		/*
		 * We expect sched_core_share_tasks() to always update p's
		 * core_cookie.
		 */
		WARN_ON_ONCE(!p->core_cookie);

		return 0;
	}

	/*
	 * If parent is tagged, inherit the cookie and ensure that the reference
	 * count is updated.
	 *
	 * Technically, we could instead zero-out the task's group_cookie and
	 * allow sched_core_change_group() to handle this post-fork, but
	 * inheriting here has a performance advantage, since we don't
	 * need to traverse the core_cookies RB tree and can instead grab the
	 * parent's cookie directly.
	 */
	if (parent_cookie) {
		bool need_stopper = false;
		unsigned long flags;

		/*
		 * cookies lock prevents task->core_cookie from changing or
		 * being freed
		 */
		raw_spin_lock_irqsave(&sched_core_cookies_lock, flags);

		if (likely(refcount_inc_not_zero(&parent_cookie->refcnt))) {
			p->core_cookie = (unsigned long)parent_cookie;
		} else {
			/*
			 * Raced with put(). We'll use stop_machine to get
			 * a core_cookie
			 */
			need_stopper = true;
		}

		raw_spin_unlock_irqrestore(&sched_core_cookies_lock, flags);

		if (need_stopper)
			stop_machine(sched_update_core_tag_stopper,
				     (void *)p, NULL);
	}

	return 0;
}

void sched_tsk_free(struct task_struct *tsk)
{
	struct sched_core_task_cookie *ck;

	sched_core_put_cookie((struct sched_core_cookie *)tsk->core_cookie);

	if (!tsk->core_task_cookie)
		return;

	ck = (struct sched_core_task_cookie *)tsk->core_task_cookie;
	queue_work(system_wq, &ck->work);
}
