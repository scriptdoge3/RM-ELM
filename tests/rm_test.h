/* Minimal test helpers: no framework, just counters and messages. */
#ifndef RM_TEST_H
#define RM_TEST_H

#include <math.h>
#include <stdio.h>

static int rm_test_failures = 0;
static int rm_test_checks = 0;

#define CHECK(cond, ...)                                                   \
    do {                                                                   \
        rm_test_checks++;                                                  \
        if (!(cond)) {                                                     \
            rm_test_failures++;                                            \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                    \
            printf(__VA_ARGS__);                                           \
            printf("\n");                                                  \
        }                                                                  \
    } while (0)

#define CHECK_REL(got, want, tol, what)                                    \
    CHECK(fabs((got) - (want)) <= (tol) * fabs(want),                      \
          "%s: got %.10g want %.10g (rel err %.3g > %.3g)", (what),        \
          (double)(got), (double)(want),                                   \
          fabs((got) - (want)) / fabs(want), (double)(tol))

#define CHECK_ABS(got, want, tol, what)                                    \
    CHECK(fabs((got) - (want)) <= (tol),                                   \
          "%s: got %.10g want %.10g (abs err %.3g > %.3g)", (what),        \
          (double)(got), (double)(want), fabs((got) - (want)), (double)(tol))

#define TEST_REPORT()                                                      \
    (printf("%d checks, %d failures\n", rm_test_checks, rm_test_failures), \
     rm_test_failures ? 1 : 0)

#endif
