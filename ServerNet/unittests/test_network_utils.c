#include <stdio.h>
#include <string.h>
#include "../network_utils.h"
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

static int total_checks  = 0;
static int passed_checks = 0;
static int total_tests   = 0;
static int failed_tests  = 0;

#define SOFT_ASSERT(condition)                                          \
    do {                                                                \
        total_checks++;                                                 \
        if (condition) {                                                \
            passed_checks++;                                            \
            fprintf(stdout, "    ok   %s\n", #condition);               \
        } else {                                                        \
            fprintf(stderr, "    FAIL %s (%s:%d)\n",                    \
                    #condition, __FILE__, __LINE__);                    \
        }                                                               \
    } while (0)

#define RUN_TEST(fn)                                                    \
    do {                                                                \
        int before = total_checks - passed_checks;                      \
        printf("\n== %s ==\n", #fn);                                    \
        fn();                                                           \
        int after = total_checks - passed_checks;                       \
        total_tests++;                                                  \
        if (after > before) {                                           \
            failed_tests++;                                             \
            printf("-- %s: FAILED (%d new failure(s))\n",               \
                   #fn, after - before);                                \
        } else {                                                        \
            printf("-- %s: PASSED\n", #fn);                             \
        }                                                               \
    } while (0)

/* ------------------------------------------------------------------ */

static void test_basic_conversion(void)
{
    char buf[INET_ADDRSTRLEN];

    SOFT_ASSERT(strcmp(network_convert_ip_n_to_p(0xC0A80101, buf), "192.168.1.1")     == 0);
    SOFT_ASSERT(strcmp(network_convert_ip_n_to_p(0x00000000, buf), "0.0.0.0")         == 0);
    SOFT_ASSERT(strcmp(network_convert_ip_n_to_p(0xFFFFFFFF, buf), "255.255.255.255") == 0);
}

static void test_assert_on_null_buffer(void)
{
    pid_t pid = fork();
    if (pid == 0) {
        /* child: silence assert message so test output stays clean */
        fclose(stderr);
        network_convert_ip_n_to_p(0x7F000001, NULL);   // should abort
        _exit(0);                                       // if it returns, test fails
    }
    int status = 0;
    waitpid(pid, &status, 0);
    SOFT_ASSERT(WIFSIGNALED(status));                   // killed by a signal
    SOFT_ASSERT(WTERMSIG(status) == SIGABRT);           // specifically SIGABRT
}

static void test_convert_ip_p_to_n(void)
{
    SOFT_ASSERT(network_convert_ip_p_to_n("192.168.1.1") == 0xC0A80101);
    SOFT_ASSERT(network_convert_ip_p_to_n("0.0.0.0") == 0x00000000);
    SOFT_ASSERT(network_convert_ip_p_to_n("255.255.255.255") == 0xFFFFFFFF);
}

/* ------------------------------------------------------------------ */

int main(void)
{
    RUN_TEST(test_basic_conversion);
    RUN_TEST(test_assert_on_null_buffer);
    RUN_TEST(test_convert_ip_p_to_n);
    
    printf("\n========================\n");
    printf("Tests : %d passed / %d total\n",
           total_tests - failed_tests, total_tests);
    printf("Checks: %d passed / %d total\n",
           passed_checks, total_checks);

    return failed_tests == 0 ? 0 : 1;
}
