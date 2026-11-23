/*
 * rtc_offsetd: keep the wall clock across reboots on msm8960.
 *
 * The PM8921 RTC cannot be set from Linux (rtc_write_enable = false; Qualcomm's
 * time_daemon kept an offset instead), so every boot started at the RTC's
 * count since its last reset (early January 1970) until network time arrived.
 * That made the clock wrong without network and broke everything that compares
 * timestamps written at boot, e.g. the package parser cache (cache files dated
 * 1970 are always older than the APKs, so ~150 packages were re-parsed at every
 * boot).
 *
 *   rtc_offsetd restore   once, from post-fs-data before zygote: set the clock
 *                         to RTC + saved offset (only forward, only if sane)
 *   rtc_offsetd watch     daemon: block on a CLOCK_REALTIME timerfd with
 *                         TFD_TIMER_CANCEL_ON_SET, which returns only when the
 *                         wall clock is set (network time, user, restore), and
 *                         save "wall clock - RTC" then. No periodic wakeups.
 */
#include <errno.h>
#include <fcntl.h>
#include <linux/rtc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

#include <log/log.h>

#undef LOG_TAG
#define LOG_TAG "rtc_offsetd"

static const char kRtcDev[] = "/dev/rtc0";
static const char kOffsetFile[] = "/data/vendor/rtc/offset";
static const char kOffsetTmp[] = "/data/vendor/rtc/offset.tmp";
// Anything before 2026-01-01 is not a real wall clock (RTC count or build-time floor guess).
static const long long kMinSane = 1767225600LL;

static bool read_rtc(long long* out) {
    int fd = open(kRtcDev, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        ALOGE("open %s: %s", kRtcDev, strerror(errno));
        return false;
    }
    struct rtc_time rt;
    memset(&rt, 0, sizeof(rt));
    int r = ioctl(fd, RTC_RD_TIME, &rt);
    close(fd);
    if (r < 0) {
        ALOGE("RTC_RD_TIME: %s", strerror(errno));
        return false;
    }
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    tm.tm_sec = rt.tm_sec;
    tm.tm_min = rt.tm_min;
    tm.tm_hour = rt.tm_hour;
    tm.tm_mday = rt.tm_mday;
    tm.tm_mon = rt.tm_mon;
    tm.tm_year = rt.tm_year;
    *out = (long long)timegm(&tm);
    return true;
}

static bool read_offset(long long* out) {
    FILE* f = fopen(kOffsetFile, "re");
    if (!f) return false;
    bool ok = fscanf(f, "%lld", out) == 1;
    fclose(f);
    return ok;
}

static void save_offset() {
    struct timeval now;
    gettimeofday(&now, nullptr);
    long long rtc;
    if (now.tv_sec < kMinSane || !read_rtc(&rtc)) return;
    long long off = (long long)now.tv_sec - rtc;
    long long old;
    if (read_offset(&old) && llabs(old - off) <= 1) return;  // unchanged (rounding)
    int fd = open(kOffsetTmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) {
        ALOGE("open %s: %s", kOffsetTmp, strerror(errno));
        return;
    }
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%lld\n", off);
    bool ok = write(fd, buf, n) == n && fsync(fd) == 0;
    close(fd);
    if (!ok || rename(kOffsetTmp, kOffsetFile) != 0) {
        ALOGE("saving offset failed: %s", strerror(errno));
        return;
    }
    ALOGI("saved offset %lld s (rtc %lld)", off, rtc);
}

static int do_restore() {
    long long off, rtc;
    if (!read_offset(&off)) {
        ALOGI("no saved offset yet");
        return 0;
    }
    if (!read_rtc(&rtc)) return 1;
    long long t = rtc + off;
    struct timeval now;
    gettimeofday(&now, nullptr);
    // Only move forward: never undo a newer clock (e.g. already set by someone else).
    if (t < kMinSane || t <= (long long)now.tv_sec + 60) {
        ALOGI("clock kept (rtc %lld + offset %lld = %lld, now %lld)", rtc, off, t, (long long)now.tv_sec);
        return 0;
    }
    struct timeval tv = {.tv_sec = (time_t)t, .tv_usec = 0};
    if (settimeofday(&tv, nullptr) != 0) {
        ALOGE("settimeofday: %s", strerror(errno));
        return 1;
    }
    ALOGI("clock restored: rtc %lld + offset %lld = %lld", rtc, off, t);
    return 0;
}

static int do_watch() {
    save_offset();  // the clock may already be right (restored or network)
    for (;;) {
        int tfd = timerfd_create(CLOCK_REALTIME, TFD_CLOEXEC);
        if (tfd < 0) {
            ALOGE("timerfd_create: %s", strerror(errno));
            return 1;
        }
        struct itimerspec its;
        memset(&its, 0, sizeof(its));
        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        its.it_value.tv_sec = now.tv_sec + 10LL * 365 * 24 * 3600;  // effectively never fires
        if (timerfd_settime(tfd, TFD_TIMER_ABSTIME | TFD_TIMER_CANCEL_ON_SET, &its, nullptr) != 0) {
            ALOGE("timerfd_settime: %s", strerror(errno));
            close(tfd);
            return 1;
        }
        unsigned long long ticks;
        ssize_t r = read(tfd, &ticks, sizeof(ticks));
        int err = errno;
        close(tfd);
        if (r < 0 && err == ECANCELED) {
            save_offset();  // wall clock was set
        } else if (r < 0 && err != EINTR) {
            ALOGE("timerfd read: %s", strerror(err));
            sleep(60);
        }
    }
}

int main(int argc, char** argv) {
    if (argc == 2 && !strcmp(argv[1], "restore")) return do_restore();
    if (argc == 2 && !strcmp(argv[1], "watch")) return do_watch();
    fprintf(stderr, "usage: %s restore|watch\n", argv[0]);
    return 2;
}
