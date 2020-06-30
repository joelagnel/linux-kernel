Core Scheduling
================
MDS and L1TF mitigations do not protect from cross-HT attacks (attacker running
on one HT with victim running on another). For proper mitigation of this,
core scheduling support is available via the ``CONFIG_SCHED_CORE`` config option.
Using this feature, userspace defines groups of tasks that trust each other.
The core scheduler uses this information to make sure that tasks that do not
trust each other will never run simultaneously on a core while ensuring trying
to maintain and ensure scheduler properties and requirements.

Usage
-----
The user-interface to this feature is not yet finalized. CUrrent implementation
uses CPU controller cgroup. Core scheduling adds a ``cpu.tag`` file to the CPU
controller CGroup. If the content of this file is 1, then all the tasks in this
CGroup trust each other and are allowed to run concurrently on the siblings of
a core.

This interface has drawbacks. Trusted tasks has to be grouped into one CPU CGroup
and this is not always possible based on system's existing Cgroup configuration,
where trusted tasks could already be in different CPU Cgroups. Also, this feature
will have a hard dependency on CGroups and systems with CGroups disabled would not
be able to use core scheduling. See `Future work`_ for other API proposals.

Design/Implementation
---------------------
Tasks are grouped as mentioned in `Usage`_ and tasks that trust each other
share the same cookie value(in task_struct).

The basic idea is that, every schedule event tries to select tasks for all the
siblings of a core such that all the selected tasks are trusted(same cookie).

During a schedule event on any sibling of a core, the highest priority task for
that core is picked and assigned to the sibling which has it enqueued. For rest of
the siblings in the core, highest priority task with the same cookie is selected if
there is one runnable in the run queue. If a task with same cookie is not available,
idle task is selected. Idle task is globally trusted.

Once a task has been selected for all the siblings in the core, an IPI is sent to
all the siblings who has a new task selected. Siblings on receiving the IPI, will
switch to the new task immediately.

Force-idling of tasks
---------------------
The scheduler tries its best to find tasks that trust each other such that all
tasks selected to be scheduled are of the highest priority in that runqueue.
However, this is not always possible. Favoring security over fairness, one or
more siblings could be forced to select a lower priority task if the highest
priority task is not trusted with respect to the core wide highest priority task.
If a sibling does not have a trusted task to run, it will be forced idle by the
scheduler(idle thread is scheduled to run).

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
core scheduling. Both interrupts and softirqs are protected.

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
Core scheduling understands trust relationships by assignment of a cookie to
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

Limitations
-----------
Core scheduling tries to guarentee that only trusted tasks run concurrently on a
core. But there could be small window of time during which untrusted tasks run
concurrently or kernel could be running concurrently with a task not trusted by
kernel.

1. IPI processing delays
^^^^^^^^^^^^^^^^^^^^^^^^
Core scheduling selects only trusted tasks to run together. IPI is used to notify
the siblings to switch to the new task. But there could be hardware delays in
receiving of the IPI on some arch (on x86, this has not been observed). This may
cause an attacker task to start running on a cpu before its siblings receive the
IPI. Even though cache is flushed on entry to user mode, victim tasks on siblings
may populate data in the cache and micro acrhitectural buffers after the attacker
starts to run and this is a possibility for data leak.

2. Asynchronous Kernel entries
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
A task can switch to kernel any time due to events like irqs, system calls etc.
Since core scheduling synchronizes only during a schedule event, kernel can run
along with a task that it doesn't trust. The IRQ pause mechanism mentioned above,
provides protection during nmi/irq/softirqs. But tasks could still enter kernel
mode via system calls and this is not currently protected.

There are ideas about mitigating this:
 - Kernel Address Space Isolation: System calls could run in a much restricted
   address space which is guarenteed not to leak any sensitive data. There are
   practical limitation in implementing this - the main concern being how to
   decided on an address space that is guarenteed to not have any sensitive
   data
 - On a system call, change the cookie to the system trusted cookie and initiate
   a schedule event. This would be better than pausing all the siblings during
   the entire duration for the system call, but still would be a huge hit to the
   performance.

Open cross-HT issues that core scheduling does not solve
--------------------------------------------------------
1. For MDS
^^^^^^^^^^
Core scheduling cannot protect against MDS attacks between an HT running in
user mode and another running in kernel mode. Even though both HTs run tasks
which trust each other, kernel memory is still considered untrusted. Such
attacks are possible for any combination of sibling CPU modes (host or guest mode).

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

Use cases
---------
The main use case for Core scheduling is mitigating the cross-HT vulnerabilities
with SMT enabled. There are other use cases where this feature could be used:

- Isolating tasks that needs a whole core: Examples include realtime tasks, tasks
  that uses SIMD instructions etc.
- Gang scheduling: Requirements for a group of tasks that needs to be scheduled
  together could also be realized using core scheduling. One example is vcpus of
  a VM.

Future work
-----------
1. API Proposals
^^^^^^^^^^^^^^^^

As mentioned in `Usage`_ section, various API proposals are listed here:

- ``prctl`` : We can pass in a tag and all tasks with same tag set by prctl forms
  a trusted group.

- ``sched_setattr`` : Similar to prctl, but has the advantage that tasks could be
  tagged by other tasks with appropriate permissions.

- ``Auto Tagging`` : Related tasks are tagged automatically. Relation could be,
  threads of the same process, tasks by a user, group or session etc.

- Dedicated cgroup or procfs/sysfs interface for grouping trusted tasks. This could
  be combined with prctl/sched_setattr as well.

2. Auto-tagging of KVM vCPU threads
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
To make configuration easier, it would be great if KVM auto-tags vCPU threads
such that a given VM only trusts other vCPUs of the same VM. Or something more
aggressive like assiging a vCPU thread a unique tag.

3. Auto-tagging of processes by default
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Currently core scheduling does not prevent 'unconfigured' tasks from being
co-scheduled on the same core. In other words, everything trusts everything
else by default. If a user wants everything default untrusted, a CONFIG option
could be added to assign every task with a unique tag by default.

4. Auto-tagging on fork
^^^^^^^^^^^^^^^^^^^^^^^
Currently, on fork a thread is added to the same trust-domain as the parent. For
systems which want all tasks to have a unique tag, it could be desirable to assign
a unique tag to a task so that the parent does not trust the child by default.

5. Skipping per-HT mitigations if task is trusted
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
If core scheduling is enabled, by default all tasks trust each other as
mentioned above. In such scenario, it may be desirable to skip the same-HT
mitigations on return to the trusted user-mode to improve performance.
