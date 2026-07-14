/*
 * race_amplified.c
 *
 * Week 1, Problem 2 — Make the race worse.
 *
 * This is 02_race_counter.c with the increment deliberately widened
 * so the race condition shows up on (almost) every single run,
 * instead of only occasionally.
 *
 * WHAT CHANGED FROM THE ORIGINAL
 * -------------------------------
 * 1. counter++ was split into its three logical steps:
 *        long tmp = counter;   // load
 *        tmp = tmp + 1;        // add
 *        counter = tmp;        // store
 *    This alone does not make the race worse — it's exactly what
 *    counter++ already compiles down to. It just makes the gap explicit
 *    so we have somewhere to put a delay.
 *
 * 2. A usleep(1) call was inserted between the load and the store.
 *    This is the actual amplification. On this machine (a single-core
 *    sandbox, confirmed with `nproc` = 1), a tight loop of counter++
 *    almost never gets preempted mid-instruction-sequence — one thread
 *    tends to run to completion (or close to it) before the other gets
 *    scheduled, so the plain race_counter.c rarely shows a wrong
 *    answer here. usleep() is a blocking syscall: it forces the
 *    calling thread to voluntarily give up the CPU, which guarantees
 *    the OS scheduler gets a chance to run the other thread right in
 *    the middle of our own load-then-store window. That is precisely
 *    the moment where the other thread can sneak in, read the same
 *    stale value of `counter`, and stomp on our update when it writes
 *    back.
 *
 * 3. ITERS was reduced drastically (from 10,000,000 to 2,000). This is
 *    necessary, not optional: a 1-microsecond sleep per iteration
 *    means 2 threads * 10,000,000 iterations would take on the order
 *    of hours. At ITERS=2000 the whole program still finishes in a
 *    couple of seconds while forcing thousands of preemption
 *    opportunities.
 *
 * We deliberately avoided printf() inside the loop, per the hint in
 * the problem statement — printf performs its own internal locking to
 * stay thread-safe, which would incidentally serialize/synchronize
 * our threads and hide the very race we're trying to amplify.
 *
 * EXPECTED RESULT
 * ---------------
 * Expected final value: 2 * ITERS = 4000
 * Actual final value on this machine: consistently far below 4000,
 * frequently close to ITERS (2000) itself — meaning almost every
 * single increment from one thread or the other is being lost.
 *
 * Compile:
 *   gcc -Wall -Wextra -pthread race_amplified.c -o race_amplified
 *
 * Run multiple times:
 *   for i in $(seq 1 10); do ./race_amplified; done
 */

#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

#define ITERS 2000

/* SHARED global counter, exactly like the original. */
long counter = 0;

void *increment(void *arg) {
    (void)arg;
    for (long i = 0; i < ITERS; i++) {
        long tmp = counter;   /* load */
        usleep(1);            /* widen the gap: force a scheduler yield */
        tmp = tmp + 1;        /* add  */
        counter = tmp;        /* store */
    }
    return NULL;
}

int main(void) {
    pthread_t t1, t2;

    pthread_create(&t1, NULL, increment, NULL);
    pthread_create(&t2, NULL, increment, NULL);

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    long expected = 2L * ITERS;
    printf("Final counter:  %ld\n", counter);
    printf("Expected:       %ld\n", expected);
    printf("Difference:     %ld\n", expected - counter);

    if (counter == expected) {
        printf("Result: CORRECT (rare at this amplification level — try again)\n");
    } else {
        printf("Result: WRONG by %ld — race condition amplified successfully.\n",
               expected - counter);
    }

    return 0;
}
