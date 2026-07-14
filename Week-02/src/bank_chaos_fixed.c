/*
 * bank_chaos_fixed.c
 *
 * Week 2, Problem 1b — Fix 04_bank_chaos.c from Week 1.
 *
 * Same setup: a shared account starts at $10000; two customer threads
 * each attempt many small withdrawals, guarded by a check-then-act
 * balance check. Week 1's version could let the balance go negative
 * (or, on this sandbox's single core, produced a "clean" balance of 0
 * for the wrong reason — see week-1 report) because the check and the
 * act were two separate, unprotected steps.
 *
 * THE FIX
 * -------
 * A single pthread_mutex_t (`balance_lock`) wraps the entire
 * check-then-act sequence — the read of `balance`, the comparison,
 * and the subtraction are now one atomic-with-respect-to-other-threads
 * unit. No other thread can observe or modify `balance` while one
 * thread is deciding whether a withdrawal is allowed and then making
 * it. This directly removes the classic "TOCTOU" (time-of-check to
 * time-of-use) race: the check and the use can no longer be split by
 * a context switch.
 *
 * Invariant we can now guarantee: balance never goes negative, and
 * successful_alice + successful_bob * WITHDRAWAL_AMOUNT will always
 * exactly account for (starting_balance - final_balance).
 *
 * Compile:
 *   gcc -Wall -pthread bank_chaos_fixed.c -o bank_chaos_fixed
 *
 * Run:
 *   ./bank_chaos_fixed
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

int balance = 10000;
pthread_mutex_t balance_lock = PTHREAD_MUTEX_INITIALIZER;

#define ATTEMPTS_PER_CUSTOMER 100000
#define WITHDRAWAL_AMOUNT     1

typedef struct {
    const char *name;
    int successful;
    int rejected;
} customer_result_t;

void *customer(void *arg) {
    customer_result_t *r = (customer_result_t *)arg;
    r->successful = 0;
    r->rejected   = 0;

    for (int i = 0; i < ATTEMPTS_PER_CUSTOMER; i++) {
        pthread_mutex_lock(&balance_lock);

        /*
         * The whole check-then-act now happens while holding the
         * lock, so no other thread can slip a withdrawal in between
         * the check and the subtraction.
         */
        if (balance >= WITHDRAWAL_AMOUNT) {
            balance -= WITHDRAWAL_AMOUNT;
            r->successful++;
        } else {
            r->rejected++;
        }

        pthread_mutex_unlock(&balance_lock);
    }

    return NULL;
}

int main(void) {
    pthread_t alice_t, bob_t;
    customer_result_t alice = { "Alice", 0, 0 };
    customer_result_t bob   = { "Bob",   0, 0 };

    int starting_balance = balance;

    pthread_create(&alice_t, NULL, customer, &alice);
    pthread_create(&bob_t,   NULL, customer, &bob);

    pthread_join(alice_t, NULL);
    pthread_join(bob_t,   NULL);

    printf("Customer %s: %d successful, %d rejected\n",
           alice.name, alice.successful, alice.rejected);
    printf("Customer %s: %d successful, %d rejected\n",
           bob.name, bob.successful, bob.rejected);
    printf("Final balance: %d\n", balance);

    int total_successful = alice.successful + bob.successful;
    int expected_balance = starting_balance
                          - total_successful * WITHDRAWAL_AMOUNT;

    printf("Starting balance:               %d\n", starting_balance);
    printf("Total successful withdrawals:   %d\n", total_successful);
    printf("Expected balance from that:     %d\n", expected_balance);

    if (balance < 0) {
        printf("Result: WRONG — balance went negative, the mutex failed to help.\n");
    } else if (balance != expected_balance) {
        printf("Result: WRONG — balance doesn't match the accounting.\n");
    } else {
        printf("Result: CORRECT — balance matches the accounting exactly, "
               "never negative.\n");
    }

    pthread_mutex_destroy(&balance_lock);
    return 0;
}
