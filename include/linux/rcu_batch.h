struct rcu_batch_pcp {
	struct llist_head head;
	atomic_t count;
};

struct rcu_batch {
	struct shrinker shr;
	int max_batch;
	spinlock_t lock;
};

#define DEFINE_RCU_BATCH(name)						\
	DEFINE_PER_CPU(struct rcu_batch_pcp, rcu_batch_pcp_##name);	\
	struct rcu_batch rcu_batch_##name;


// Return a reference to an rcu batch. This is would be passed to call_rcu(2)
// For batch functionality.
#define RCU_BATCH(name)							\
	&rcu_batch_pcp_##name
