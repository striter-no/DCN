#include <logger.h>
#include <stdarg.h>

void logger_init(
    struct logger *log, 
    FILE *output
){
    if (!log) return;
    allocator_init(&log->allc);
    array_init(&log->levels, sizeof(char *));
    log->output = output;
    log->last_log = NULL;
    log->logs_num = 0;
    log->is_active = true;
}

void logger_stop(
    struct logger *log
){
    if (!log) return;
    array_free(&log->levels);
    allocator_end(&log->allc);
    free(log->last_log);
    log->output = NULL;
    log->logs_num = 0;
}

void logger_act(
    struct logger *log
){
    if (!log) return;
    log->is_active = true;
}

void logger_deact(
    struct logger *log
){
    if (!log) return;
    log->is_active = false;
}

void dblog(
    struct logger *log,
    LOG_TYPES type,
    char *format,
    ...
){
    if (!log) return;
    if(!log->is_active) return;

    char *date = malloc(9); // %H:%M:%S
    time_t rawtime;
    struct tm *timeinfo;
    
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    strftime(date, 9, "%H:%M:%S", timeinfo);
    
    char *strtype = NULL;

    switch (type){
        case INFO:    strtype = "INFO"; break;
        case WARNING: strtype = "WARN"; break;
        case ERROR:   strtype = "ERROR"; break;
        case FATAL:   strtype = "FATAL"; break;
        default:      strtype = "UNKN"; break;
    }

    char *lvl = NULL;
    if (0 == array_size(&log->levels)){
        lvl = malloc(5);
        strcpy(lvl, "root");
    } else {
        char **ptr = NULL;
        array_at(&log->levels, (void**)&ptr, array_size(&log->levels) - 1);
        if (!ptr || !(*ptr)){
            lvl = malloc(6);
            strcpy(lvl, "undef");
        } else {
            lvl = malloc(strlen(*ptr) + 1);
            strcpy(lvl, *ptr);
        }
    }

    va_list args;
    va_start(args, format);
    char *message = NULL;
    if (vasprintf(&message, format, args) < 0) {
        va_end(args);
        free(date);
        free(lvl);
        return;
    }
    va_end(args);

    if (log->last_log && strcmp(log->last_log, message) == 0){
        if (log->logs_num <= 99)
            fprintf(
                log->output,
                "\r(%zu)[T%zu][%s][%s][%s] %s",
                log->logs_num + 1,
                syscall(SYS_gettid),
                date,
                strtype,
                lvl,
                message
            );
        else
            fprintf(
                log->output,
                "\r(99+)[T%zu][%s][%s][%s] %s",
                syscall(SYS_gettid),
                date,
                strtype,
                lvl,
                message
            );
        log->logs_num++;
    } else {
        if (log->logs_num != 0){
            fprintf(log->output, "\n");
            log->logs_num = 0;
        }
        fprintf(
            log->output,
            "[T%zu][%s][%s][%s] %s\n",
            syscall(SYS_gettid),
            date,
            strtype,
            lvl,
            message
        );
        log->last_log = realloc(log->last_log, strlen(message) + 1);
        strcpy(log->last_log, message);
    }
    

    free(message);
    free(date);
    free(lvl);
}

void dblevel_push(
    struct logger *log,
    char *level_name // valid string
){
    if (!log) return;
    if(!log->is_active) return;
    array_append(&log->levels, &level_name);
    dblog(log, INFO, "new lvl");
}

void dblevel_pop(
    struct logger *log
){
    if (!log) return;
    if(!log->is_active) return;
    if (array_del(&log->levels, array_size(&log->levels) - 1)){
        dblog(log, INFO, "pop lvl");
    }
}
