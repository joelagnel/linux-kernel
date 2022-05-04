#include <linux/rcupdate.h>

struct rcu_lazy_pcp {
	struct llist_head head;
	atomic_t count;
};

DEFINE_PER_CPU(struct rcu_lazy_pcp, rcu_lazy_pcp_ins);

void call_rcu_lazy(struct rcu_head *head, rcu_callback_t func)
{
	head->func = func;

	preempt_disable();
        struct rcu_lazy_pcp *rlp = this_cpu_ptr(&rcu_lazy_pcp_ins);
	preempt_enable();

	atomic_inc(&this_rlp->count);
	llist_add(&head->llist_node, &this_rlp->head);
}
