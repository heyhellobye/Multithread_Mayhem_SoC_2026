# Week 2 Report — Synchronization

**Handle:** aethi254

## Problems attempted

- Problem 1 — Fix last week's mess ✅
- Problem 2 — Thread-safe bounded buffer ✅
- Problem 3 — Thread-safe linked list ✅
- Problem 4 — Dining philosophers ✅ (including the starvation bonus)

## Environment note

Same sandbox as Week 1: `nproc` = 1 (single core). This mattered a lot
less this week — mutexes and condition variables force correct
behavior regardless of scheduling, so single-core vs multi-core only
shows up in the *timing* comparisons (Problem 1's reflection question),
not in correctness.

## Problem 1 — Fixing `parallel_sum` and `bank_chaos`

Files: `problem-1/parallel_sum_fixed.c`, `problem-1/bank_chaos_fixed.c`

Both wrap their previously-racy critical section in a single
`pthread_mutex_t`:
- `parallel_sum_fixed.c` locks around each `total += array[i]`.
- `bank_chaos_fixed.c` locks around the *entire* check-then-act
  (`if (balance >= amount) { balance -= amount; ... }`), which removes
  the TOCTOU (time-of-check-to-time-of-use) gap that let both
  customers see a stale, pre-withdrawal balance last week.

**20-run results:**
- `parallel_sum_fixed`: **20/20 runs exactly 10,000,000.** No variation
  at all, unlike Week 1's racy version (9/10 wrong there).
- `bank_chaos_fixed`: **20/20 runs correct** — balance always ended at
  0, and this time the accounting actually checks out:
  `starting_balance − (alice.successful + bob.successful) == final_balance`
  holds on every run (I added that explicit check in the program since
  Week 1's version only eyeballed "is balance 0", which turned out to
  be true for the wrong reason on this machine — see Week 1's report).

**Reflection question — why is the fixed version slower?**
Timed both directly:

| Version | 3-run times (`real`) |
|---|---|
| Racy (Week 1's `03_parallel_sum.c`) | 0.088s, 0.064s, 0.065s |
| Fixed (mutex around every add) | 0.224s, 0.224s, 0.224s |

The fixed version is consistently **~3x slower**. The extra time isn't
"computation" — it's contention overhead. Every single array element
now requires a full lock/unlock pair (two syscall-adjacent operations
with memory barriers) around what used to be one plain memory add.
With 10,000,000 elements and 4 threads all fighting over the *same*
single mutex, most of the added time is threads blocking on the lock
and the kernel doing futex wake-ups, not any actual arithmetic. This is
exactly the coarse-grained-locking cost the theory material warns
about — locking once per element is correct but pays for
synchronization far more often than necessary. (A per-thread local
accumulator, added to the shared total only once at the end, would
avoid almost all of this cost — that's the "bonus" pattern the Week 1
problem statement hinted at, deliberately not used here so the cost of
naive locking would be visible for this question.)

## Problem 2 — Thread-safe bounded buffer

File: `problem-2/bounded_buffer.c`

Generic `buffer_init/put/get/destroy` API backed by one mutex + two
condition variables (`not_full`, `not_empty`), same shape as
`03_producer_consumer.c` but with the synchronization hidden behind
the four functions.

**Test:** 3 producers (each producing 1000 known integers —
producer N makes `N*1000 .. N*1000+999`) and 2 consumers, each
accumulating a running count and sum. Producers signal completion
through a `producers_done` counter guarded by the same lock; consumers
stop once all producers are done *and* the buffer is drained (checked
under the same lock, so there's no separate race around "are we
finished").

After joining, main checks total consumed count (expected 3000) and
total consumed sum (expected 4,498,500, i.e. 0+1+...+2999) against the
known values.

**Results across configurations, 10 runs each:**

| Configuration | Result |
|---|---|
| Default capacity (8), signal | 10/10 PASS |
| Capacity = 1 (tightest possible sync) | 10/10 PASS |
| Capacity = 8, broadcast instead of signal | 10/10 PASS |

**30/30 total runs passed** — every item produced was consumed exactly
once, count and sum both matched, in every configuration.

**Edge cases considered:**
- **Capacity 1** — forces producer and consumer into near-lockstep;
  this is the tightest possible synchronization case and the one most
  likely to expose an off-by-one in the wraparound (`in`/`out`
  indices) or a missed wakeup. It passed every time.
- **`signal` vs `broadcast`** — both are correct here (only one
  consumer *needs* to wake per `not_empty` signal, and only one
  producer needs to wake per `not_full` signal, since exactly one slot
  opens/fills per operation), so I expected no correctness difference,
  and saw none. I also timed both, but with only 2 consumer threads
  and 3000 total items the timings were within noise (single-digit
  milliseconds either way) — I don't think this test is large enough
  to show broadcast's extra wakeup cost. I'd expect a measurable gap
  with many more consumers all waking up unnecessarily on every
  `broadcast`, but didn't have time to push the thread count up this
  week.

## Problem 3 — Thread-safe linked list

File: `problem-3/safe_list.c`

Singly-linked list with **one coarse-grained lock** for the whole
structure (as recommended), stored next to the head pointer rather
than inside individual nodes — a per-node lock wouldn't protect the
head pointer itself or multi-node operations like `remove`.

**Test:** 8 worker threads, 1000 random ops each (insert/contains/remove,
uniformly chosen), values restricted to `[0, 99]` so threads constantly
collide on the same values — that's the point, it's what actually
exercises the lock rather than just running 8 independent lists in
parallel by accident.

**Correctness checks after the stress test:**
1. Walk the list end-to-end with a generous upper bound on length as a
   crude cycle detector (catches corruption/infinite loops).
2. Insert a fresh, distinct value after all workers join; confirm
   `list_contains` finds it.
3. Confirm a value that was never inserted anywhere (a large sentinel,
   well outside the workers' `[0,99]` range) is correctly reported
   absent.

I deliberately did **not** try to assert the exact final contents of
the list — with 8 threads inserting/removing overlapping values
concurrently, the final state is inherently nondeterministic and isn't
a bug; the structural checks above are what should hold regardless.

**10/10 runs: PASS**, no crashes, no corrupted/cyclic list ever
observed. Final list lengths varied a little between runs (556–563
nodes out of up to 8000 possible insert operations), which is expected
given the random mix of inserts/removes racing on the same value range.

## Problem 4 — Dining philosophers

Files: `problem-4/philosophers_broken.c` (not submitted, kept for
reference), `problem-4/philosophers.c` (the actual deliverable)

**What the broken version did:** ran it under `timeout 4` with
line-buffered stdout to actually capture the moment it froze. Output
showed exactly the textbook deadlock:

```
[philosopher 4] picking up left fork 4
[philosopher 3] picking up left fork 3
[philosopher 2] picking up left fork 2
[philosopher 1] picking up left fork 1
[philosopher 0] picking up left fork 0
[philosopher 0] picking up right fork 1
[philosopher 1] picking up right fork 2
[philosopher 2] picking up right fork 3
[philosopher 3] picking up right fork 4
[philosopher 4] picking up right fork 0
```
...and then nothing. All five had grabbed their left fork, then every
single one blocked forever trying for their right fork — a perfect
circular wait (0→1→2→3→4→0). `timeout` had to kill it (exit code 124);
the program never printed anything after that last line.

**What I changed to fix it:** lock ordering. Instead of "left fork,
then right fork" (relative to each philosopher, so direction differs
around the table), every philosopher now computes
`first_fork = min(left, right)`, `second_fork = max(left, right)` and
always locks the lower-numbered fork first. This makes philosopher 4 —
whose "left" (fork 4) is numerically *higher* than its "right"
(fork 0) — lock fork 0 before fork 4, breaking the cycle that the
naive version always formed. With a single global lock-acquisition
order, a circular wait can never form again, independent of
scheduling.

**Verification:** ran the fixed version 6 times total (5 quick + the
one shown above); every single run completed and printed "everyone ate
exactly the required number of times. No deadlock." Total meals always
came out to exactly 5×10=50 across the philosophers, and the process
exited normally every time — a sharp contrast to the broken version,
which never exits on its own.

**Starvation bonus:** added a `seconds=N` free-run mode (loop until a
shared stop flag is set after N wall-clock seconds, instead of
stopping at a fixed meal count — a fixed count can't reveal starvation
since everyone finishes eventually regardless of fairness). Ran it for
8 seconds twice:

| Run | Meal counts (philosophers 0–4) | min/max ratio |
|---|---|---|
| 1 | (not logged individually) | 578 / 664 = 0.87 |
| 2 | 581, 634, 655, 670, 581 | 581 / 670 = 0.87 |

Ratios around 0.87 both times — some spread, but nothing close to one
philosopher being starved out. I'd call this "reasonably fair, mildly
uneven," not real starvation. Makes sense: with lock ordering and short
hold times, the scheduler doesn't have much room to consistently favor
one philosopher over another on this workload.

## One thing that surprised me

How large the mutex overhead was in Problem 1 — a 3x slowdown for
locking around a single `+=` felt bigger than I expected going in. It
made the theory material's point about lock granularity click in a way
just reading about it hadn't.

## What I got stuck on

Getting the "starvation bonus" to actually mean something took a
rethink — my first version reused the fixed-meal-count loop and of
course every philosopher always finished with an identical meal count,
which is a useless signal. I only got a real fairness comparison once
I switched to running everyone for the same wall-clock duration and
comparing counts within that shared window.
