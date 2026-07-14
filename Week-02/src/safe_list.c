/*
 * safe_list.c
 *
 * Week 2, Problem 3 — Thread-safe singly-linked list.
 *
 * A singly-linked list of ints, safe to insert/lookup/remove from any
 * number of threads concurrently. Uses ONE coarse-grained mutex for
 * the whole list (as the problem statement explicitly recommends),
 * stored alongside the head pointer rather than inside individual
 * nodes — a lock inside each node wouldn't protect operations that
 * touch the head pointer or that span multiple nodes (like remove,
 * which has to update the previous node's `next`).
 *
 * API:
 *   void list_init(void);
 *   void list_insert(int value);
 *   int  list_contains(int value);
 *   void list_remove(int value);
 *   void list_destroy(void);
 *
 * TEST DESIGN
 * -----------
 * 8 worker threads, each performing 1000 random operations
 * (insert / contains / remove) on values in [0, 99], so threads
 * frequently collide on the same values — that's on purpose, it's
 * what actually exercises the lock.
 *
 * After all threads finish we check:
 *   1. The list is well-formed: walking from head to NULL terminates
 *      within a sane number of steps (catches cycles/corruption).
 *   2. A value we insert AFTER all worker threads have joined and
 *      immediately look up is found (list_contains returns 1).
 *   3. A value we know we never insert anywhere (a huge sentinel,
 *      outside the workers' [0,99] range) is correctly reported absent.
 *
 * Note: because 8 threads are inserting/removing the SAME range of
 * values concurrently and unpredictably, we can't assert exactly
 * which values of [0,99] remain in the list at the end — that's
 * inherently nondeterministic and is not a bug. What we CAN and DO
 * assert deterministically is structural correctness (no crash, no
 * corruption, no infinite loop) plus the two targeted before/after
 * checks above.
 *
 * Compile:
 *   gcc -Wall -pthread safe_list.c -o safe_list
 *
 * Run:
 *   ./safe_list
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

/* ------------------------------------------------------------------ */
/* Thread-safe singly-linked list                                     */
/* ------------------------------------------------------------------ */

typedef struct node {
    int value;
    struct node *next;
} node_t;

static node_t *list_head = NULL;

/* ONE lock for the whole list. It lives here, next to the head
 * pointer it protects, not inside any individual node. */
static pthread_mutex_t list_lock = PTHREAD_MUTEX_INITIALIZER;

void list_init(void) {
    pthread_mutex_lock(&list_lock);
    list_head = NULL;
    pthread_mutex_unlock(&list_lock);
}

void list_insert(int value) {
    node_t *n = malloc(sizeof(node_t));
    n->value = value;

    pthread_mutex_lock(&list_lock);
    n->next = list_head;    /* insert at head — O(1), simplest */
    list_head = n;
    pthread_mutex_unlock(&list_lock);
}

int list_contains(int value) {
    pthread_mutex_lock(&list_lock);
    node_t *cur = list_head;
    while (cur != NULL) {
        if (cur->value == value) {
            pthread_mutex_unlock(&list_lock);
            return 1;
        }
        cur = cur->next;
    }
    pthread_mutex_unlock(&list_lock);
    return 0;
}

void list_remove(int value) {
    pthread_mutex_lock(&list_lock);

    node_t *cur  = list_head;
    node_t *prev = NULL;

    while (cur != NULL) {
        if (cur->value == value) {
            if (prev == NULL) {
                list_head = cur->next;
            } else {
                prev->next = cur->next;
            }
            free(cur);
            /* Remove only the first match — matches list semantics
             * where duplicate values may exist and we only remove
             * one occurrence per call, same as a typical list API. */
            pthread_mutex_unlock(&list_lock);
            return;
        }
        prev = cur;
        cur  = cur->next;
    }

    pthread_mutex_unlock(&list_lock);
    /* value not found: no-op, same as most remove() APIs */
}

void list_destroy(void) {
    pthread_mutex_lock(&list_lock);
    node_t *cur = list_head;
    while (cur != NULL) {
        node_t *next = cur->next;
        free(cur);
        cur = next;
    }
    list_head = NULL;
    pthread_mutex_unlock(&list_lock);
}

/* Walk the list and return its length, or -1 if it looks corrupted
 * (walked further than any sane test could have produced — a crude
 * cycle detector for the "well-formed" check). */
long list_length_bounded(long max_reasonable) {
    pthread_mutex_lock(&list_lock);
    long len = 0;
    node_t *cur = list_head;
    while (cur != NULL) {
        len++;
        if (len > max_reasonable) {
            pthread_mutex_unlock(&list_lock);
            return -1;   /* almost certainly a cycle */
        }
        cur = cur->next;
    }
    pthread_mutex_unlock(&list_lock);
    return len;
}

/* ------------------------------------------------------------------ */
/* Stress test: 8 threads, 1000 mixed random ops each                 */
/* ------------------------------------------------------------------ */

#define NUM_THREADS    8
#define OPS_PER_THREAD 1000
#define VALUE_RANGE    100     /* values 0..99 */
#define NEVER_INSERTED 999999  /* outside VALUE_RANGE, used as sentinel */

void *worker(void *arg) {
    unsigned int seed = (unsigned int)(size_t)arg;   /* per-thread RNG state */

    for (int i = 0; i < OPS_PER_THREAD; i++) {
        int value = rand_r(&seed) % VALUE_RANGE;
        int op    = rand_r(&seed) % 3;

        switch (op) {
            case 0: list_insert(value);       break;
            case 1: (void)list_contains(value); break;
            case 2: list_remove(value);       break;
        }
    }
    return NULL;
}

int main(void) {
    list_init();

    pthread_t threads[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++) {
        /* pass a distinct seed per thread via the pointer value */
        pthread_create(&threads[i], NULL, worker, (void *)(size_t)(i + 1));
    }
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("All %d worker threads finished %d ops each without crashing.\n",
           NUM_THREADS, OPS_PER_THREAD);

    /* Check 1: well-formed list (no cycle, terminates). Upper bound is
     * generous: at most NUM_THREADS * OPS_PER_THREAD nodes could ever
     * have been inserted in total. */
    long len = list_length_bounded((long)NUM_THREADS * OPS_PER_THREAD + 10);
    if (len < 0) {
        printf("FAIL: list appears corrupted (cycle or unreasonable length).\n");
        return 1;
    }
    printf("List is well-formed, length = %ld.\n", len);

    /* Check 2: insert a fresh value now that all workers are done,
     * and confirm we can find it. */
    int fresh_value = 424242;
    list_insert(fresh_value);
    int found_fresh = list_contains(fresh_value);
    printf("Just inserted %d, list_contains says: %s\n",
           fresh_value, found_fresh ? "FOUND (correct)" : "NOT FOUND (BUG)");

    /* Check 3: a value that was never inserted anywhere (workers only
     * ever touch [0, 99], and fresh_value is different too) should be
     * reported absent. */
    int found_never = list_contains(NEVER_INSERTED);
    printf("Value %d was never inserted, list_contains says: %s\n",
           NEVER_INSERTED, found_never ? "FOUND (BUG)" : "NOT FOUND (correct)");

    int ok = (len >= 0) && found_fresh && !found_never;
    printf(ok ? "PASS: all correctness checks succeeded.\n"
              : "FAIL: at least one correctness check failed.\n");

    list_destroy();
    pthread_mutex_destroy(&list_lock);
    return ok ? 0 : 1;
}
