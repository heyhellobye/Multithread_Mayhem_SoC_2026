/*
 * philosophers_broken.c
 *
 * Week 2, Problem 4 — Dining philosophers, BROKEN (naive) version.
 *
 * NOT the submission — kept alongside philosophers.c purely so the
 * "what did the broken version do" part of the report is backed by
 * real, reproducible code and output rather than a description from
 * memory. philosophers.c is the fixed, deadlock-free file to submit.
 *
 * Five philosophers, five forks arranged in a circle. Each philosopher
 * needs both adjacent forks to eat. Naive strategy: everyone picks up
 * their LEFT fork first, then their RIGHT fork.
 *
 * This deadlocks classically: if all five grab their left fork at
 * roughly the same time, every philosopher is holding one fork and
 * waiting for a neighbor's fork that will never be released — a
 * circular wait, one of the four Coffman conditions.
 *
 * Compile:
 *   gcc -Wall -pthread philosophers_broken.c -o philosophers_broken
 *
 * Run — and be ready to Ctrl+C, it deadlocks within a second or two:
 *   ./philosophers_broken
 */

#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

#define NUM_PHILOSOPHERS 5

pthread_mutex_t forks[NUM_PHILOSOPHERS];

void *philosopher(void *arg) {
    int id    = *(int *)arg;
    int left  = id;
    int right = (id + 1) % NUM_PHILOSOPHERS;

    for (int meal = 0; meal < 10; meal++) {
        printf("[philosopher %d] thinking\n", id);
        usleep(10000);

        printf("[philosopher %d] picking up left fork %d\n", id, left);
        pthread_mutex_lock(&forks[left]);

        /* Give everyone time to grab their left fork before anyone
         * tries for their right — this guarantees the deadlock shows
         * up quickly and reliably instead of only occasionally. */
        usleep(10000);

        printf("[philosopher %d] picking up right fork %d\n", id, right);
        pthread_mutex_lock(&forks[right]);   /* <-- hangs here forever */

        printf("[philosopher %d] eating (meal %d)\n", id, meal);
        usleep(10000);

        pthread_mutex_unlock(&forks[right]);
        pthread_mutex_unlock(&forks[left]);
    }

    printf("[philosopher %d] done eating 10 times\n", id);
    return NULL;
}

int main(void) {
    pthread_t threads[NUM_PHILOSOPHERS];
    int ids[NUM_PHILOSOPHERS];

    for (int i = 0; i < NUM_PHILOSOPHERS; i++) {
        pthread_mutex_init(&forks[i], NULL);
    }

    printf("Starting BROKEN dining philosophers. Watch it deadlock, "
           "then Ctrl+C.\n\n");

    for (int i = 0; i < NUM_PHILOSOPHERS; i++) {
        ids[i] = i;
        pthread_create(&threads[i], NULL, philosopher, &ids[i]);
    }
    for (int i = 0; i < NUM_PHILOSOPHERS; i++) {
        pthread_join(threads[i], NULL);   /* main hangs here too */
    }

    printf("This line should never print.\n");
    return 0;
}
