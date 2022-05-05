/*
 * Lockless rcu_lazy implementation.
 */
#include <linux/rcupdate.h>

// How much to batch before flushing?
#define MAX_LAZY_BATCH		64

// How much to wait before flushing?
#define MAX_LAZY_JIFFIES	30

struct rcu_lazy_pcp {
	struct llist_head head;
	struct delayed_work work;
	atomic_t count;
};
DEFINE_PER_CPU(struct rcu_lazy_pcp, rcu_lazy_pcp_ins);

// Lockless flush of CPU, can be called concurrently from
// more than 1 CPU.
static void lazy_rcu_flush_cpu(struct rcu_lazy_pcp *rlp)
{
	node = llist_del_all(&rlp->head);
	if (!node)
		continue;

	llist_for_each_entry_safe(cursor, temp, node, llist_node) {
		debug_rcu_head_unqueue(cursor);
		call_rcu(cursor, cursor->func);
		atomic_dec(&rlp->count);
	}
}

void call_rcu_lazy(struct rcu_head *head, rcu_callback_t func)
{
	struct rcu_lazy_pcp *rlp;

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
	if (atomic_inc_return(&rlp->count) >= MAX_LAZY_BATCH) {
		lazy_rcu_flush_cpu(rlp);
	} else {
		if (!delayed_work_pending(&rlp->work)) {
			schedule_delayed_work(&rlp->work, MAX_LAZY_JIFFIES);
		}
	}
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
	struct kfree_rcu_cpu *rlp = container_of(work, struct rcu_lazy_pcp, work.work);
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

	for_each_possible_cpu(cpu) {
		struct rcu_lazy_pcp *rlp = per_cpu_ptr(&rcu_lazy_pcp_ins, cpu);
		INIT_DELAYED_WORK(&rlp->work, lazy_work);
	}

	if (register_shrinker(&lazy_rcu_shrinker))
		pr_err("Failed to register lazy_rcu shrinker!\n");
}
