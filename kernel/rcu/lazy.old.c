static unsigned long
lazy_shrink_count(struct shrinker *shrink, struct shrink_control *sc)
{
	unsigned long count = 0;
	struct rcu_lazy *rl;
	int cpu;

	rl = container_of(shrink, struct rcu_lazy, shr);

	for_each_possible_cpu(cpu) {
		struct rcu_lazy_pcp *rlp = per_cpu_ptr(rl->rlp, cpu);

		count += atomic_read(&rlp->count);
	}
	// trace_printk("count called, %ld\n", (unsigned long)count);
	return count;
}

int lazy_rcu_flush(struct rcu_lazy *rl)
{
	struct llist_node *node, *first, *last;
	struct rcu_lazy_pcp *rlp;
	struct file *f, *t;
	int cpu, count;

	for_each_possible_cpu(cpu) {
		count = 0;

		rlp = per_cpu_ptr(rl->rlp, cpu);
		node = llist_del_all(&rlp->head);

		// trace_printk("scan called , sc->nr_to_scan %lu\n", sc->nr_to_scan);
		if (!node) {
			// trace_printk("scan node is NULL\n");
			return 0;
		}

		llist_for_each_entry_safe(f, t, node, f_u.fu_llist) {
			call_rcu(&f->f_u.fu_rcuhead, f->f_u.fu_rcuhead.func);
			atomic_dec(&count);
			count++;

			// Do only 64K per-cpu of call_rcu()s at a time.
			if (count >= 65536) {
				break;
			}
		}

		// trace_printk("call_rcud %d\n", count);

		// Queue back the rest.
		if (count >= 65536 && t) {
			// Get the last node in the list
			first = last = &t->f_u.fu_llist;
			while (llist_next(last)) {
				last = llist_next(last);
			}

			llist_add_batch(first, last, &rb->head);
		}
	}

	return count;
}

static unsigned long
lazy_shrink_scan(struct shrinker *shrink, struct shrink_control *sc)
{
	struct rcu_lazy *rl;

	rl = container_of(shrink, struct rcu_lazy, shr);
	int count = lazy_rcu_flush(rl);

	// trace_printk("list size after queuing back rest: %d atomic count
	// %d", get_list_size_full(), get_list_size());
	return count == 0 ? SHRINK_STOP : count;
}


void __call_rcu_lazy(struct rcu_head *head, rcu_callback_t func,
		     struct rcu_lazy *rl)
{
	// Queue the new file into a callback. Shrinkers will RCU free them.
	preempt_disable();
	struct rcu_lazy_pcp *this_rlp = this_cpu_ptr(rl->rlp);
	preempt_enable();
	atomic_inc(&this_rlp->count);
	llist_add(&f->f_u.fu_llist, &this_rlp->head);

	// If we have too many objects, force rcu flush.
	if (atomic_read(&this_rlp->count) >= rl->max_count)
		lazy_rcu_flush(rl);

}

void __init_rcu_lazy(struct rcu_lazy *rl)
{
	rl->shr.count_objects = lazy_shrink_count;
	rl->shr.scan_objects = lazy_shrink_scan;
	rl->shr.seeks = DEFAULT_SEEKS;

	BUG_ON(register_shrinker(&rl->shr));
}
