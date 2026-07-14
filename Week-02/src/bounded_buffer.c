/*
 * bounded_buffer.c
 *
 * Week 2, Problem 2 — Thread-safe bounded buffer.
 *
 * A generic, reusable bounded buffer of ints, safe to call from any
 * number of producer and consumer threads simultaneously. Built the
 * same way as 03_producer_consumer.c (one mutex + two condition
 * variables), but wrapped behind an API where the caller never sees
 * the synchronization:
 *
 *   void buffer_init(int capacity);
 *   void buffer_put(int item);   // blocks if full
 *   int  buffer_get(void);       // blocks if empty
 *   void buffer_destroy(void);
 *
 * TEST DESIGN
 * -----------
 * 3 producer threads, 2 consumer threads.
 *   - Producer N (N = 0, 1, 2) produces the 1000 integers
 *     N*1000 .. N*1000+999.
 *   - Each consumer thread accumulates a running count and sum of
 *     everything it takes out of the buffer.
 *   - Producers signal they're done by each incrementing a shared
 *     `producers_done` counter (protected by the same buffer lock)
 *     after their last buffer_put. Consumers stop pulling once
 *     producers_done == NUM_PRODUCERS AND the buffer is empty — that
 *     condition is checked with the SAME lock/condvar the buffer
 *     already uses, so there's no separate race to reason about.
 *   - After both consumers finish, main sums their counts and totals
 *     and checks them against the known expected count (3000 items)
 *     and expected sum (0+1+...+2999 = 4498500).
 *
 * EDGE CASES CONSIDERED (see report for more)
 *   - Capacity 1: run the whole test again with buffer_init(1).
 *     Correctness must hold — it just serializes producers and
 *     consumers much more tightly (heavy blocking on both sides).
 *   - signal vs broadcast: BUFFER_USE_BROADCAST toggles between
 *     pthread_cond_signal and pthread_cond_broadcast on both condition
 *     variables, to compare correctness and, informally, performance
 *     with multiple consumers.
 *
 * Compile:
 *   gcc -Wall -pthread bounded_buffer.c -o bounded_buffer
 *
 * Run:
 *   ./bounded_buffer            # default capacity, signal
 *   ./bounded_buffer 1          # capacity-1 edge case
 *   ./bounded_buffer 5 broadcast
 */

#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

/* ------------------------------------------------------------------ */
/* Generic thread-safe bounded buffer of ints                         */
/* ------------------------------------------------------------------ */

static int  *buf_data     = NULL;
static int   buf_capacity = 0;
static int   buf_count    = 0;   /* items currently in the buffer */
static int   buf_in       = 0;   /* next slot to write */
static int   buf_out      = 0;   /* next slot to read  */
static int   buf_use_broadcast = 0;   /* set at runtime for the experiment */

static pthread_mutex_t buf_lock       = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  buf_not_full   = PTHREAD_COND_INITIALIZER;
static pthread_cond_t  buf_not_empty  = PTHREAD_COND_INITIALIZER;

void buffer_init(int capacity) {
    buf_data     = malloc(capacity * sizeof(int));
    buf_capacity = capacity;
    buf_count    = 0;
    buf_in       = 0;
    buf_out      = 0;
}

void buffer_put(int item) {
    pthread_mutex_lock(&buf_lock);

    /* WHILE, not IF: guards against spurious wakeups and against the
     * condition having changed again by the time we're rescheduled. */
    while (buf_count == buf_capacity) {
        pthread_cond_wait(&buf_not_full, &buf_lock);
    }

    buf_data[buf_in] = item;
    buf_in = (buf_in + 1) % buf_capacity;
    buf_count++;

    if (buf_use_broadcast) {
        pthread_cond_broadcast(&buf_not_empty);
    } else {
        pthread_cond_signal(&buf_not_empty);
    }

    pthread_mutex_unlock(&buf_lock);
}

int buffer_get(void) {
    pthread_mutex_lock(&buf_lock);

    while (buf_count == 0) {
        pthread_cond_wait(&buf_not_empty, &buf_lock);
    }

    int item = buf_data[buf_out];
    buf_out = (buf_out + 1) % buf_capacity;
    buf_count--;

    if (buf_use_broadcast) {
        pthread_cond_broadcast(&buf_not_full);
    } else {
        pthread_cond_signal(&buf_not_full);
    }

    pthread_mutex_unlock(&buf_lock);
    return item;
}

void buffer_destroy(void) {
    free(buf_data);
    buf_data = NULL;
}

/*
 * A separate piece of shared state — how many producers have finished
 * — protected by the SAME lock the buffer already uses, and announced
 * through the SAME not_empty condition variable, so consumers waiting
 * on "is there work OR are we done" only ever have to check one
 * condition variable's worth of predicate.
 */
static int producers_done  = 0;
static int NUM_PRODUCERS_G = 0;

void producer_finished(void) {
    pthread_mutex_lock(&buf_lock);
    producers_done++;
    /* Wake up anyone waiting on not_empty so they can re-check the
     * "are we entirely done" predicate even if the buffer is empty. */
    pthread_cond_broadcast(&buf_not_empty);
    pthread_mutex_unlock(&buf_lock);
}

