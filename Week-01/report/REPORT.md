# Week 1 Report — Threads & Concurrency Basics

**Handle:** aethi254

## Problems attempted

- Problem 1 — Run, observe, write down ✅
- Problem 2 — Make the race worse ✅
- Problem 3 — Parallel word count ✅
- Problem 4 — Stretch simulation — not attempted this week

## Environment note (read this first)

`nproc` on my machine reports **1 CPU core**. This matters a lot for
everything below — several of the example programs that are "supposed"
to race almost never do here, purely because there's no second core for
a second thread to actually run on *at the same instant* as the first.
The OS still preempts and interleaves threads on a single core, but the
window in which a bad interleaving can happen is much smaller than on a
multi-core machine, especially for tight loops with no syscalls in them.
I confirmed this by comparing my runs against `sample-outputs/`, which
clearly came from a multi-core machine (race_counter is wrong on nearly
every sample run there, e.g. final counter 12,471,651 instead of
20,000,000).

## Problem 1 — Observations across 10+ runs

### `hello_thread`
Expected: 5 lines "Hello from thread N" in some order + "Main thread: all done."
Ran 10 times. 8/10 runs printed threads 0–4 in strict numeric order; 2/10
runs had a swapped pair (e.g. thread 2 and 3 swapped). So ordering is
technically non-deterministic here too, just usually not obviously so
with only 5 threads.

### `race_counter` (default ITERS = 10,000,000)
Expected final counter: 20,000,000.
**10/10 runs were CORRECT (exactly 20,000,000).** This surprised me —
see the environment note above; I believe it's the single-core
scheduler letting each thread's tight loop run in large uninterrupted
bursts, so load/increment/store rarely straddles a context switch.

Variations required by the problem statement:
- **ITERS = 1000:** 10/10 correct (2000). Even easier to stay correct — fewer
  total loop iterations means even fewer chances for a bad interleaving.
- **ITERS = 100,000,000:** 6/10 wrong this time (e.g. 196,724,396 instead
  of 200,000,000), 4/10 correct. So the race *does* exist and does show up
  once the loop is long enough to guarantee many more scheduling
  opportunities — it just needed a much bigger loop than the default to
  overcome the single-core advantage.
- **`-O2`:** 10/10 correct at the default ITERS. I checked the disassembly
  (`objdump -d`) and confirmed why: at `-O2`, gcc proves `counter` (not
  `volatile`) has no other observable effect inside the loop and collapses
  the entire 10-million-iteration loop into one `addq $10000000, counter`
  instruction. That's not the race being fixed — it's the race window
  shrinking from millions of loads/stores down to a single instruction,
  which makes a bad interleaving vastly less likely without actually
  making the program correct.

