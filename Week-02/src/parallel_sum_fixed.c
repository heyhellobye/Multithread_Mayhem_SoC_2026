/*
 * parallel_sum_fixed.c
 *
 * Week 2, Problem 1a — Fix 03_parallel_sum.c from Week 1.
 *
 * Same setup as the original: split a large array of 1s into chunks,
 * one per thread, and have each thread add its chunk's values into a
 * shared `total`. Week 1's version raced badly on `total` (9/10 runs
 * wrong on the sandbox used for that week's report).
 *
 * THE FIX
 * -------
 * A single pthread_mutex_t (`total_lock`) protects every access to
 * `total`. Only one thread can execute `total += array[i]` at a time,
 * so the lost-update problem from Week 1 is now structurally
 * impossible — there is no window in which two threads can both read
 * the same value of `total` before either writes it back.
 *
 * This is a coarse-grained approach: we lock once per array element,
 * which is correct but slow (see the report's reflection question on
 * where the extra time goes). A faster fix — have each thread
 * accumulate into a private local variable and only touch the shared
 * `total` once at the very end — is mentioned as a "bonus" pattern in
 * Week 1's problem statement and is exactly what Problem 2's
 * bounded-buffer test does with per-thread partial sums; we don't use
 * it here on purpose, so the mutex's cost is visible for the
 * reflection question.
 *
 * Compile:
 *   gcc -Wall -pthread parallel_sum_fixed.c -o parallel_sum_fixed
 *
 * Run:
 *   ./parallel_sum_fixed
 *
 * Unlike Week 1's version, this always prints exactly ARRAY_SIZE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#define ARRAY_SIZE  10000000
#define NUM_THREADS 4

int *array;
long total = 0;
pthread_mutex_t total_lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    int thread_id;
    long start_index;
    long end_index;
} thread_arg_t;

void *partial_sum(void *arg) {
    thread_arg_t *t = (thread_arg_t *)arg;
    for (long i = t->start_index; i < t->end_index; i++) {
        pthread_mutex_lock(&total_lock);
        total += array[i];      /* critical section — now protected */
        pthread_mutex_unlock(&total_lock);
    }
    return NULL;
}

int main(void) {
    array = malloc(ARRAY_SIZE * sizeof(int));
    if (!array) {
        perror("malloc");
        return 1;
    }
    for (long i = 0; i < ARRAY_SIZE; i++) {
        array[i] = 1;
    }

    pthread_t threads[NUM_THREADS];
    thread_arg_t args[NUM_THREADS];
    long chunk = ARRAY_SIZE / NUM_THREADS;

    for (int i = 0; i < NUM_THREADS; i++) {
        args[i].thread_id   = i;
        args[i].start_index = i * chunk;
        args[i].end_index   = (i == NUM_THREADS - 1) ? ARRAY_SIZE
                                                     : (i + 1) * chunk;
        pthread_create(&threads[i], NULL, partial_sum, &args[i]);
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("Computed sum: %ld\n", total);
    printf("Expected:     %d\n",  ARRAY_SIZE);

    if (total == ARRAY_SIZE) {
        printf("Result: CORRECT — every time, no matter how many threads.\n");
    } else {
        printf("Result: WRONG by %ld — the mutex should have prevented this.\n",
               (long)ARRAY_SIZE - total);
    }

    pthread_mutex_destroy(&total_lock);
    free(array);
    return 0;
}
