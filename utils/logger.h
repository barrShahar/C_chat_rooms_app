#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>
#include <time.h>

#define LOG_LEVEL_DEBUG 0
#define LOG_LEVEL_INFO  1
#define LOG_LEVEL_WARN  2
#define LOG_LEVEL_ERROR 3
#define LOG_LEVEL_NONE  4

#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_DEBUG
#endif

#define LOG_WRITE(level_str, fmt, ...) \
    do { \
        time_t _t = time(NULL); \
        char _buf[20]; \
        strftime(_buf, sizeof(_buf), "%Y-%m-%d %H:%M:%S", localtime(&_t)); \
        fprintf(stderr, "[%s] [%-5s] %s:%d (%s) - " fmt "\n", \
                _buf, level_str, __FILE__, __LINE__, __func__, ##__VA_ARGS__); \
    } while (0)

#if LOG_LEVEL <= LOG_LEVEL_DEBUG
#define LOG_DEBUG(fmt, ...) LOG_WRITE("DEBUG", fmt, ##__VA_ARGS__)
#else
#define LOG_DEBUG(fmt, ...) do {} while (0)
#endif

#if LOG_LEVEL <= LOG_LEVEL_INFO
#define LOG_INFO(fmt, ...)  LOG_WRITE("INFO", fmt, ##__VA_ARGS__)
#else
#define LOG_INFO(fmt, ...)  do {} while (0)
#endif

#if LOG_LEVEL <= LOG_LEVEL_WARN
#define LOG_WARN(fmt, ...)  LOG_WRITE("WARN", fmt, ##__VA_ARGS__)
#else
#define LOG_WARN(fmt, ...)  do {} while (0)
#endif

#if LOG_LEVEL <= LOG_LEVEL_ERROR
#define LOG_ERROR(fmt, ...) LOG_WRITE("ERROR", fmt, ##__VA_ARGS__)
#else
#define LOG_ERROR(fmt, ...) do {} while (0)
#endif

#endif /* LOGGER_H */