### `parallel_sum` (ARRAY_SIZE = 10,000,000, 4 threads)
Expected sum: 10,000,000.
Ran 10 times: **1/10 correct, 9/10 wrong**, and badly wrong — e.g.
7,172,164 or 2,911,313 instead of 10,000,000, off by millions each time.
Unlike `race_counter`, this one races heavily on this machine even at
default settings, since each of the 4 threads is doing millions of
`total += array[i]` operations back-to-back with no compiler collapsing
possible (array contents aren't known at compile time), giving the OS
far more opportunities to interleave badly.

### `bank_chaos`
Ran 10 times. **10/10 runs ended with balance exactly 0**, but the
success counts never summed the way you'd hope for correctness. Every
single run showed: Alice 10,000 successful / 90,000 rejected, Bob 0
successful / 100,000 rejected. In other words, on this single-core
machine, the first thread created (Alice) consistently ran essentially
to completion — draining the account to zero — before the scheduler ever
gave Bob's thread meaningful CPU time. Bob's thread then found the
account already empty for its entire run. Balance is 0, which naively
"looks correct," but the *reason* it's 0 has nothing to do with fair
concurrent access — it's really closer to sequential execution in
disguise, not the interleaved chaos the exercise expects.

## Problem 2 — Making the race worse

File: `submissions/aethi254/problem-2/race_amplified.c`

What I changed:
1. Split `counter++` into its three explicit steps (load into `tmp`,
   increment `tmp`, store `tmp` back to `counter`).
2. Inserted `usleep(1)` between the load and the store.
3. Reduced `ITERS` from 10,000,000 to 2,000 (necessary — at 1μs of sleep
   per iteration, the original iteration count would take hours).

Why this works, specifically on this single-core machine: the default
`race_counter.c` almost never races here because a tight loop rarely
gets preempted mid-instruction on one core — one thread just runs to
completion in a burst. `usleep()` is a blocking syscall: it forces the
calling thread to voluntarily *give up* the CPU right in the middle of
the load-then-store window, guaranteeing the scheduler runs the other
thread at exactly the moment it can do the most damage — read the same
stale value and stomp on our write when it comes back.

Result: **10/10 runs wrong**, final counter consistently in the
2000–2005 range instead of the expected 4000 — i.e. almost every single
increment from one thread or the other was lost. That matches the
success criterion in the problem statement (result close to `ITERS`
rather than `2*ITERS`).

I avoided `printf` inside the loop per the hint — it does its own
internal locking and would have partially serialized the threads,
hiding the race instead of amplifying it.

## Problem 3 — Parallel word count

File: `submissions/aethi254/problem-3/word_count.c`

**Word definition:** a maximal run of non-whitespace characters (same
definition `wc -w` uses).

**Note on the test file:** the network in my sandbox only allows a
short domain allowlist and `gutenberg.org` isn't on it, so I couldn't
download Moby-Dick as suggested. Instead I generated a synthetic
1,000,000-word text file (`sample.txt`, ~5.2 MB, random dictionary words
separated by spaces/newlines) and verified its true count with `wc -w`.
If you'd like, I can redo this against a real Gutenberg text once you
give me a file directly.

**Deliberately racy total**, per the instructions — every thread adds
its partial count straight into a shared `total`, no locking.

Results, `wc -w` says 1,000,000:

| Threads | 3 sample runs |
|---|---|
| 1 | 1,000,000 / 1,000,000 / 1,000,000 |
| 2 | 1,000,000 / 1,000,000 / 1,000,000 |
| 4 | 1,000,000 / 1,000,000 / 1,000,000 |
| 8 | 1,000,000 / 1,000,000 / 1,000,000 |
| 16 (15 runs) | 1,000,000 every single time |

Every run matched `wc -w` exactly, even at 16 threads over 15 runs.
This is consistent with what I saw in `race_counter` at default
settings: on a single core, each thread's critical section here is a
single `total += partial` — one add, executed once per thread, not in a
loop — so there just isn't a meaningfully long window for two threads'
adds to land on top of each other. I'd genuinely expect this to race on
a multi-core machine, especially with 16 threads all finishing their
(unsynchronized) scans around the same time.

## One thing that surprised me

How much a single CPU core changed the entire picture. I went in
expecting `race_counter` to fail almost every run — that's the whole
point of the demo — and instead had to push ITERS up 10x to get it to
misbehave at all. `bank_chaos` was even more striking: instead of chaotic
alternating withdrawals, one thread reliably ran to near-completion before
the other got real CPU time, which produced a "clean" balance of 0 for
the wrong reason. It made the "note on your machine" section of the
problem statement feel very real rather than like a disclaimer nobody
hits.

## What I got stuck on

Getting `race_counter` to fail at all took some digging — my first
instinct was "the code must be wrong," but comparing against
`sample-outputs/` (which clearly came from a multi-core box) and then
confirming `nproc` = 1 made it clear the machine, not the code, was the
reason. Worth flagging for mentorship: might be useful to explicitly ask
mentees to check `nproc` at the very start of the week.
