# Jamo speed test

Best of 5 runs after a warm-up, in seconds. Made by `python bench/run_bench.py`.
Every program prints the same answer in each language, and the script checks that.

| Program | Jamo | C (gcc -O2) | Python | Jamo vs Python |
|---|---|---|---|---|
| Recursion (fib 35) | 0.044 | 0.012 | 0.872 | 20x faster |
| Loops (100 million) | 0.066 | 0.055 | 5.037 | 76x faster |
| Array math (20 million) | 0.035 | 0.008 | 2.327 | 66x faster |
| Game particles (5,000 x 1,000) | 0.031 | 0.010 | 0.902 | 30x faster |
| Text joining (3 million) | 0.134 | 0.188 | 0.583 | 4x faster |

Python 3.13.16. Times depend on the PC, so compare the columns, not the numbers.
