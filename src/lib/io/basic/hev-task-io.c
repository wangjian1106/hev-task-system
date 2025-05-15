/*
 ============================================================================
 Name        : hev-task-io.c
 Author      : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2018 everyone.
 Description : Task I/O operations
 ============================================================================
 */

#if !defined(__linux__) && defined(ENABLE_IO_SPLICE_SYSCALL)
#undef ENABLE_IO_SPLICE_SYSCALL
#endif /* !defined(__linux__) && ENABLE_IO_SPLICE_SYSCALL */

#ifdef ENABLE_IO_SPLICE_SYSCALL
#define _GNU_SOURCE
#endif /* ENABLE_IO_SPLICE_SYSCALL */

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include "kern/task/hev-task.h"
#include "lib/io/buffer/hev-circular-buffer.h"
#include "lib/misc/hev-compiler.h"

#include "hev-task-io.h"
#include "lib/log/hev-task-logger.h"

#include <stdio.h>
#include <time.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

// 互斥锁保证线程安全
static pthread_mutex_t bandwidth_mutex = PTHREAD_MUTEX_INITIALIZER;

// 全局静态带宽监测器
static struct {
    // 上传相关状态
    uint64_t total_upload;
    uint64_t last_total_upload;
    time_t last_upload_time;
    double current_upload_rate;
    char formatted_upload[32];

    // 下载相关状态
    uint64_t total_download;
    uint64_t last_total_download;
    time_t last_download_time;
    double current_download_rate;
    char formatted_download[32];

    // 线程控制
    volatile int running;
} bandwidth_monitor;

// 监控线程函数
void* monitor_thread(void* arg) {
    (void)arg; // 避免未使用参数警告

    while(1) {
        pthread_mutex_lock(&bandwidth_mutex);
        const int running = bandwidth_monitor.running;
        pthread_mutex_unlock(&bandwidth_mutex);

        if (running) {
            // 获取当前速率
            const char* upload = hev_task_io_bandwidth_get_formatted_upload();
            const char* download = hev_task_io_bandwidth_get_formatted_download();

            // 打印速率信息
            const time_t now = time(NULL);
            HEV_TASK_LOG_I("[%ld] Upload: %s, Download: %s\n", now, upload, download);
        }

        // 每秒一次
        sleep(1);
    }

    return NULL;
}

// 初始化带宽监测器
void hev_task_io_bandwidth_init() {
    HEV_TASK_LOG_I ("init bandwidth monitor");

    pthread_mutex_lock(&bandwidth_mutex);
    // 上传初始化
    bandwidth_monitor.total_upload = 0;
    bandwidth_monitor.last_upload_time = time(NULL);
    bandwidth_monitor.current_upload_rate = 0.0;
    strcpy(bandwidth_monitor.formatted_upload, "0.00 B/s");

    // 下载初始化
    bandwidth_monitor.total_download = 0;
    bandwidth_monitor.last_download_time = time(NULL);
    bandwidth_monitor.current_download_rate = 0.0;
    strcpy(bandwidth_monitor.formatted_download, "0.00 B/s");

    // pthread_t monitor_tid;
    // // 创建监控线程
    // if (pthread_create(&monitor_tid, NULL, monitor_thread, NULL) != 0) {
    //     HEV_TASK_LOG_E ("Failed to create monitor thread");
    //     return;
    // }

    bandwidth_monitor.running = 1;

    pthread_mutex_unlock(&bandwidth_mutex);
}

// 停止监控
void hev_task_io_bandwidth_stop() {
    pthread_mutex_lock(&bandwidth_mutex);
    bandwidth_monitor.running = 0;
    pthread_mutex_unlock(&bandwidth_mutex);
}

// 内部：格式化速率显示
static void hev_task_io_format_rate(const double rate, char* buffer, size_t size) {
    const char* unit = "B/s";
    double display = rate;

    if (rate > 1024 * 1024) {
        display = rate / (1024 * 1024);
        unit = "MB/s";
    } else if (rate > 1024) {
        display = rate / 1024;
        unit = "KB/s";
    }

    snprintf(buffer, size, "%.2f %s", round(display * 100)/100, unit);
}

