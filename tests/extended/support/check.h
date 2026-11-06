/* Minimal test framework for the extended regression suite.
 *
 * Each test runs in its own forked child process, so a crash or a failing
 * production assert() in one test is reported as a failure of that test only.
 *
 * Test kinds:
 *   TEST(name)                 must pass.
 *   KNOWN_DEFECT(name, why)    documents a defect present in the current code.
 *                              The test asserts the *correct* behaviour, so it is
 *                              expected to fail today and is reported as XFAIL.
 *                              If it starts passing it is reported as XPASS and
 *                              the suite fails, so whoever fixed the defect turns it
 *                              into a normal TEST deliberately.
 *
 * Usage in a test file:
 *   TEST(foo) { CHECK(1 + 1 == 2); }
 *   KNOWN_DEFECT(bar, "short explanation") { CHECK(buggy() == 42); }
 *   int main(int argc, char **argv) { return run_tests(argc, argv); }
 *
 * Optional argument: a substring; only tests whose name contains it are run.
 */
#ifndef TEST_CHECK_H
#define TEST_CHECK_H

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

typedef void (*TestFn)(void);

struct TestCase {
    const char *name;
    TestFn fn;
    const char *known_defect; /* NULL for ordinary tests */
};

#define TEST_MAX_CASES 256
static struct TestCase test_cases[TEST_MAX_CASES];
static int test_case_count;

static void test_register(const char *name, TestFn fn, const char *defect) {
    if (test_case_count == TEST_MAX_CASES) {
        fprintf(stderr, "too many tests\n");
        exit(2);
    }
    test_cases[test_case_count++] = (struct TestCase){ name, fn, defect };
}

#define TEST_DEFINE_(name, defect)                                         \
    static void test_##name(void);                                         \
    __attribute__((constructor)) static void test_register_##name(void) {  \
        test_register(#name, test_##name, defect);                         \
    }                                                                      \
    static void test_##name(void)

#define TEST(name) TEST_DEFINE_(name, NULL)
#define KNOWN_DEFECT(name, why) TEST_DEFINE_(name, why)

/* Failure reporting. Messages go to stderr; the child exits with status 1. */
#define FAIL_(...)                                                           \
    do {                                                                     \
        fprintf(stderr, "    %s:%d: ", __FILE__, __LINE__);                  \
        fprintf(stderr, __VA_ARGS__);                                        \
        fputc('\n', stderr);                                                 \
        exit(1);                                                             \
    } while (0)

#define CHECK(cond)                                                          \
    do { if (!(cond)) FAIL_("CHECK(%s) failed", #cond); } while (0)

#define CHECK_MSG(cond, ...)                                                 \
    do { if (!(cond)) FAIL_(__VA_ARGS__); } while (0)

#define CHECK_EQ(a, b)                                                       \
    do {                                                                     \
        long long a_ = (long long)(a), b_ = (long long)(b);                  \
        if (a_ != b_) FAIL_("%s == %s failed: %lld != %lld", #a, #b, a_, b_);\
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                \
    do {                                                                     \
        double a_ = (double)(a), b_ = (double)(b), t_ = (double)(tol);       \
        if (!(fabs(a_ - b_) <= t_))                                          \
            FAIL_("%s ~= %s failed: %.9g vs %.9g (tol %.3g)", #a, #b, a_, b_, t_); \
    } while (0)

#define CHECK_IVEC3_EQ(a, b)                                                 \
    do {                                                                     \
        ivec3s a_ = (a), b_ = (b);                                           \
        if (a_.x != b_.x || a_.y != b_.y || a_.z != b_.z)                    \
            FAIL_("%s == %s failed: (%d,%d,%d) != (%d,%d,%d)", #a, #b,        \
                  a_.x, a_.y, a_.z, b_.x, b_.y, b_.z);                       \
    } while (0)

/* Deterministic PRNG for property-style tests (xorshift64*). */
static unsigned long long test_rng_state = 0x9E3779B97F4A7C15ull;
static inline void test_rng_seed(unsigned long long s) { test_rng_state = s ? s : 1; }
static inline unsigned long long test_rng_u64(void) {
    test_rng_state ^= test_rng_state >> 12;
    test_rng_state ^= test_rng_state << 25;
    test_rng_state ^= test_rng_state >> 27;
    return test_rng_state * 0x2545F4914F6CDD1Dull;
}
/* uniform integer in [lo, hi] */
static inline long long test_rng_range(long long lo, long long hi) {
    return lo + (long long)(test_rng_u64() % (unsigned long long)(hi - lo + 1));
}

static int run_tests(int argc, char **argv) {
    const char *filter = argc > 1 ? argv[1] : NULL;
    int passed = 0, failed = 0, xfailed = 0, xpassed = 0;

    for (int i = 0; i < test_case_count; i++) {
        struct TestCase *t = &test_cases[i];
        if (filter && !strstr(t->name, filter)) continue;

        fflush(stdout);
        fflush(stderr);
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); return 2; }
        if (pid == 0) {
            t->fn();
            fflush(stdout);
            exit(0); /* not _exit: lets coverage/sanitizer runtimes flush */
        }
        int status = 0;
        waitpid(pid, &status, 0);
        bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
        const char *how = WIFSIGNALED(status) ? " (crashed)" : "";

        if (!t->known_defect) {
            if (ok) { passed++; printf("  PASS   %s\n", t->name); }
            else    { failed++; printf("  FAIL   %s%s\n", t->name, how); }
        } else {
            if (ok) {
                xpassed++;
                printf("  XPASS  %s  <-- known defect no longer reproduces; "
                       "convert KNOWN_DEFECT to TEST\n         (%s)\n",
                       t->name, t->known_defect);
            } else {
                xfailed++;
                printf("  XFAIL  %s%s\n         known defect: %s\n", t->name, how, t->known_defect);
            }
        }
    }

    printf("  -- %d passed, %d failed, %d known defects (xfail), %d unexpectedly passing (xpass)\n",
           passed, failed, xfailed, xpassed);
    return (failed || xpassed) ? 1 : 0;
}

#endif
