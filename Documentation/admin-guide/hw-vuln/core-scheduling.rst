Core Scheduling
***************
Core scheduling support allows userspace to define groups of tasks that can
share a core. These groups can be specified either for security usecases (one
group of tasks don't trust another), or for performance usecases (some
workloads may benefit from running on the same core as they don't need the same
hardware resources of the shared core).

Security usecase
----------------
A cross-HT attack involves the attacker and victim running on different
Hyper Threads of the same core. MDS and L1TF are examples of such attacks.
Without core scheduling, the only full mitigation of cross-HT attacks is to
disable Hyper Threading (HT). Core scheduling allows HT to be turned on safely
by ensuring that trusted tasks can share a core. This increase in core sharing
can improvement performance, however it is not guaranteed that performance will
always improve, though that is seen to be the case with a number of real world
workloads. In theory, core scheduling aims to perform at least as good as when
Hyper Threading is disabled. In practise, this is mostly the case though not
always: as synchronizing scheduling decisions across 2 or more CPUs in a core
involves additional overhead - especially when the system is lightly loaded
(``total_threads <= N/2``).

Usage
-----
Core scheduling support is enabled via the ``CONFIG_SCHED_CORE`` config option.
Using this feature, userspace defines groups of tasks that trust each other.
The core scheduler uses this information to make sure that tasks that do not
trust each other will never run simultaneously on a core, while doing its best
to satisfy the system's scheduling requirements.

There are 2 ways to use core-scheduling:

CGroup
######
Core scheduling adds additional files to the CPU controller CGroup:

* ``cpu.tag``
Writing ``1`` into this file results in all tasks in the group get tagged. This
results in all the CGroup's tasks allowed to run concurrently on a core's
hyperthreads (also called siblings).

The file being a value of ``0`` means the tag state of the CGroup is inheritted
from its parent hierarchy. If any ancestor of the CGroup is tagged, then the
group is tagged.

.. note:: Once a CGroup is tagged via cpu.tag, it is not possible to set this
          for any descendant of the tagged group. For finer grained control, the
          ``cpu.tag_color`` file described next may be used.

.. note:: When a CGroup is not tagged, all the tasks within the group can share
          a core with kernel threads and untagged system threads. For this reason,
          if a group has ``cpu.tag`` of 0, it is considered to be trusted.

* ``cpu.tag_color``
For finer grained control over core sharing, a color can also be set in
addition to the tag. This allows to further control core sharing between child
CGroups within an already tagged CGroup. The color and the tag are both used to
generate a `cookie` which is used by the scheduler to identify the group.

Upto 256 different colors can be set (0-255) by writing into this file.

A sample real-world usage of this file follows:

Google uses DAC controls to make ``cpu.tag`` writeable only by root and the
``cpu.tag_color`` can be changed by anyone.

The hierarchy looks like this:
::
  Root group
     / \
    A   B    (These are created by the root daemon - borglet).
   / \   \
  C   D   E  (These are created by AppEngine within the container).

A and B are containers for 2 different jobs or apps that are created by a root
daemon called borglet. borglet then tags each of these group with the ``cpu.tag``
file. The job itself can create additional child CGroups which are colored by
the container's AppEngine with the ``cpu.tag_color`` file.

The reason why Google uses this 2-level tagging system is that AppEngine wants to
allow a subset of child CGroups within a tagged parent CGroup to be co-scheduled on a
core while not being co-scheduled with other child CGroups. Think of these
child CGroups as belonging to the same customer or project.  Because these
child CGroups are created by AppEngine, they are not tracked by borglet (the
root daemon), therefore borglet won't have a chance to set a color for them.
That's where cpu.tag_color file comes in. A color could be set by AppEngine,
and once set, the normal tasks within the subcgroup would not be able to
overwrite it. This is enforced by promoting the permission of the
``cpu.tag_color`` file in cgroupfs.

The color is an 8-bit value allowing for upto 256 unique colors.

.. note:: Once a CGroup is colored, none of its descendants can be re-colored. Also
          coloring of a CGroup is possible only if either the group or one of its
          ancestors were tagged via the ``cpu.tag`` file.