/* Returns 1 if a consumer should keep trying to get an item, 0 if
 * every producer is done AND the buffer is drained (i.e. truly no
 * more work will ever arrive). Must be called WITHOUT holding
 * buf_lock; it takes the lock itself. */
int work_remains(void) {
    pthread_mutex_lock(&buf_lock);
    int remains = !(producers_done == NUM_PRODUCERS_G && buf_count == 0);
    pthread_mutex_unlock(&buf_lock);
    return remains;
}

/*
 * A get that returns -1 (sentinel; our test never produces negative
 * numbers) instead of blocking forever once all producers are done
 * and the buffer is empty.
 */
int buffer_get_or_done(void) {
    pthread_mutex_lock(&buf_lock);
    while (buf_count == 0 && producers_done < NUM_PRODUCERS_G) {
        pthread_cond_wait(&buf_not_empty, &buf_lock);
    }
    if (buf_count == 0) {
        /* producers are all done and buffer is empty: no more work */
        pthread_mutex_unlock(&buf_lock);
        return -1;
    }
    int item = buf_data[buf_out];
    buf_out = (buf_out + 1) % buf_capacity;
    buf_count--;

    if (buf_use_broadcast) {
        pthread_cond_broadcast(&buf_not_full);
    } else {
        pthread_cond_signal(&buf_not_full);
    }

    pthread_mutex_unlock(&buf_lock);
    return item;
}

/* ------------------------------------------------------------------ */
/* Test: 3 producers, 2 consumers                                     */
/* ------------------------------------------------------------------ */

#define NUM_PRODUCERS 3
#define NUM_CONSUMERS 2
#define ITEMS_PER_PRODUCER 1000

typedef struct {
    int producer_id;
} producer_arg_t;

typedef struct {
    long consumed_count;
    long consumed_sum;
} consumer_result_t;

void *producer_thread(void *arg) {
    producer_arg_t *p = (producer_arg_t *)arg;
    int base = p->producer_id * ITEMS_PER_PRODUCER;
    for (int i = 0; i < ITEMS_PER_PRODUCER; i++) {
        buffer_put(base + i);
    }
    producer_finished();
    return NULL;
}

void *consumer_thread(void *arg) {
    consumer_result_t *r = (consumer_result_t *)arg;
    r->consumed_count = 0;
    r->consumed_sum   = 0;

    while (1) {
        int item = buffer_get_or_done();
        if (item == -1) {
            break;
        }
        r->consumed_count++;
        r->consumed_sum += item;
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    int capacity = 8;
    if (argc >= 2) {
        capacity = atoi(argv[1]);
        if (capacity < 1) {
            fprintf(stderr, "capacity must be >= 1\n");
            return 1;
        }
    }
    if (argc >= 3 && strcmp(argv[2], "broadcast") == 0) {
        buf_use_broadcast = 1;
    }

    printf("Bounded buffer test: capacity=%d, wake-strategy=%s\n",
           capacity, buf_use_broadcast ? "broadcast" : "signal");

    buffer_init(capacity);
    NUM_PRODUCERS_G = NUM_PRODUCERS;
    producers_done  = 0;

    pthread_t producers[NUM_PRODUCERS];
    pthread_t consumers[NUM_CONSUMERS];
    producer_arg_t     p_args[NUM_PRODUCERS];
    consumer_result_t  c_results[NUM_CONSUMERS];

    for (int i = 0; i < NUM_PRODUCERS; i++) {
        p_args[i].producer_id = i;
        pthread_create(&producers[i], NULL, producer_thread, &p_args[i]);
    }
    for (int i = 0; i < NUM_CONSUMERS; i++) {
        pthread_create(&consumers[i], NULL, consumer_thread, &c_results[i]);
    }

    for (int i = 0; i < NUM_PRODUCERS; i++) {
        pthread_join(producers[i], NULL);
    }
    for (int i = 0; i < NUM_CONSUMERS; i++) {
        pthread_join(consumers[i], NULL);
    }

    long total_count = 0;
    long total_sum   = 0;
    for (int i = 0; i < NUM_CONSUMERS; i++) {
        printf("Consumer %d: consumed %ld items, sum %ld\n",
               i, c_results[i].consumed_count, c_results[i].consumed_sum);
        total_count += c_results[i].consumed_count;
        total_sum   += c_results[i].consumed_sum;
    }

    long expected_count = (long)NUM_PRODUCERS * ITEMS_PER_PRODUCER;
    long max_item = expected_count - 1;
    long expected_sum = max_item * (max_item + 1) / 2;   /* 0+1+...+max_item */

    printf("Total consumed count: %ld (expected %ld)\n", total_count, expected_count);
    printf("Total consumed sum:   %ld (expected %ld)\n", total_sum, expected_sum);

    int ok = 1;
    if (total_count != expected_count) {
        printf("FAIL: consumed count mismatch — items lost or duplicated.\n");
        ok = 0;
    }
    if (total_sum != expected_sum) {
        printf("FAIL: consumed sum mismatch — items lost, duplicated, or corrupted.\n");
        ok = 0;
    }
    if (ok) {
        printf("PASS: every item produced was consumed exactly once.\n");
    }

    buffer_destroy();
    pthread_mutex_destroy(&buf_lock);
    pthread_cond_destroy(&buf_not_full);
    pthread_cond_destroy(&buf_not_empty);

    return ok ? 0 : 1;
}
