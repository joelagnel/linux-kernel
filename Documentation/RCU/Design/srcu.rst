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
sampled is not the right one. This can cause some implementations to keep
looping till they get the right index, potentially for a long time.

SRCU's design on the other hand does not suffer from this issue. Even though
SRCU readers have higher overhead than non-preemptible RCU (which is basically
just a compiler barrier), the number of instructions they have to execute are
fixed.  This is because SRCU carefully scans counters during grace period
detection, such that unbounded looping in the reader is not necessary.
