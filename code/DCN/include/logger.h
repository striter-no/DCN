#pragma once
#define _GNU_SOURCE
#include <allocator.h>
#include <array.h>
#include <threads.h>
#include <unistd.h>
#include <unistdio.h>
#include <sys/syscall.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <stdio.h>

struct logger {
    struct allocator allc; // internal allocator    
    struct array levels; // just char*
    size_t logs_num;

    FILE *output;
    char *last_log;
};

typedef enum {
    INFO,
    WARNING,
    ERROR,
    FATAL
} LOG_TYPES;

void logger_init(
    struct logger *log, 
    FILE *output
);

void logger_stop(
    struct logger *log
);

void dblog(
    struct logger *log,
    LOG_TYPES type,
    char *format,
    ...
);

void dblevel_push(
    struct logger *log,
    char *level_name // valid string
);

void dblevel_pop(
    struct logger *log
);