prctl interface
###############
A ``prtcl(2)`` command ``PR_SCHED_CORE_SHARE`` is available to a process to request
sharing a core with another process.  For example, consider 2 processes ``P1``
and ``P2`` with PIDs 100 and 200. If process ``P1`` calls
``prctl(PR_SCHED_CORE_SHARE, 200)``, the kernel makes ``P1`` share a core with ``P2``.
The kernel performs ptrace access mode checks before granting the request.

.. note:: This operation is not commutative. P1 calling
          ``prctl(PR_SCHED_CORE_SHARE, pidof(P2)`` is not the same as P2 calling the
          same for P1. The former case is P1 joining P2's group of processes
          (which P2 would have joined with ``prctl(2)`` prior to P1's ``prctl(2)``).

.. note:: The core-sharing granted with prctl(2) will be subject to
          core-sharing restrictions specified by the CGroup interface. For example
          if P1 and P2 are a part of 2 different tagged CGroups, then they will
          not share a core even if a prctl(2) call is made. This is analogous
          to how affinities are set using the cpuset interface.

It is important to note that, on a ``CLONE_THREAD`` ``clone(2)`` syscall, the child
will be assigned the same tag as its parent and thus be allowed to share a core
with them. is design choice is because, for the security usecase, a
``CLONE_THREAD`` child can access its parent's address space anyway, so there's
no point in not allowing them to share a core. If a different behavior is
desired, the child thread can call ``prctl(2)`` as needed.  This behavior is
specific to the ``prctl(2)`` interface. For the CGroup interface, the child of a
fork always share's a core with its parent's.  On the other hand, if a parent
was previously tagged via ``prctl(2)`` and does a regular ``fork(2)`` syscall, the
child will receive a unique tag.

Design/Implementation
---------------------
Each task that is tagged is assigned a cookie internally in the kernel. As
mentioned in `Usage`_, tasks with the same cookie value are assumed to trust
each other and share a core.

The basic idea is that, every schedule event tries to select tasks for all the
siblings of a core such that all the selected tasks running on a core are
trusted (same cookie) at any point in time. Kernel threads are assumed trusted.
The idle task is considered special, in that it trusts every thing.

During a ``schedule()`` event on any sibling of a core, the highest priority task for
that core is picked and assigned to the sibling calling ``schedule()`` if it has it
enqueued. For rest of the siblings in the core, highest priority task with the
same cookie is selected if there is one runnable in their individual run
queues. If a task with same cookie is not available, the idle task is selected.
Idle task is globally trusted.

Once a task has been selected for all the siblings in the core, an IPI is sent to
siblings for whom a new task was selected. Siblings on receiving the IPI, will
switch to the new task immediately. If an idle task is selected for a sibling,
then the sibling is considered to be in a `forced idle` state. i.e., it may
have tasks on its on runqueue to run, however it will still have to run idle.
More on this in the next section.

Forced-idling of tasks
----------------------
The scheduler tries its best to find tasks that trust each other such that all
tasks selected to be scheduled are of the highest priority in a core.  However,
it is possible that some runqueues had tasks that were incompatibile with the
highest priority ones in the core. Favoring security over fairness, one or more
siblings could be forced to select a lower priority task if the highest
priority task is not trusted with respect to the core wide highest priority
task.  If a sibling does not have a trusted task to run, it will be forced idle
by the scheduler(idle thread is scheduled to run).

When the highest priorty task is selected to run, a reschedule-IPI is sent to
the sibling to force it into idle. This results in 4 cases which need to be
considered depending on whether a VM or a regular usermode process was running
on either HT::

          HT1 (attack)            HT2 (victim)
   A      idle -> user space      user space -> idle
   B      idle -> user space      guest -> idle
   C      idle -> guest           user space -> idle
   D      idle -> guest           guest -> idle

Note that for better performance, we do not wait for the destination CPU
(victim) to enter idle mode. This is because the sending of the IPI would bring
the destination CPU immediately into kernel mode from user space, or VMEXIT
in the case of  guests. At best, this would only leak some scheduler metadata
which may not be worth protecting. It is also possible that the IPI is received
too late on some architectures, but this has not been observed in the case of
x86.

Kernel protection from untrusted tasks
--------------------------------------
The scheduler on its own cannot protect the kernel executing concurrently with
an untrusted task in a core. This is because the scheduler is unaware of
interrupts/syscalls at scheduling time. To mitigate this, an IPI is sent to
siblings on kernel entry (syscall and IRQ). This IPI forces the sibling to enter
kernel mode and wait before returning to user until all siblings of the
core have left kernel mode. This process is also known as stunning.  For good
performance, an IPI is sent only to a sibling only if it is running a tagged
task. If a sibling is running a kernel thread or is idle, no IPI is sent.

The kernel protection feature can be turned off on the kernel command line by
passing ``sched_core_protect_kernel=0``.

Other alternative ideas discussed for kernel protection are listed below just
for completeness. They all have limitations:

1. Changing interrupt affinities to a trusted core which does not execute untrusted tasks
#########################################################################################
By changing the interrupt affinities to a designated safe-CPU which runs
only trusted tasks, IRQ data can be protected. One issue is this involves
giving up a full CPU core of the system to run safe tasks. Another is that,
per-cpu interrupts such as the local timer interrupt cannot have their
affinity changed. also, sensitive timer callbacks such as the random entropy timer
can run in softirq on return from these interrupts and expose sensitive
data. In the future, that could be mitigated by forcing softirqs into threaded
mode by utilizing a mechanism similar to ``CONFIG_PREEMPT_RT``.

Yet another issue with this is, for multiqueue devices with managed
interrupts, the IRQ affinities cannot be changed however it could be
possible to force a reduced number of queues which would in turn allow to
shield one or two CPUs from such interrupts and queue handling for the price
of indirection.

2. Running IRQs as threaded-IRQs
################################
This would result in forcing IRQs into the scheduler which would then provide
the process-context mitigation. However, not all interrupts can be threaded.
Also this does nothing about syscall entries.

3. Kernel Address Space Isolation
#################################
System calls could run in a much restricted address space which is
guarenteed not to leak any sensitive data. There are practical limitation in
implementing this - the main concern being how to decide on an address space
that is guarenteed to not have any sensitive data.

4. Limited cookie-based protection
##################################
On a system call, change the cookie to the system trusted cookie and initiate a
schedule event. This would be better than pausing all the siblings during the
entire duration for the system call, but still would be a huge hit to the
performance.

Trust model
-----------
Core scheduling maintains trust relationships amongst groups of tasks by
assigning the tag of them with the same cookie value.
When a system with core scheduling boots, all tasks are considered to trust
each other. This is because the core scheduler does not have information about
trust relationships until userspace uses the above mentioned interfaces, to
communicate them. In other words, all tasks have a default cookie value of 0.
and are considered system-wide trusted. The stunning of siblings running
cookie-0 tasks is also avoided.

Once userspace uses the above mentioned interfaces to group sets of tasks, tasks
within such groups are considered to trust each other, but do not trust those
outside. Tasks outside the group also don't trust tasks within.

Limitations
-----------
Core scheduling tries to guarentee that only trusted tasks run concurrently on a
core. But there could be small window of time during which untrusted tasks run
concurrently or kernel could be running concurrently with a task not trusted by
kernel.

1. IPI processing delays
########################
Core scheduling selects only trusted tasks to run together. IPI is used to notify
the siblings to switch to the new task. But there could be hardware delays in
receiving of the IPI on some arch (on x86, this has not been observed). This may
cause an attacker task to start running on a cpu before its siblings receive the
IPI. Even though cache is flushed on entry to user mode, victim tasks on siblings
may populate data in the cache and micro acrhitectural buffers after the attacker
starts to run and this is a possibility for data leak.

Open cross-HT issues that core scheduling does not solve
--------------------------------------------------------
1. For MDS
##########
Core scheduling cannot protect against MDS attacks between an HT running in
user mode and another running in kernel mode. Even though both HTs run tasks
which trust each other, kernel memory is still considered untrusted. Such
attacks are possible for any combination of sibling CPU modes (host or guest mode).

2. For L1TF
###########
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
Skipping per-HT mitigations if task is trusted
##############################################
If core scheduling is enabled, by default all tasks trust each other as
mentioned above. In such scenario, it may be desirable to skip the same-HT
mitigations on return to the trusted user-mode to improve performance.