// 更新上传数据（传入新增字节数）
void hev_task_io_bandwidth_add_upload(const uint64_t bytes_added) {
    pthread_mutex_lock(&bandwidth_mutex);

    bandwidth_monitor.total_upload += bytes_added;

    const time_t now = time(NULL);
    const double time_diff = difftime(now, bandwidth_monitor.last_upload_time);

    if (time_diff >= 1.0) { // 至少1秒后计算
        const uint64_t bytes_diff = bandwidth_monitor.total_upload -
                                    bandwidth_monitor.last_total_upload;
        bandwidth_monitor.current_upload_rate =
            (double)bytes_diff / time_diff;

        hev_task_io_format_rate(bandwidth_monitor.current_upload_rate,
                   bandwidth_monitor.formatted_upload,
                   sizeof(bandwidth_monitor.formatted_upload));

        bandwidth_monitor.last_total_upload = bandwidth_monitor.total_upload;
        bandwidth_monitor.last_upload_time = now;
    }

    pthread_mutex_unlock(&bandwidth_mutex);
}

// 更新下载数据（传入新增字节数）
void hev_task_io_bandwidth_add_download(const uint64_t bytes_added) {
    pthread_mutex_lock(&bandwidth_mutex);

    bandwidth_monitor.total_download += bytes_added;

    const time_t now = time(NULL);
    const double time_diff = difftime(now, bandwidth_monitor.last_download_time);

    if (time_diff >= 1.0) { // 至少1秒后计算
        const uint64_t bytes_diff = bandwidth_monitor.total_download -
                                    bandwidth_monitor.last_total_download;
        bandwidth_monitor.current_download_rate =
            (double)bytes_diff / time_diff;

        hev_task_io_format_rate(bandwidth_monitor.current_download_rate,
                   bandwidth_monitor.formatted_download,
                   sizeof(bandwidth_monitor.formatted_download));

        bandwidth_monitor.last_total_download = bandwidth_monitor.total_download;
        bandwidth_monitor.last_download_time = now;
    }

    pthread_mutex_unlock(&bandwidth_mutex);
}

void hev_task_io_bandwidth_add(const uint64_t bytes_count, char* name, char* action)
{
    if (0 == strcmp (name, "client")) {
        if (0 == strcmp (action, "read")) {
            hev_task_io_bandwidth_add_upload (bytes_count);
        } else {
            hev_task_io_bandwidth_add_download (bytes_count);
        }
    }
    else if (0 == strcmp (name, "target")) {
        if (0 == strcmp (action, "read")) {
            hev_task_io_bandwidth_add_download (bytes_count);
        } else {
            hev_task_io_bandwidth_add_upload (bytes_count);
        }
    }
}

// 获取当前上传速率(B/s)
double hev_task_io_bandwidth_get_upload_rate() {
    return bandwidth_monitor.current_upload_rate;
}

// 获取当前下载速率(B/s)
double hev_task_io_bandwidth_get_download_rate() {
    return bandwidth_monitor.current_download_rate;
}

// 获取格式化上传速率
EXPORT_SYMBOL const char* hev_task_io_bandwidth_get_formatted_upload() {
    return bandwidth_monitor.formatted_upload;
}

// 获取格式化下载速率
EXPORT_SYMBOL const char* hev_task_io_bandwidth_get_formatted_download() {
    return bandwidth_monitor.formatted_download;
}

// 获取总上传字节数
uint64_t hev_task_io_bandwidth_get_total_upload() {
    return bandwidth_monitor.total_upload;
}

// 获取总下载字节数
uint64_t hev_task_io_bandwidth_get_total_download() {
    return bandwidth_monitor.total_download;
}

typedef struct _HevTaskIOSplicer HevTaskIOSplicer;

struct _HevTaskIOSplicer
{
#ifdef ENABLE_IO_SPLICE_SYSCALL
    int fd[2];
    size_t wlen;
    size_t blen;
#else
    HevCircularBuffer *buf;
#endif /* !ENABLE_IO_SPLICE_SYSCALL */
    char* name;
};

EXPORT_SYMBOL int
hev_task_io_open (const char *pathname, int flags, ...)
{
    flags |= O_NONBLOCK;

    if (O_CREAT & flags) {
        int fd;
        va_list ap;

        va_start (ap, flags);
        fd = open (pathname, flags, va_arg (ap, int));
        va_end (ap);

        return fd;
    }

    return open (pathname, flags);
}

EXPORT_SYMBOL int
hev_task_io_creat (const char *pathname, mode_t mode)
{
    return hev_task_io_open (pathname, O_CREAT | O_WRONLY | O_TRUNC, mode);
}

