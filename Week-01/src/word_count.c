/*
 * word_count.c
 *
 * Week 1, Problem 3 — Parallel word count (deliberately racy version).
 *
 * Counts the total number of words in a text file using multiple
 * threads. Per the problem statement, this version does NOT guard the
 * shared total — every thread adds its partial count directly into a
 * shared `total` variable, the same way 03_parallel_sum.c does. The
 * goal is to watch the race appear in a real, useful task rather than
 * in a toy counter.
 *
 * WORD DEFINITION
 * ----------------
 * A word is a maximal run of non-whitespace characters (same
 * definition wc -w uses). Whitespace = space, tab, newline, carriage
 * return, form feed, vertical tab (anything isspace() considers
 * whitespace).
 *
 * HOW THE FILE IS SPLIT
 * ----------------------
 * The file is read entirely into memory, then split into num_threads
 * roughly-equal byte ranges. Splitting a text file at arbitrary byte
 * offsets risks cutting a word in half at a chunk boundary (e.g.
 * "jump" | "s over"), which would either double count it or, more
 * commonly with this simple boundary logic, miscount by one at each
 * internal boundary. That boundary effect is a *separate, deterministic*
 * source of small discrepancies from the *nondeterministic* race on
 * `total` — both are visible in the output of this program, and the
 * report distinguishes between them.
 *
 * Each thread counts words strictly within [start, end) of the shared
 * buffer (read-only access — no race there, only on `total`).
 *
 * Usage:
 *   ./word_count <filename> <num_threads>
 *
 * Compile:
 *   gcc -Wall -Wextra -pthread word_count.c -o word_count
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>

char *buffer;          /* whole file contents, shared (read-only) */
long  buffer_len;
long  total = 0;       /* SHARED accumulator — deliberately racy */

typedef struct {
    long start;         /* inclusive */
    long end;            /* exclusive */
} chunk_t;

/*
 * Count words strictly inside [start, end) of the shared buffer.
 * A word that starts before `start` but spills into this range is NOT
 * counted here (it belongs to the previous chunk); a word that starts
 * inside this range but spills past `end` IS counted here, in whole,
 * by just continuing past `end` until whitespace or the true end of
 * the buffer. This avoids splitting a word into two counted halves in
 * either chunk, though a word can still occasionally be attributed to
 * the "wrong" chunk relative to wc's own scan — the point is not to
 * be bit-for-bit identical to wc, but to see the race.
 */
long count_words_in_range(long start, long end) {
    long count = 0;
    long i = start;

    /* If we start mid-word (previous char was non-whitespace), skip
     * forward to the next whitespace: that word belongs to the
     * previous chunk. */
    if (i > 0 && i < buffer_len && !isspace((unsigned char)buffer[i - 1])) {
        while (i < buffer_len && !isspace((unsigned char)buffer[i])) {
            i++;
        }
    }

    int in_word = 0;
    while (i < buffer_len && (i < end || in_word)) {
        if (isspace((unsigned char)buffer[i])) {
            in_word = 0;
        } else {
            if (!in_word) {
                count++;
                in_word = 1;
            }
        }
        i++;
    }

    return count;
}

void *worker(void *arg) {
    chunk_t *c = (chunk_t *)arg;
    long partial = count_words_in_range(c->start, c->end);
    total += partial;      /* race condition lives here, on purpose */
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <filename> <num_threads>\n", argv[0]);
        return 1;
    }

    const char *filename = argv[1];
    int num_threads = atoi(argv[2]);
    if (num_threads < 1) {
        fprintf(stderr, "num_threads must be >= 1\n");
        return 1;
    }

    FILE *f = fopen(filename, "rb");
    if (!f) {
        perror("fopen");
        return 1;
    }

    fseek(f, 0, SEEK_END);
    buffer_len = ftell(f);
    fseek(f, 0, SEEK_SET);

    buffer = malloc(buffer_len);
    if (!buffer) {
        perror("malloc");
        fclose(f);
        return 1;
    }

    if (fread(buffer, 1, buffer_len, f) != (size_t)buffer_len) {
        fprintf(stderr, "Failed to read entire file\n");
        fclose(f);
        free(buffer);
        return 1;
    }
    fclose(f);

    pthread_t *threads = malloc(num_threads * sizeof(pthread_t));
    chunk_t   *chunks  = malloc(num_threads * sizeof(chunk_t));
    long chunk_size = buffer_len / num_threads;

    for (int i = 0; i < num_threads; i++) {
        chunks[i].start = i * chunk_size;
        chunks[i].end   = (i == num_threads - 1) ? buffer_len
                                                  : (i + 1) * chunk_size;
        pthread_create(&threads[i], NULL, worker, &chunks[i]);
    }

    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("File:         %s\n", filename);
    printf("Threads:      %d\n", num_threads);
    printf("Total words:  %ld\n", total);

    free(buffer);
    free(threads);
    free(chunks);
    return 0;
}
