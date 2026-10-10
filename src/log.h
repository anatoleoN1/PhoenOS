#ifndef PHOENOS_LOG_H
#define PHOENOS_LOG_H

/* Memes valeurs que enum dlog_level (dlog.h) */
enum log_level {
    LOG_TRACE = 0,
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR
};

/*
 * Journal du noyau : toujours enregistre dans le journal de debogage (dlog.h, etiquette "kernel") ;
 * affiche aussi a l'ecran et sur le port serie pour INFO et plus : [secondes.ms NIVEAU] message
 */
void klog(enum log_level level, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

#endif