EXPORT_SYMBOL int
hev_task_io_openat (int dirfd, const char *pathname, int flags, ...)
{
    flags |= O_NONBLOCK;

    if (O_CREAT & flags) {
        int fd;
        va_list ap;

        va_start (ap, flags);
        fd = openat (dirfd, pathname, flags, va_arg (ap, int));
        va_end (ap);

        return fd;
    }

    return openat (dirfd, pathname, flags);
}

EXPORT_SYMBOL int
hev_task_io_dup (int oldfd)
{
    int newfd;
    int nonblock = 1;

    newfd = dup (oldfd);
    if (0 > newfd)
        return -1;

    if (0 > ioctl (newfd, FIONBIO, (char *)&nonblock)) {
        close (newfd);
        return -2;
    }

    return newfd;
}

EXPORT_SYMBOL int
hev_task_io_dup2 (int oldfd, int newfd)
{
    int nonblock = 1;

    newfd = dup2 (oldfd, newfd);
    if (0 > newfd)
        return -1;

    if (0 > ioctl (newfd, FIONBIO, (char *)&nonblock)) {
        close (newfd);
        return -2;
    }

    return newfd;
}

EXPORT_SYMBOL ssize_t
hev_task_io_read (int fd, void *buf, size_t count, HevTaskIOYielder yielder,
                  void *yielder_data)
{
    ssize_t s;

retry:
    s = read (fd, buf, count);
    if (s < 0 && errno == EAGAIN) {
        if (yielder) {
            if (yielder (HEV_TASK_WAITIO, yielder_data))
                return -2;
        } else {
            hev_task_yield (HEV_TASK_WAITIO);
        }
        goto retry;
    }

    return s;
}

EXPORT_SYMBOL ssize_t
hev_task_io_readv (int fd, const struct iovec *iov, int iovcnt,
                   HevTaskIOYielder yielder, void *yielder_data)
{
    ssize_t s;

retry:
    s = readv (fd, iov, iovcnt);
    if (s < 0 && errno == EAGAIN) {
        if (yielder) {
            if (yielder (HEV_TASK_WAITIO, yielder_data))
                return -2;
        } else {
            hev_task_yield (HEV_TASK_WAITIO);
        }
        goto retry;
    }

    return s;
}

EXPORT_SYMBOL ssize_t
hev_task_io_write (int fd, const void *buf, size_t count,
                   HevTaskIOYielder yielder, void *yielder_data)
{
    ssize_t s;

retry:
    s = write (fd, buf, count);
    if (s < 0 && errno == EAGAIN) {
        if (yielder) {
            if (yielder (HEV_TASK_WAITIO, yielder_data))
                return -2;
        } else {
            hev_task_yield (HEV_TASK_WAITIO);
        }
        goto retry;
    }

    return s;
}

EXPORT_SYMBOL ssize_t
hev_task_io_writev (int fd, const struct iovec *iov, int iovcnt,
                    HevTaskIOYielder yielder, void *yielder_data)
{
    ssize_t s;

retry:
    s = writev (fd, iov, iovcnt);
    if (s < 0 && errno == EAGAIN) {
        if (yielder) {
            if (yielder (HEV_TASK_WAITIO, yielder_data))
                return -2;
        } else {
            hev_task_yield (HEV_TASK_WAITIO);
        }
        goto retry;
    }

    return s;
}

#ifdef ENABLE_IO_SPLICE_SYSCALL

static int
task_io_splicer_init (HevTaskIOSplicer *self, size_t buf_size, char* name)
{
    HevTask *task = hev_task_self ();
    int res;

    res = pipe2 (self->fd, O_NONBLOCK);
    if (res < 0)
        goto exit;

    res = hev_task_add_fd (task, self->fd[0], POLLIN);
    if (res < 0)
        goto exit_close;

    res = hev_task_add_fd (task, self->fd[1], POLLOUT);
    if (res < 0)
        goto exit_close;

#ifdef F_GETPIPE_SZ
    buf_size = fcntl (self->fd[0], F_GETPIPE_SZ);
#endif

    self->name = name;
    self->wlen = 0;
    self->blen = buf_size;

    return 0;

exit_close:
    close (self->fd[0]);
    close (self->fd[1]);
exit:
    return res;
}

