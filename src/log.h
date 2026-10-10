#ifndef PHOENOS_LOG_H
#define PHOENOS_LOG_H

enum log_level {
    LOG_DEBUG = 0,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR
};

/* Journal horodaté : [secondes.ms NIVEAU] message  (écran + port série) */
void klog(enum log_level level, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

#endif
