Cross-HT attacks
================
MDS and L1TF mitigations do not protect from cross-HT attacks (attacker running
on one HT with victim running on another). For proper mitigation of this,
core scheduler support is available via the ``CONFIG_SCHED_CORE`` config option.
Using this feature, userspace defines groups of tasks that trust each other.
The core scheduler uses this information to make sure that 2 tasks that do not
trust each other will never run simultaneously on a core while ensuring CFS
fairness properties on a core-wide basis.

Usage
-----
Core-scheduling adds a ``cpu.tag`` file to the CPU controller CGroup. Tasks that
trust each other are assigned to a common group with 1 written into the group's
tag file.

Force-idling of tasks
---------------------
The scheduler tries its best to find 2 tasks that trust each other such that
both tasks are of the highest dynamic CFS priority. However, this is not always
possible. In case the highest priority task of a core does not have any other
task it can be co-scheduled with, the other sibling HTs of the core will be
forced to idle.

When the highest priorty task is selected to run, a reschedule-IPI is sent to
the sibling to force it into idle. This results in 4 cases which need to be
considered depending on whether a VM or a regular usermode process was running
on either HT:

::
          HT1 (attack)            HT2 (victim)
   
   A      idle -> user space      user space -> idle
   
   B      idle -> user space      guest -> idle
   
   C      idle -> guest           user space -> idle
   
   D      idle -> guest           guest -> idle

Note that for better performance, we do not wait for the destination CPU
(victim) to enter idle mode.  This is because the sending of the IPI would
bring the destination CPU immediately into kernel mode from user space, or
VMEXIT from guests. At best, this would only leak some scheduler metadata which
may not be worth protecting.

Protection against interrupts using IRQ pausing
-----------------------------------------------
The scheduler on its own cannot protect interrupt data. This is because the
scheduler is unaware of interrupts at scheduling time. To mitigate this, we
send an IPI to siblings on IRQ entry. This IPI handler busy-waits until the IRQ
on the sending HT exits. For good performance, we send an IPI only if it is
detected that the core is running tasks that have been marked for
core-scheduling. Both interrupts and softirqs are protected.

This protection can be disabled by disabling ``CONFIG_SCHED_CORE_IRQ_PAUSE`` or
through the ``sched_core_irq_pause`` boot parameter.

If it is desired to disable IRQ pausing, other mitigation methods can be considered:

1. Changing interrupt affinities to a trusted core which does not execute untrusted tasks
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
By changing the interrupt affinities to a designated safe-CPU which runs
only trusted tasks, IRQ data can be protected. One issue is this involves
giving up a full CPU core of the system to run safe tasks. Another is that,
per-cpu interrupts such as the local timer interrupt cannot have their
affinity changed. also, sensitive timer callbacks such as the random entropy timer
can run in softirq on return from these interrupts and expose sensitive
data. In the future, that could be mitigated by forcing softirqs into threaded
mode by utilizing a mechanism similar to ``PREEMPT_RT``.

Yet another issue with this is, for multiqueue devices with managed
interrupts, the IRQ affinities cannot be changed however it could be
possible to force a reduced number of queues which would in turn allow to
shield one or two CPUs from such interrupts and queue handling for the price
of indirection.

2. Running IRQs as threaded-IRQs
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
This would result in forcing IRQs into the scheduler which would then provide
the process-context mitigation. However, not all interrupts can be threaded.

Trust model
-----------
Core-scheduling understands trust relationships by assignment of a cookie to
related tasks using the above mentioned interface.  When a system with core
scheduling boots, all tasks are considered to trust each other. This is because
the scheduler does not have information about trust relationships. That is, all
tasks have a default cookie value of 0. This cookie value is also considered
the system-wide cookie value and the IRQ-pausing mitigation is avoided if
siblings are running these cookie-0 tasks.

By default, all system processes on boot are considered trusted and userspace
has to explicitly use the interfaces mentioned above to group sets of tasks.
Tasks within the group trust each other, but not those outside. Tasks outside
the group don't trust the task inside.

Open cross-HT issues that core-scheduling does not solve
--------------------------------------------------------
1. For MDS
^^^^^^^^^^
Core scheduling cannot protect against MDS attacks between an HT running in
user mode and another running in kernel mode. Even though both HTs
run tasks which trust each other, kernel memory is still considered
untrusted. Such attacks are possible for any combination of sibling CPU
modes (host or guest mode).

2. For L1TF
^^^^^^^^^^^
Core scheduling cannot protect against a L1TF guest attackers exploiting a
guest or host victim. This is because the guest attacker can craft invalid
PTEs which are not inverted due to a vulnerable guest kernel. The only
solution is to disable EPT.

For both MDS and L1TF, if the guest vCPU is configured to not trust each
other (by tagging separately), then the guest to guest attacks would go away.
Or it could be a system admin policy which considers guest to guest attacks as
a guest problem.

Another approach to resolve these would be to make every untrusted task on the
system to not trust every other untrusted task. While this could reduce
parallelism of the untrusted tasks, it would still solve the above issues while
allowing system processes (trusted tasks) to share a core.

Future work
-----------
1. Auto-tagging of KVM vCPU threads
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
To make configuration easier, it would be great if KVM auto-tags vCPU threads
such that a given VM only trusts other vCPUs of the same VM. Or something more
aggressive like assiging a vCPU thread a unique tag.

2. Auto-tagging of processes by default
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Currently core-scheduling does not prevent 'unconfigured' tasks from being
co-scheduled on the same core. In other words, everything trusts everything
else by default. If a user wants everything default untrusted, a CONFIG option
could be added to assign every task with a unique tag by default.

3. Auto-tagging on fork
^^^^^^^^^^^^^^^^^^^^^^^
Currently, on fork a thread is added to the same trust-domain as the parent.
For systems which want all tasks to have a unique tag, it could be desirable to
assign a unique tag to a task so that the parent does not trust the child by default.

4. Skipping per-HT mitigations if task is trusted
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
If core-scheduling is enabled, by default all tasks trust each other as
mentioned above. In such scenario, it may be desirable to skip the same-HT
mitigations on return to the trusted user-mode to improve performance.
