/*
 * time.c — date/time functions for libonyxc v0.5.
 *
 * Implementations:
 *   - time() / clock() / difftime()
 *   - gmtime() / localtime() — localtime is UTC (no TZ database)
 *   - mktime()
 *   - asctime() / ctime()
 *   - strftime() — covers %Y %m %d %H %M %S %y %j %p %I %A %a %B %b %w %U %%
 *   - clock_gettime / clock_getres / gettimeofday / nanosleep / sleep / usleep
 *
 * Days-in-month and leap-year logic is straightforward. No localization.
 */
#include "onyxc.h"

/* Days in each month for non-leap years. */
static const int mdays[2][12] = {
    {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},   /* non-leap */
    {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},   /* leap */
};

static int is_leap(int year) {
    if (year % 4 != 0) return 0;
    if (year % 100 != 0) return 1;
    if (year % 400 != 0) return 0;
    return 1;
}

static int day_of_week(int year, int month, int day) {
    /* Zeller's congruence. month: 3=March..14=Feb (Jan/Feb treated as 13/14 of prev year). */
    if (month < 3) { month += 12; year -= 1; }
    int c = year / 100;
    int y = year % 100;
    int h = (day + (13 * (month + 1)) / 5 + y + (y / 4) + (c / 4) + 5 * c) % 7;
    return (h + 6) % 7;   /* 0 = Sunday */
}

static int day_of_year(int year, int month, int day) {
    int doy = 0;
    int leap = is_leap(year);
    for (int m = 0; m < month - 1; m++) {
        doy += mdays[leap][m];
    }
    return doy + day;
}

time_t time(time_t *t) {
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) < 0) {
        if (t) *t = (time_t)-1;
        return (time_t)-1;
    }
    if (t) *t = ts.tv_sec;
    return ts.tv_sec;
}

