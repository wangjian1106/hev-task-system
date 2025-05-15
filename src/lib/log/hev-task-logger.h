/*
 ============================================================================
 Name        : hev-task-logger.h
 Author      : w.jian <i-jianwj@outlook.com>
 Copyright   : Copyright (c) 2025 - 2035 hev
 Description : Logger
 ============================================================================
 */

#ifndef __HEV_TASK_LOGGER_H__
#define __HEV_TASK_LOGGER_H__

#define HEV_TASK_LOG_D(fmt...) hev_task_logger_log (HEV_TASK_LOGGER_DEBUG, fmt)
#define HEV_TASK_LOG_I(fmt...) hev_task_logger_log (HEV_TASK_LOGGER_INFO, fmt)
#define HEV_TASK_LOG_W(fmt...) hev_task_logger_log (HEV_TASK_LOGGER_WARN, fmt)
#define HEV_TASK_LOG_E(fmt...) hev_task_logger_log (HEV_TASK_LOGGER_ERROR, fmt)

#define HEV_TASK_LOG_ON() hev_task_logger_enabled (HEV_TASK_LOGGER_UNSET)
#define HEV_TASK_LOG_ON_D() hev_task_logger_enabled (HEV_TASK_LOGGER_DEBUG)
#define HEV_TASK_LOG_ON_I() hev_task_logger_enabled (HEV_TASK_LOGGER_INFO)
#define HEV_TASK_LOG_ON_W() hev_task_logger_enabled (HEV_TASK_LOGGER_WARN)
#define HEV_TASK_LOG_ON_E() hev_task_logger_enabled (HEV_TASK_LOGGER_ERROR)

typedef enum
{
    HEV_TASK_LOGGER_DEBUG,
    HEV_TASK_LOGGER_INFO,
    HEV_TASK_LOGGER_WARN,
    HEV_TASK_LOGGER_ERROR,
    HEV_TASK_LOGGER_UNSET,
} HevTaskLoggerLevel;

int hev_task_logger_init (HevTaskLoggerLevel level, const char *path);
void hev_task_logger_fini (void);

int hev_task_logger_enabled (HevTaskLoggerLevel level);
void hev_task_logger_log (HevTaskLoggerLevel level, const char *fmt, ...);

#endif /* __HEV_TASK_LOGGER_H__ */
