/*
 * Lockless lazy-RCU implementation.
 */
#include <linux/rcupdate.h>
#include <linux/shrinker.h>
#include <linux/workqueue.h>
#include "rcu.h"

// How much to batch before flushing?
#define MAX_LAZY_BATCH		2048

// How much to wait before flushing?
#define MAX_LAZY_JIFFIES	(10 * HZ)

unsigned int sysctl_rcu_lazy_batch = MAX_LAZY_BATCH;
unsigned int sysctl_rcu_lazy_jiffies = MAX_LAZY_JIFFIES;
unsigned int sysctl_rcu_lazy = 1;

// We cast lazy_rcu_head to rcu_head and back. This keeps the API simple while
// allowing us to use lockless list node in the head. Also, we use BUILD_BUG_ON
// later to ensure that rcu_head and lazy_rcu_head are of the same size.
struct lazy_rcu_head {
	struct llist_node llist_node;
	void (*func)(struct callback_head *head);
} __attribute__((aligned(sizeof(void *))));

struct rcu_lazy_pcp {
	struct llist_head head;
	struct delayed_work work;
	atomic_t count;
};
DEFINE_PER_CPU(struct rcu_lazy_pcp, rcu_lazy_pcp_ins);

// Lockless flush of CPU, can be called concurrently.
static void lazy_rcu_flush_cpu(struct rcu_lazy_pcp *rlp)
{
	struct llist_node *node = llist_del_all(&rlp->head);
	struct lazy_rcu_head *cursor, *temp;

	if (!node)
		return;

	llist_for_each_entry_safe(cursor, temp, node, llist_node) {
		struct rcu_head *rh = (struct rcu_head *)cursor;
		debug_rcu_head_unqueue(rh);
		call_rcu(rh, rh->func);
		atomic_dec(&rlp->count);
	}
}

void call_rcu_lazy(struct rcu_head *rhp, rcu_callback_t func)
{
	struct lazy_rcu_head *head = (struct lazy_rcu_head *)rhp;
	struct rcu_lazy_pcp *rlp;

	if (!sysctl_rcu_lazy) {
		return call_rcu(head_rcu, func);
	}

	preempt_disable();
	rlp = this_cpu_ptr(&rcu_lazy_pcp_ins);
	preempt_enable();

	if (debug_rcu_head_queue((void *)head)) {
		// Probable double call_rcu(), just leak.
		WARN_ONCE(1, "%s(): Double-freed call. rcu_head %p\n",
				__func__, head);

		// Mark as success and leave.
		return;
	}

	// Queue to per-cpu llist
	head->func = func;
	llist_add(&head->llist_node, &rlp->head);

	// Flush queue if too big
	if (atomic_inc_return(&rlp->count) >= sysctl_rcu_lazy_batch) {
		lazy_rcu_flush_cpu(rlp);
	else
		schedule_delayed_work(&rlp->work, sysctl_rcu_lazy_jiffies);
}

static unsigned long
lazy_rcu_shrink_count(struct shrinker *shrink, struct shrink_control *sc)
{
	unsigned long count = 0;
	int cpu;

	/* Snapshot count of all CPUs */
	for_each_possible_cpu(cpu) {
		struct rcu_lazy_pcp *rlp = per_cpu_ptr(&rcu_lazy_pcp_ins, cpu);

		count += atomic_read(&rlp->count);
	}

	return count;
}

static unsigned long
lazy_rcu_shrink_scan(struct shrinker *shrink, struct shrink_control *sc)
{
	int cpu, freed = 0;

	for_each_possible_cpu(cpu) {
		struct rcu_lazy_pcp *rlp = per_cpu_ptr(&rcu_lazy_pcp_ins, cpu);
		unsigned long count;

		count = atomic_read(&rlp->count);
		lazy_rcu_flush_cpu(rlp);
		sc->nr_to_scan -= count;
		freed += count;
		if (sc->nr_to_scan <= 0)
			break;
	}

	return freed == 0 ? SHRINK_STOP : freed;
}

/*
 * This function is invoked after MAX_LAZY_JIFFIES timeout.
 */
static void lazy_work(struct work_struct *work)
{
	struct rcu_lazy_pcp *rlp = container_of(work, struct rcu_lazy_pcp, work.work);

	lazy_rcu_flush_cpu(rlp);
}

static struct shrinker lazy_rcu_shrinker = {
	.count_objects = lazy_rcu_shrink_count,
	.scan_objects = lazy_rcu_shrink_scan,
	.batch = 0,
	.seeks = DEFAULT_SEEKS,
};

void __init rcu_lazy_init(void)
{
	int cpu;

	BUILD_BUG_ON(sizeof(struct lazy_rcu_head) != sizeof(struct rcu_head));

	for_each_possible_cpu(cpu) {
		struct rcu_lazy_pcp *rlp = per_cpu_ptr(&rcu_lazy_pcp_ins, cpu);
		INIT_DELAYED_WORK(&rlp->work, lazy_work);
	}

	if (register_shrinker(&lazy_rcu_shrinker))
		pr_err("Failed to register lazy_rcu shrinker!\n");
}