clock_t clock(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) return (clock_t)-1;
    return (clock_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

double difftime(time_t end, time_t start) {
    return (double)end - (double)start;
}

static struct tm g_tm_buf;

struct tm *gmtime(const time_t *t) {
    if (!t) return NULL;
    time_t v = *t;
    int sec = (int)(v % 60); v /= 60;
    int min = (int)(v % 60); v /= 60;
    int hour = (int)(v % 24); v /= 24;
    /* v is now days since 1970-01-01. */
    int days = (int)v;
    int year = 1970;
    while (1) {
        int dy = is_leap(year) ? 366 : 365;
        if (days < dy) break;
        days -= dy;
        year++;
    }
    int month = 0;
    int leap = is_leap(year);
    while (month < 12 && days >= mdays[leap][month]) {
        days -= mdays[leap][month];
        month++;
    }
    g_tm_buf.tm_sec = sec;
    g_tm_buf.tm_min = min;
    g_tm_buf.tm_hour = hour;
    g_tm_buf.tm_mday = days + 1;
    g_tm_buf.tm_mon = month;
    g_tm_buf.tm_year = year - 1900;
    g_tm_buf.tm_wday = day_of_week(year, month + 1, days + 1);
    g_tm_buf.tm_yday = day_of_year(year, month + 1, days + 1);
    g_tm_buf.tm_isdst = 0;
    return &g_tm_buf;
}

struct tm *localtime(const time_t *t) {
    return gmtime(t);   /* No TZ database. */
}

time_t mktime(struct tm *tm) {
    if (!tm) return (time_t)-1;
    int year = tm->tm_year + 1900;
    int leap = is_leap(year);
    /* Walk years back to 1970. */
    time_t t = 0;
    for (int y = 1970; y < year; y++) {
        t += is_leap(y) ? 366 : 365;
    }
    for (int m = 0; m < tm->tm_mon; m++) {
        t += mdays[leap][m];
    }
    t += tm->tm_mday - 1;
    t *= 24; t += tm->tm_hour;
    t *= 60; t += tm->tm_min;
    t *= 60; t += tm->tm_sec;
    return t;
}

static char g_asc_buf[32];

char *asctime(const struct tm *tm) {
    static const char *wday[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    static const char *moname[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                  "Jul","Aug","Sep","Oct","Nov","Dec"};
    if (!tm) return NULL;
    /* "Www Mmm DD HH:MM:SS YYYY\n" */
    int n = sprintf(g_asc_buf, "%s %s %2d %02d:%02d:%02d %d\n",
        wday[tm->tm_wday & 7],
        moname[tm->tm_mon % 12],
        tm->tm_mday,
        tm->tm_hour, tm->tm_min, tm->tm_sec,
        tm->tm_year + 1900);
    (void)n;
    return g_asc_buf;
}

char *ctime(const time_t *t) {
    return asctime(gmtime(t));
}

/* strftime helper context (file scope — no nested functions, OnyxCC
 * does not support the GNU nested-function extension). */
typedef struct {
    char *s;
    size_t n;
    size_t max;
} sfmt_ctx_t;

static int sfmt_emit(sfmt_ctx_t *c, const char *str, size_t len) {
    if (c->n + len >= c->max) return -1;
    for (size_t i = 0; i < len; i++) c->s[c->n++] = str[i];
    return 0;
}

size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm) {
    if (!s || !fmt || !tm || max == 0) return 0;
    sfmt_ctx_t ctx;
    ctx.s = s;
    ctx.n = 0;
    ctx.max = max;
    size_t *np = &ctx.n;   /* alias kept minimal */
    (void)np;
    static const char *wday[] = {"Sunday","Monday","Tuesday","Wednesday",
                                "Thursday","Friday","Saturday"};
    static const char *wday_short[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    static const char *moname[] = {"January","February","March","April","May","June",
                                  "July","August","September","October","November","December"};
    static const char *moname_short[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                        "Jul","Aug","Sep","Oct","Nov","Dec"};
    size_t n = 0;

    while (*fmt && ctx.n + 1 < max) {
        if (*fmt != '%') { s[ctx.n++] = *fmt++; continue; }
        fmt++;
        char buf[16];
        switch (*fmt) {
            case 'Y':
                snprintf(buf, sizeof(buf), "%04d", tm->tm_year + 1900);
                if (sfmt_emit(&ctx, buf, strlen(buf)) < 0) return ctx.n;
                break;
            case 'y':
                snprintf(buf, sizeof(buf), "%02d", (tm->tm_year + 1900) % 100);
                if (sfmt_emit(&ctx, buf, strlen(buf)) < 0) return ctx.n;
                break;
            case 'm':
                snprintf(buf, sizeof(buf), "%02d", tm->tm_mon + 1);
                if (sfmt_emit(&ctx, buf, strlen(buf)) < 0) return ctx.n;
                break;
            case 'd':
                snprintf(buf, sizeof(buf), "%02d", tm->tm_mday);
                if (sfmt_emit(&ctx, buf, strlen(buf)) < 0) return ctx.n;
                break;
            case 'H':
                snprintf(buf, sizeof(buf), "%02d", tm->tm_hour);
                if (sfmt_emit(&ctx, buf, strlen(buf)) < 0) return ctx.n;
                break;
            case 'I': {
                int h = tm->tm_hour % 12; if (h == 0) h = 12;
                snprintf(buf, sizeof(buf), "%02d", h);
                if (sfmt_emit(&ctx, buf, strlen(buf)) < 0) return ctx.n;
                break;
            }
            case 'M':
                snprintf(buf, sizeof(buf), "%02d", tm->tm_min);
                if (sfmt_emit(&ctx, buf, strlen(buf)) < 0) return ctx.n;
                break;
            case 'S':
                snprintf(buf, sizeof(buf), "%02d", tm->tm_sec);
                if (sfmt_emit(&ctx, buf, strlen(buf)) < 0) return ctx.n;
                break;
            case 'j':
                snprintf(buf, sizeof(buf), "%03d", tm->tm_yday + 1);
                if (sfmt_emit(&ctx, buf, strlen(buf)) < 0) return ctx.n;
                break;
            case 'p':
                if (sfmt_emit(&ctx, tm->tm_hour < 12 ? "AM" : "PM", 2) < 0) return ctx.n;
                break;
            case 'A':
                if (sfmt_emit(&ctx, wday[tm->tm_wday & 7], strlen(wday[tm->tm_wday & 7])) < 0) return ctx.n;
                break;
            case 'a':
                if (sfmt_emit(&ctx, wday_short[tm->tm_wday & 7], 3) < 0) return ctx.n;
                break;
            case 'B':
                if (sfmt_emit(&ctx, moname[tm->tm_mon % 12], strlen(moname[tm->tm_mon % 12])) < 0) return ctx.n;
                break;
            case 'b': case 'h':
                if (sfmt_emit(&ctx, moname_short[tm->tm_mon % 12], 3) < 0) return ctx.n;
                break;
            case 'w':
                snprintf(buf, sizeof(buf), "%d", tm->tm_wday);
                if (sfmt_emit(&ctx, buf, strlen(buf)) < 0) return ctx.n;
                break;
            case 'U': {
                int w = (tm->tm_yday - tm->tm_wday + 7) / 7;
                if (w < 0) w = 0;
                snprintf(buf, sizeof(buf), "%02d", w);
                if (sfmt_emit(&ctx, buf, strlen(buf)) < 0) return ctx.n;
                break;
            }
            case '%':
                if (ctx.n + 1 >= max) return ctx.n;
                s[ctx.n++] = '%';
                break;
            default:
                if (ctx.n + 2 >= max) return ctx.n;
                s[ctx.n++] = '%';
                s[ctx.n++] = *fmt;
                break;
        }
        if (*fmt == 0) break;
        fmt++;
    }
    s[ctx.n] = 0;
    n = ctx.n;
    return n;
}

int clock_gettime(long clk_id, struct timespec *ts) {
    long r = _onyx_clock_gettime(clk_id, ts);
    if (r < 0) { errno = (int)(-r); return -1; }
    return 0;
}

int clock_getres(long clk_id, struct timespec *res) {
    long r = _onyx_clock_getres(clk_id, res);
    if (r < 0) { errno = (int)(-r); return -1; }
    return 0;
}

int gettimeofday(struct timeval *tv, void *tz) {
    (void)tz;
    long r = _onyx_gettimeofday(tv);
    if (r < 0) { errno = (int)(-r); return -1; }
    return 0;
}

int nanosleep(const struct timespec *req, struct timespec *rem) {
    long r = _onyx_nanosleep(req, rem);
    if (r < 0) { errno = (int)(-r); return -1; }
    return 0;
}

int usleep(unsigned long us) {
    struct timespec req;
    req.tv_sec = (long)(us / 1000000);
    req.tv_nsec = (long)((us % 1000000) * 1000);
    return nanosleep(&req, NULL);
}

unsigned int sleep(unsigned int sec) {
    struct timespec req;
    req.tv_sec = (long)sec;
    req.tv_nsec = 0;
    if (nanosleep(&req, NULL) < 0) return sec;
    return 0;
}

unsigned int alarm(unsigned int sec) {
    (void)sec;
    /* No timer support in kernel yet — would need SIGALRM delivery. */
    return 0;
}
