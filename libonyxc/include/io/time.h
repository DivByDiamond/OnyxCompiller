/*
 * time.h — date/time functions (libonyxc v0.5).
 *
 * Provides time_t, struct tm, clock(), time(), difftime(), localtime(),
 * gmtime(), asctime(), ctime(), strftime(). Local time is treated as UTC
 * (no timezone database), DST flag is always 0.
 */
#ifndef _ONYX_TIME_H
#define _ONYX_TIME_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef long time_t;
typedef long clock_t;
typedef long suseconds_t;

#define CLOCKS_PER_SEC  1000

#ifndef _ONYX_TIMESPEC_DEFINED
#define _ONYX_TIMESPEC_DEFINED
struct timespec {
    long tv_sec;
    long tv_nsec;
};
#endif

#ifndef _ONYX_TIMEVAL_DEFINED
#define _ONYX_TIMEVAL_DEFINED
struct timeval {
    long tv_sec;
    long tv_usec;
};
#endif

struct tm {
    int tm_sec;     /* seconds [0..60] (60 for leap second) */
    int tm_min;     /* minutes [0..59] */
    int tm_hour;    /* hours [0..23] */
    int tm_mday;    /* day of month [1..31] */
    int tm_mon;     /* month [0..11] */
    int tm_year;    /* year - 1900 */
    int tm_wday;    /* day of week [0..6, 0=Sunday] */
    int tm_yday;    /* day of year [0..365] */
    int tm_isdst;   /* daylight saving flag, -1 unknown */
};

#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1

time_t time(time_t *t);
clock_t clock(void);
double difftime(time_t end, time_t start);
struct tm *gmtime(const time_t *t);
struct tm *localtime(const time_t *t);
time_t mktime(struct tm *tm);
char *asctime(const struct tm *tm);
char *ctime(const time_t *t);
size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm);

int clock_gettime(long clk_id, struct timespec *ts);
int clock_getres(long clk_id, struct timespec *res);
int gettimeofday(struct timeval *tv, void *tz);
int nanosleep(const struct timespec *req, struct timespec *rem);
int usleep(unsigned long us);
unsigned int sleep(unsigned int sec);

#ifdef __cplusplus
}
#endif

#endif /* _ONYX_TIME_H */
