#!/usr/bin/env python3
"""
Generates the 1,000,000-word synthetic test file used for problem-3
observations, since the sandbox network allowlist doesn't include
gutenberg.org. Run this to reproduce sample.txt locally, or swap in a
real Project Gutenberg text and re-run word_count against that instead.
"""
import random

WORDS = ['the', 'quick', 'brown', 'fox', 'jumps', 'over', 'lazy', 'dog',
         'whale', 'sea', 'captain', 'ship']

def main():
    random.seed(42)
    with open('sample.txt', 'w') as f:
        for _ in range(50000):
            f.write(' '.join(random.choice(WORDS) for _ in range(20)) + '\n')

if __name__ == '__main__':
    main()
