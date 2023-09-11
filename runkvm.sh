#!/bin/bash

# Define the kconfig array
kconfigs=(
		"CONFIG_RCU_TRACE=y"
		"CONFIG_PROVE_LOCKING=y"
		"CONFIG_DEBUG_LOCK_ALLOC=y"
		"CONFIG_DEBUG_INFO_DWARF5=y"
		"CONFIG_RANDOMIZE_BASE=n"
		"CONFIG_LOCKUP_DETECTOR=y"
		"CONFIG_SOFTLOCKUP_DETECTOR=y"
		"CONFIG_HARDLOCKUP_DETECTOR=y"
		"CONFIG_DETECT_HUNG_TASK=y"
		"CONFIG_DEFAULT_HUNG_TASK_TIMEOUT=60"
	)

# Define the bootargs array (excluding trace_event)
bootargs=(
		"ftrace_dump_on_oops"
		"panic_on_warn=1"
		"sysctl.kernel.panic_on_rcu_stall=1"
		"sysctl.kernel.max_rcu_stall_to_panic=1"
		"trace_buf_size=20K"
		"traceoff_on_warning=1"
		"panic_print=0x1f"      # To dump held locks, mem and other info.
		"rcutorture.test_boost=2" # changed from default of 1
		"rcutorture.test_boost_duration=3" # changed from default of 4
		"rcutorture.test_boost_interval=1" # changed from default of 7
	)

# Define the trace events array passed to bootargs.
trace_events=(
		"sched:sched_switch"
		"sched:sched_waking"
		"rcu:rcu_callback"
		"rcu:rcu_fqs"
		"rcu:rcu_quiescent_state_report"
		"rcu:rcu_grace_period"
		"rcu:rcu_invoke_callback"
		"rcu:rcu_barrier"
	)

# Call kvm.sh with the arrays
sudo tools/testing/selftests/rcutorture/bin/kvm.sh \
		--cpus 80 \
		--duration 60 \
		--configs "40*TREE03" \
		--kconfig "$(IFS=" "; echo "${kconfigs[*]}")" \
		--bootargs "trace_event=$(IFS=,; echo "${trace_events[*]}") $(IFS=" "; echo "${bootargs[*]}")"
