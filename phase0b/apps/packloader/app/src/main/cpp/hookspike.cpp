// T5 step 1: hook-technique spike (technique A, Dobby).
// Hooks open/openat/__openat/fchmod/fopen/opendir inside this app's own
// native library and measures install success, coverage and per-call cost.
#include <jni.h>
#include <android/log.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <ftw.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <time.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <atomic>

#include "Dobby/dobby.h"
#include "xdl.h"

#define TAG "PACK"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

static std::atomic<long> c_open(0), c_openat(0), c___openat(0), c_fchmod(0), c_fopen(0), c_opendir(0);

static int (*orig_open)(const char *, int, ...) = nullptr;
static int (*orig_openat)(int, const char *, int, ...) = nullptr;
static int (*orig___openat)(int, const char *, int, ...) = nullptr;
static int (*orig_fchmod)(int, mode_t) = nullptr;
static FILE *(*orig_fopen)(const char *, const char *) = nullptr;
static DIR *(*orig_opendir)(const char *) = nullptr;

static int h_open(const char *p, int f, ...) {
    c_open++;
    if (f & O_CREAT) {
        va_list ap;
        va_start(ap, f);
        mode_t m = va_arg(ap, mode_t);
        va_end(ap);
        return orig_open(p, f, m);
    }
    return orig_open(p, f);
}

static int h_openat(int d, const char *p, int f, ...) {
    c_openat++;
    if (f & O_CREAT) {
        va_list ap;
        va_start(ap, f);
        mode_t m = va_arg(ap, mode_t);
        va_end(ap);
        return orig_openat(d, p, f, m);
    }
    return orig_openat(d, p, f);
}

static int h___openat(int d, const char *p, int f, ...) {
    c___openat++;
    if (f & O_CREAT) {
        va_list ap;
        va_start(ap, f);
        mode_t m = va_arg(ap, mode_t);
        va_end(ap);
        return orig___openat(d, p, f, m);
    }
    return orig___openat(d, p, f);
}

static int h_fchmod(int fd, mode_t m) {
    c_fchmod++;
    return orig_fchmod(fd, m);
}

static FILE *h_fopen(const char *p, const char *m) {
    c_fopen++;
    return orig_fopen(p, m);
}

static DIR *h_opendir(const char *p) {
    c_opendir++;
    return orig_opendir(p);
}

static bool hook_one(const char *name, void *target, void *repl, void **orig) {
    if (!target) {
        LOGI("install %s: NOT FOUND", name);
        return false;
    }
    void *tramp = nullptr;
    int r = DobbyHook(target, repl, &tramp);
    if (r == 0) {
        *orig = tramp;
        LOGI("install %s: OK target=%p tramp=%p", name, target, tramp);
        return true;
    }
    LOGI("install %s: FAIL r=%d target=%p", name, r, target);
    return false;
}

static long bench_open(int n) {
    struct timespec a, b;
    clock_gettime(CLOCK_MONOTONIC, &a);
    for (int i = 0; i < n; i++) {
        int fd = open("/proc/self/cmdline", O_RDONLY);
        if (fd >= 0) close(fd);
    }
    clock_gettime(CLOCK_MONOTONIC, &b);
    return (b.tv_sec - a.tv_sec) * 1000000000L + (b.tv_nsec - a.tv_nsec);
}

static int nftw_cb(const char *p, const struct stat *s, int t, struct FTW *f) {
    (void) p;
    (void) s;
    (void) t;
    (void) f;
    return 0;
}

