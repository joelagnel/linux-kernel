struct rcu_batch_pcp {
	struct llist_head head;
	atomic_t count;
};

static DEFINE_PER_CPU(struct rcu_batch_pcp, frb);

struct rcu_batch {
	struct shrinker shr;
	int max_batch;
	spinlock_t lock;
};



