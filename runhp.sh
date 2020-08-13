#!/bin/bash
	
run-qemu \
    --boot-args "trace_event=sched:* trace_buf_size=200k rcutorture.onoff_interval=200 rcutorture.onoff_holdoff=2 rcutorture.shutdown_secs=10 rcutree.gp_preinit_delay=12 rcutree.gp_init_delay=3 rcutree.gp_cleanup_delay=3 rcutree.kthread_prio=2 threadirqs ftrace_dump_on_oops" --cpus 4 | tee /tmp/o

exit

run-qemu \
    --boot-args "rcutorture.onoff_interval=200 rcutorture.onoff_holdoff=2 rcutorture.shutdown_secs=10 rcutree.gp_preinit_delay=12 rcutree.gp_init_delay=3 rcutree.gp_cleanup_delay=3 rcutree.kthread_prio=2 threadirqs" #trace_event=rcu:rcu_grace_period"