extern "C" JNIEXPORT jstring JNICALL
Java_dev_dualapp_packloader_HookSpike_spike(JNIEnv *env, jclass, jstring jdir) {
    const char *dir = env->GetStringUTFChars(jdir, nullptr);
    char out[4096];
    int pos = 0;
    auto add = [&](const char *fmt, ...) {
        va_list ap;
        va_start(ap, fmt);
        int n = vsnprintf(out + pos, sizeof(out) - pos, fmt, ap);
        va_end(ap);
        if (n > 0) pos += n;
    };

    LOGI("spike start");
    long base = bench_open(1000000);
    LOGI("baseline 1M open: %ld ns (%ld ns/call)", base, base / 1000000);

    void *h = dlopen("libc.so", RTLD_NOW);
    orig_open = (int (*)(const char *, int, ...)) dlsym(h, "open");
    orig_openat = (int (*)(int, const char *, int, ...)) dlsym(h, "openat");
    orig_fchmod = (int (*)(int, mode_t)) dlsym(h, "fchmod");
    orig_fopen = (FILE *(*)(const char *, const char *)) dlsym(h, "fopen");
    orig_opendir = (DIR *(*)(const char *)) dlsym(h, "opendir");
    void *xh = xdl_open("libc.so", XDL_DEFAULT);
    void *sym___openat = xh ? xdl_dsym(xh, "__openat", nullptr) : nullptr;

    bool ok_open = hook_one("open", (void *) orig_open, (void *) h_open, (void **) &orig_open);
    bool ok_openat = hook_one("openat", (void *) orig_openat, (void *) h_openat, (void **) &orig_openat);
    bool ok___openat = hook_one("__openat", sym___openat, (void *) h___openat, (void **) &orig___openat);
    bool ok_fchmod = hook_one("fchmod", (void *) orig_fchmod, (void *) h_fchmod, (void **) &orig_fchmod);
    bool ok_fopen = hook_one("fopen", (void *) orig_fopen, (void *) h_fopen, (void **) &orig_fopen);
    bool ok_opendir = hook_one("opendir", (void *) orig_opendir, (void *) h_opendir, (void **) &orig_opendir);

    long hooked = bench_open(1000000);
    LOGI("hooked 1M open: %ld ns (%ld ns/call)", hooked, hooked / 1000000);

    long f0 = c_fopen.load();
    for (int i = 0; i < 10000; i++) {
        FILE *f = fopen("/proc/self/cmdline", "r");
        if (f) fclose(f);
    }
    long f1 = c_fopen.load();

    long d0 = c_opendir.load();
    for (int i = 0; i < 1000; i++) {
        DIR *d = opendir("/proc/self");
        if (d) closedir(d);
    }
    long d1 = c_opendir.load();

    int ffd = open("/proc/self/cmdline", O_RDONLY);
    if (ffd >= 0) {
        fchmod(ffd, 0644);
        close(ffd);
    }

    char sub[512];
    snprintf(sub, sizeof(sub), "%s/spike", dir);
    mkdir(sub, 0700);
    for (int i = 0; i < 10; i++) {
        char p[600];
        snprintf(p, sizeof(p), "%s/f%d", sub, i);
        int fd = open(p, O_CREAT | O_WRONLY, 0600);
        if (fd >= 0) close(fd);
    }
    long n0 = c___openat.load();
    nftw(sub, nftw_cb, 10, FTW_PHYS);
    long n1 = c___openat.load();

    add("installed open=%d openat=%d __openat=%d fchmod=%d fopen=%d opendir=%d\n",
        ok_open, ok_openat, ok___openat, ok_fchmod, ok_fopen, ok_opendir);
    add("baseline_ns_per_open=%ld\n", base / 1000000);
    add("hooked_ns_per_open=%ld\n", hooked / 1000000);
    add("delta_ns_per_open=%ld\n", (hooked - base) / 1000000);
    add("counts open=%ld openat=%ld __openat=%ld fchmod=%ld fopen=%ld opendir=%ld\n",
        c_open.load(), c_openat.load(), c___openat.load(), c_fchmod.load(),
        c_fopen.load(), c_opendir.load());
    add("fopen_calls=%ld opendir_calls=%ld nftw___openat_delta=%ld\n",
        f1 - f0, d1 - d0, n1 - n0);
    add("SPIKE DONE\n");

    LOGI("spike done");
    env->ReleaseStringUTFChars(jdir, dir);
    return env->NewStringUTF(out);
}