static void
task_io_splicer_fini (HevTaskIOSplicer *self)
{
    close (self->fd[0]);
    close (self->fd[1]);
}

static int
task_io_splice (HevTaskIOSplicer *self, int fd_in, int fd_out)
{
    int res;
    ssize_t s;

    s = splice (fd_in, NULL, self->fd[1], NULL, self->blen,
                SPLICE_F_MOVE | SPLICE_F_NONBLOCK);
    if (0 >= s) {
        if ((0 > s) && (EAGAIN == errno))
            res = 0;
        else
            res = -1;
    } else {
        res = 1;
        self->wlen += s;
    }

    if (self->wlen) {
        s = splice (self->fd[0], NULL, fd_out, NULL, self->blen,
                    SPLICE_F_MOVE | SPLICE_F_NONBLOCK);
        if (0 >= s) {
            if ((0 > s) && (EAGAIN == errno))
                res = 0;
            else
                res = -1;
        } else {
            res = 1;
            self->wlen -= s;
        }
    }

    return res;
}

#else

static int
task_io_splicer_init (HevTaskIOSplicer *self, size_t buf_size, char* name)
{
    self->name = name;
    self->buf = hev_circular_buffer_new (buf_size);
    if (!self->buf)
        return -1;

    return 0;
}

static void
task_io_splicer_fini (HevTaskIOSplicer *self)
{
    if (self->buf)
        hev_circular_buffer_unref (self->buf);
}

static int
task_io_splice (HevTaskIOSplicer *self, int fd_in, int fd_out)
{
    struct iovec iov[2];
    int res = 1, iovc;

    iovc = hev_circular_buffer_writing (self->buf, iov);
    //HEV_TASK_LOG_I ("task_io_splice hev_circular_buffer_writing %d", iovc);
    if (iovc) {
        ssize_t s = readv (fd_in, iov, iovc);
        if (0 >= s) {
            if ((0 > s) && (EAGAIN == errno))
                res = 0;
            else
                res = -1;
        } else {
            //HEV_TASK_LOG_I ("task_io_splice received %zd bytes\n", s);
            //hev_task_io_bandwidth_add_download (s);
            hev_circular_buffer_write_finish (self->buf, s);
        }
    }

    iovc = hev_circular_buffer_reading (self->buf, iov);
    //HEV_TASK_LOG_I ("task_io_splice hev_circular_buffer_reading %d", iovc);
    if (iovc) {
        ssize_t s = writev (fd_out, iov, iovc);
        if (0 >= s) {
            if ((0 > s) && (EAGAIN == errno))
                res = 0;
            else
                res = -1;
        } else {
            //HEV_TASK_LOG_I ("task_io_splice written %zd bytes to %s\n", s, self->name);
            hev_task_io_bandwidth_add (s, self->name, "write");
            res = 1;
            hev_circular_buffer_read_finish (self->buf, s);
        }
    }

    return res;
}

#endif /* !ENABLE_IO_SPLICE_SYSCALL */

EXPORT_SYMBOL void
hev_task_io_splice (int fd_a_i, int fd_a_o, int fd_b_i, int fd_b_o,
                    size_t buf_size, HevTaskIOYielder yielder,
                    void *yielder_data)
{
    HevTaskIOSplicer splicer_f;
    HevTaskIOSplicer splicer_b;
    int res_f = 1;
    int res_b = 1;

    HEV_TASK_LOG_I ("hev_task_io_splice init");

    if (task_io_splicer_init (&splicer_f, buf_size, "target") < 0)
        return;
    if (task_io_splicer_init (&splicer_b, buf_size, "client") < 0)
        goto exit;

    for (;;) {
        HevTaskYieldType type;

        if (res_f >= 0)
            res_f = task_io_splice (&splicer_f, fd_a_i, fd_b_o);
        if (res_b >= 0)
            res_b = task_io_splice (&splicer_b, fd_b_i, fd_a_o);

        if (res_f > 0 || res_b > 0)
            type = HEV_TASK_YIELD;
        else if ((res_f | res_b) == 0)
            type = HEV_TASK_WAITIO;
        else
            break;

        if (yielder) {
            if (yielder (type, yielder_data))
                break;
        } else {
            hev_task_yield (type);
        }
    }

    task_io_splicer_fini (&splicer_b);
exit:
    task_io_splicer_fini (&splicer_f);
}
