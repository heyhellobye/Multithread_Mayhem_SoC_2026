/*
 * philosophers.c
 *
 * Week 2, Problem 4 — Dining philosophers, FIXED with lock ordering.
 *
 * Same setup as philosophers_broken.c: 5 philosophers, 5 forks, each
 * philosopher needs both adjacent forks to eat. The broken version
 * deadlocked because every philosopher grabbed "left, then right,"
 * and left/right are relative to each philosopher — which means it's
 * possible (and, with everyone grabbing left first, essentially
 * guaranteed) to form a cycle: 0 waits on 1, 1 waits on 2, 2 waits on
 * 3, 3 waits on 4, 4 waits on 0.
 *
 * THE FIX: lock ordering.
 * ------------------------
 * Forks are numbered 0..4. Instead of "left then right," every
 * philosopher acquires the LOWER-NUMBERED fork first, regardless of
 * whether it's their left or right fork. Concretely, for philosopher
 * `id` with adjacent forks `left = id` and `right = (id+1) % 5`:
 *   - for philosophers 0..3: left < right, so they lock left then right
 *     (same as before).
 *   - for philosopher 4: left = 4, right = 0, so 4 is now forced to
 *     lock fork 0 FIRST, then fork 4 — the opposite order of what the
 *     naive version did.
 *
 * This breaks the cycle: philosopher 4 no longer competes with
 * philosopher 0 for fork 0 "second" the way it used to. Every
 * philosopher acquires locks in a single global, consistent order (by
 * fork number), so a circular-wait chain can never form — one of the
 * four Coffman conditions for deadlock is now structurally impossible,
 * regardless of thread scheduling.
 *
 * BONUS: starvation check.
 * -------------------------
 * A fixed "everyone eats exactly 10 times" loop can't actually reveal
 * starvation — every philosopher finishes eventually regardless of
 * how unevenly the forks get shared, as long as there's no deadlock.
 * To test starvation properly, this program supports a second mode:
 * run all philosophers for a fixed WALL-CLOCK duration instead of a
 * fixed meal count, then compare how many times each one ate in that
 * same time window. If one philosopher's count is far below the
 * others, that's starvation — the system as a whole made progress,
 * but access wasn't shared fairly.
 *
 * Compile:
 *   gcc -Wall -pthread philosophers.c -o philosophers
 *
 * Run (fixed 10-meals-each mode, the required deliverable):
 *   ./philosophers
 *
 * Run (starvation bonus: free-run for N seconds, compare counts):
 *   ./philosophers seconds=8
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <stdatomic.h>

#define NUM_PHILOSOPHERS 5
#define MEALS_PER_PHILOSOPHER 10

pthread_mutex_t forks[NUM_PHILOSOPHERS];
int meals_eaten[NUM_PHILOSOPHERS];   /* for the starvation bonus check */

/* When > 0, run in free-run/starvation mode for this many seconds
 * instead of stopping at MEALS_PER_PHILOSOPHER. Set once in main
 * before any threads start, so plain reads from worker threads are
 * fine (no writes happen after threads are created). */
static int run_seconds = 0;
static atomic_int stop_flag = 0;

void *philosopher(void *arg) {
    int id    = *(int *)arg;
    int left  = id;
    int right = (id + 1) % NUM_PHILOSOPHERS;

    /* Lock ordering: always acquire the lower-numbered fork first. */
    int first_fork  = (left < right) ? left  : right;
    int second_fork = (left < right) ? right : left;

    int meal = 0;
    while (run_seconds > 0 ? !atomic_load(&stop_flag)
                            : meal < MEALS_PER_PHILOSOPHER) {
        if (!run_seconds) {
            printf("[philosopher %d] thinking\n", id);
        }
        usleep(5000);

        pthread_mutex_lock(&forks[first_fork]);
        pthread_mutex_lock(&forks[second_fork]);

        if (!run_seconds) {
            printf("[philosopher %d] eating (meal %d)\n", id, meal + 1);
        }
        usleep(5000);
        meals_eaten[id]++;
        meal++;

        pthread_mutex_unlock(&forks[second_fork]);
        pthread_mutex_unlock(&forks[first_fork]);
    }

    if (!run_seconds) {
        printf("[philosopher %d] done — ate %d times\n", id, meals_eaten[id]);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc >= 2 && strncmp(argv[1], "seconds=", 8) == 0) {
        run_seconds = atoi(argv[1] + 8);
        if (run_seconds <= 0) {
            fprintf(stderr, "seconds=N must be positive\n");
            return 1;
        }
    }

    pthread_t threads[NUM_PHILOSOPHERS];
    int ids[NUM_PHILOSOPHERS];

    for (int i = 0; i < NUM_PHILOSOPHERS; i++) {
        pthread_mutex_init(&forks[i], NULL);
        meals_eaten[i] = 0;
    }

    if (run_seconds > 0) {
        printf("Starting FIXED dining philosophers in free-run mode for "
               "%d seconds (starvation check).\n\n", run_seconds);
    } else {
        printf("Starting FIXED dining philosophers (lock ordering applied).\n\n");
    }

    for (int i = 0; i < NUM_PHILOSOPHERS; i++) {
        ids[i] = i;
        pthread_create(&threads[i], NULL, philosopher, &ids[i]);
    }

    if (run_seconds > 0) {
        sleep(run_seconds);
        atomic_store(&stop_flag, 1);
    }

    for (int i = 0; i < NUM_PHILOSOPHERS; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("\nAll philosophers finished. Meals eaten per philosopher:\n");
    int min_meals = meals_eaten[0], max_meals = meals_eaten[0];
    for (int i = 0; i < NUM_PHILOSOPHERS; i++) {
        printf("  philosopher %d: %d meals\n", i, meals_eaten[i]);
        if (meals_eaten[i] < min_meals) min_meals = meals_eaten[i];
        if (meals_eaten[i] > max_meals) max_meals = meals_eaten[i];
    }

    if (run_seconds > 0) {
        /* Starvation check: everyone ran for the SAME wall-clock time,
         * so unequal counts here are a meaningful fairness signal,
         * not just scheduling noise. */
        double ratio = (max_meals > 0)
                      ? (double)min_meals / (double)max_meals
                      : 1.0;
        printf("\nFree-ran for %d seconds. min=%d meals, max=%d meals "
               "(min/max ratio = %.2f).\n", run_seconds, min_meals, max_meals, ratio);
        if (ratio < 0.5) {
            printf("Result: significant imbalance — the least-fed "
                   "philosopher ate less than half as often as the "
                   "best-fed one. That's starvation.\n");
        } else {
            printf("Result: counts are reasonably balanced — no strong "
                   "starvation observed in this run.\n");
        }
    } else {
        printf("\nEach philosopher was supposed to eat %d times.\n",
               MEALS_PER_PHILOSOPHER);
        if (min_meals == MEALS_PER_PHILOSOPHER && max_meals == MEALS_PER_PHILOSOPHER) {
            printf("Result: everyone ate exactly the required number of "
                   "times. No deadlock.\n");
        } else {
            printf("Result: FAIL — not everyone reached %d meals (should "
                   "be impossible without a deadlock in this fixed-count "
                   "mode).\n", MEALS_PER_PHILOSOPHER);
        }
    }

    for (int i = 0; i < NUM_PHILOSOPHERS; i++) {
        pthread_mutex_destroy(&forks[i]);
    }

    return 0;
}
