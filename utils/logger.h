#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define LOG_LEVEL_DEBUG 0
#define LOG_LEVEL_INFO  1
#define LOG_LEVEL_WARN  2
#define LOG_LEVEL_ERROR 3
#define LOG_LEVEL_NONE  4

#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_DEBUG
#endif

#define LOG_COLOR_DEBUG  "\033[36m"          /* cyan */
#define LOG_COLOR_INFO   "\033[32m"          /* green */
#define LOG_COLOR_WARN   "\033[33m"          /* yellow */
#define LOG_COLOR_ERROR  "\033[31m"          /* red */
#define LOG_COLOR_ASSERT "\033[1;5;97;41m"   /* bold + blink + bright white on red bg */
#define LOG_COLOR_RESET  "\033[0m"

#define LOG_WRITE(color, level_str, fmt, ...) \
    do { \
        time_t _t = time(NULL); \
        struct tm _tm; \
        char _buf[20]; \
        int _tty = isatty(STDERR_FILENO) || (getenv("FORCE_COLOR") != NULL); \
        strftime(_buf, sizeof(_buf), "%Y-%m-%d %H:%M:%S", localtime_r(&_t, &_tm)); \
        fprintf(stderr, "[%s] %s[%-5s]%s %s:%d (%s) - " fmt "\n", \
                _buf, _tty ? color : "", level_str, _tty ? LOG_COLOR_RESET : "", \
                __FILE__, __LINE__, __func__, ##__VA_ARGS__); \
    } while (0)

#if LOG_LEVEL <= LOG_LEVEL_DEBUG
#define LOG_DEBUG(fmt, ...) LOG_WRITE(LOG_COLOR_DEBUG, "DEBUG", fmt, ##__VA_ARGS__)
#else
#define LOG_DEBUG(fmt, ...) do {} while (0)
#endif

#if LOG_LEVEL <= LOG_LEVEL_INFO
#define LOG_INFO(fmt, ...)  LOG_WRITE(LOG_COLOR_INFO,  "INFO",  fmt, ##__VA_ARGS__)
#else
#define LOG_INFO(fmt, ...)  do {} while (0)
#endif

#if LOG_LEVEL <= LOG_LEVEL_WARN
#define LOG_WARN(fmt, ...)  LOG_WRITE(LOG_COLOR_WARN,  "WARN",  fmt, ##__VA_ARGS__)
#else
#define LOG_WARN(fmt, ...)  do {} while (0)
#endif

#if LOG_LEVEL <= LOG_LEVEL_ERROR
#define LOG_ERROR(fmt, ...) LOG_WRITE(LOG_COLOR_ERROR, "ERROR", fmt, ##__VA_ARGS__)
#else
#define LOG_ERROR(fmt, ...) do {} while (0)
#endif

/* Soft assert: condition that must NEVER be false (a "should be impossible" bug).
 * On failure, prints a very loud banner and continues execution.
 * Caller is responsible for any error handling / early return.
 */
#define SOFT_ASSERT(cond) \
    do { \
        if (!(cond)) { \
            LOG_WRITE(LOG_COLOR_ASSERT, "BUG!!", \
                      ">>>>> SOFT ASSERT FAILED: ( %s ) <<<<<", #cond); \
        } \
    } while (0)

#endif /* LOGGER_H */
