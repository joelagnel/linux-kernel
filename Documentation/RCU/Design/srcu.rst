SRCU Design
-----------
This document describes the SRCU implementation design. This is important
because there are a number of design choices that effect SRCU behavior, and are
worth capturing in a document of its own.

SRCU is fundamentally a counter-based RCU implementation. Readers modify
counters during entry into and exiting from the RCU read-side critical section.
The writer reads these counters to detect that pre-existing readers have
exited. Exactly what these counters look like, and how they are scanned, is
what makes SRCU unique.

[Describe briefly about counts/design]

Let us list some of SRCU design's implications:

1. An SRCU reader/writer is bound to an SRCU instance (or domain)

Unlike traditional RCU, which is global: SRCU is instantiated per usage.
Read-side critical sections and grace period detection are strictly per-domain
and operations in one domain do not affect the other.  This design choice
minimize the effects of  SRCU readers sleeping for long periods of time. If a
reader does not wake up soon enough, only that SRCU reader's domain's grace
period is held up, with other domains not being affected. Contrast this to
regular RCU, where any reader running for too long can stall the entire system.

2. SRCU writers may have longer update-side speeds compared to regular RCU

Unlike regular RCU, SRCU readers are allowed to sleep. This implies that SRCU
readers can block for long periods of time, thus affecting the update-side
speed. Thus, if it is desired to have fast update side speed and blocking is
not needed, SRCU should be avoided.

3. SRCU read-side lock executes fixed set of instructions and is wait-free

Counter-based RCU implementations such as QRCU [1] and SRCU depend on sampling
of an index which tells the reader which pair of counters to modify. It is
possible that there is a race between reader and writer, such that the index
sampled is not the right one because it was sampled after a flip happened. This
can cause an implementation to retry. It can even keep retrying indefinitely if
it keeps getting blocked and keeps sampling the incorrect index potentially
waiting for a long time.

SRCU's design on the other hand does not suffer from this issue. Even though
SRCU readers have higher overhead than non-preemptible RCU (which is basically
just a compiler barrier), the number of instructions they execute are fixed.
This is because SRCU updates scan the inactive set of counters twice (both
before and after a flip), such that unbounded looping in the reader is not
necessary.

4. Starvation freedom

Info on this is mentioned in:
https://people.kernel.org/joelfernandes/srcu-state-machine-and-double-scan
To prevent starvation, flipping and draining of inactive counter-part is
needed. The reader-count on the inactive part has to monotonically decrease so
that scanning inactive counter part eventually converges.

5. Overhead of SRCU

SRCU needs mem barriers to maintain the RCU invariants. To a point where Paul
is proposing a new RCU variant (Tasks tracing RCU), which uses TasksRCU like
mechanisms and IPIs to detect grace periods, while using task-specific nesting
counters on the reader side.
lore.kernel.org/r/CAEXW_YRtGhiaz+86pTL2WTyx5tqrpjB-bgQbnMLXjSQXPCmYfw@mail.gmail.com

6. Use of workqueues instead of GP thread

The grace period machinery of SRCU is driven using workqueues which
periodically queue/requeue work, to detect end of grace periods, start new
grace periods and invoke callbacks. This design choice is because SRCU does not
do updates that often. On the other hand, RCU does updates all the time and we
really care about forward progress much more there. In the future, if it is
seen that SRCU does a lot of updates, then the design may move to using kernel
threads.

The other reason is, SRCU readers are expected to block for a long period of
time such as waiting for a network connection. In this case, worrying about
forward progress of grace periods makes even less sense.

7. Why TREE SRCU?

The tree-based SRCU scheme uses the same Tree mechanism as regular RCU to
program start and end of grace periods, and invoke callbacks. Thus reducing
lock contention on any locking needed to manage the SRCU state.

This arose out of work to get rid of mmap_sem and replace it with SRCU in the
mm subsystem (such work has not yet been completed). By using TREE SRCU, the
idea is for SRCU to scale to a large number of CPUs (potentially 1000s) where
contention could be quite high in the page fault code paths where it tries to
update the memory map.

8. Memory barriers

All about GP guarantees and scanning.
