// Canary v0.3 native probes for Phase 0b T2 / T3 / T6.
// Read-only except escapeTest() and setxattrTest(), which are only called
// from buttons the operator presses deliberately.
#include <jni.h>
#include <android/log.h>
#include <sys/system_properties.h>
#include <sys/inotify.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/vfs.h>
#include <sys/xattr.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#define TAG "CANARY_ENV"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

static jstring mk(JNIEnv *env, const char *s) {
    return env->NewStringUTF(s ? s : "");
}

extern "C" JNIEXPORT jstring JNICALL
Java_dev_dualapp_canary_EnvProbe_oldPropGet(JNIEnv *env, jclass, jstring jname) {
    const char *name = env->GetStringUTFChars(jname, nullptr);
    char value[PROP_VALUE_MAX] = {0};
    int n = __system_property_get(name, value);
    env->ReleaseStringUTFChars(jname, name);
    char out[PROP_VALUE_MAX + 32];
    snprintf(out, sizeof(out), "%s (len=%d)", value[0] ? value : "<empty>", n);
    return mk(env, out);
}

static void prop_cb(void *cookie, const char *name, const char *value, uint32_t serial) {
    (void) name;
    (void) serial;
    if (cookie) snprintf((char *) cookie, PROP_VALUE_MAX, "%s", value ? value : "");
}

extern "C" JNIEXPORT jstring JNICALL
Java_dev_dualapp_canary_EnvProbe_modernPropFind(JNIEnv *env, jclass, jstring jname) {
    const char *name = env->GetStringUTFChars(jname, nullptr);
    const prop_info *pi = __system_property_find(name);
    env->ReleaseStringUTFChars(jname, name);
    if (!pi) return mk(env, "<not found>");
    char value[PROP_VALUE_MAX] = {0};
    __system_property_read_callback(pi, prop_cb, value);
    char out[PROP_VALUE_MAX + 32];
    snprintf(out, sizeof(out), "%s (found)", value[0] ? value : "<empty>");
    return mk(env, out);
}

// T2 step 3: hard-coded paths from native code.
//   /data/data/dev.dualapp.canary/files/x  -> expected EACCES (different UID)
//   /sdcard/Download/.dualapp-escape-test  -> expected to succeed (engine has All Files Access)
// The second file is 1 byte and is deleted immediately.
extern "C" JNIEXPORT jstring JNICALL
Java_dev_dualapp_canary_EnvProbe_escapeTest(JNIEnv *env, jclass) {
    char out[512];

    int fd1 = open("/data/data/dev.dualapp.canary/files/x", O_RDWR | O_CREAT, 0600);
    int e1 = errno;
    int ok1 = fd1 >= 0;
    if (ok1) close(fd1);

    int fd2 = open("/sdcard/Download/.dualapp-escape-test", O_WRONLY | O_CREAT | O_TRUNC, 0600);
    int e2 = errno;
    int ok2 = fd2 >= 0;
    int wrote = 0;
    if (ok2) {
        wrote = (write(fd2, "X", 1) == 1);
        close(fd2);
        unlink("/sdcard/Download/.dualapp-escape-test");
    }

    snprintf(out, sizeof(out),
             "dataData.open=%s errno=%d (%s); sdcard.open=%s errno=%d (%s) wrote=%d",
             ok1 ? "ALLOWED" : "DENIED", e1, strerror(e1),
             ok2 ? "ALLOWED" : "DENIED", e2, strerror(e2), wrote);
    LOGI("escape %s", out);
    return mk(env, out);
}

// T3 step 4: can a guest set an xattr on its own file?
extern "C" JNIEXPORT jstring JNICALL
Java_dev_dualapp_canary_EnvProbe_setxattrTest(JNIEnv *env, jclass, jstring jpath) {
    const char *path = env->GetStringUTFChars(jpath, nullptr);
    int r = setxattr(path, "user.dualapp.blob", "test", 4, 0);
    int e = errno;
    char out[256];
    snprintf(out, sizeof(out), "setxattr ret=%d errno=%d (%s)", r, e, strerror(e));
    env->ReleaseStringUTFChars(jpath, path);
    LOGI("%s", out);
    return mk(env, out);
}

// T6 C18: raw syscall read, bypassing libc hooks.
extern "C" JNIEXPORT jstring JNICALL
Java_dev_dualapp_canary_EnvProbe_rawOpenRead(JNIEnv *env, jclass, jstring jpath) {
    const char *path = env->GetStringUTFChars(jpath, nullptr);
    int fd = (int) syscall(SYS_openat, AT_FDCWD, path, O_RDONLY);
    int e = errno;
    char out[256];
    if (fd < 0) {
        snprintf(out, sizeof(out), "raw open DENIED errno=%d (%s)", e, strerror(e));
    } else {
        unsigned char b[16];
        ssize_t n = read(fd, b, sizeof(b));
        close(fd);
        char hex[64] = {0};
        for (ssize_t i = 0; i < n && i < 16; i++) {
            snprintf(hex + i * 2, sizeof(hex) - i * 2, "%02x", b[i]);
        }
        snprintf(out, sizeof(out), "raw open OK n=%zd hex=%s", n, hex);
    }
    env->ReleaseStringUTFChars(jpath, path);
    LOGI("%s", out);
    return mk(env, out);
}

// T3 step 11: handle budget.
extern "C" JNIEXPORT jstring JNICALL
Java_dev_dualapp_canary_EnvProbe_rlimitNofile(JNIEnv *env, jclass) {
    struct rlimit r;
    char out[128];
    if (getrlimit(RLIMIT_NOFILE, &r) != 0) {
        snprintf(out, sizeof(out), "getrlimit failed errno=%d (%s)", errno, strerror(errno));
    } else {
        snprintf(out, sizeof(out), "cur=%llu max=%llu",
                 (unsigned long long) r.rlim_cur, (unsigned long long) r.rlim_max);
    }
    return mk(env, out);
}

// T3 step 1: filesystem type and block size (statfs f_type).
extern "C" JNIEXPORT jstring JNICALL
Java_dev_dualapp_canary_EnvProbe_fsType(JNIEnv *env, jclass, jstring jpath) {
    const char *path = env->GetStringUTFChars(jpath, nullptr);
    struct statfs st;
    char out[256];
    if (statfs(path, &st) != 0) {
        snprintf(out, sizeof(out), "statfs failed errno=%d (%s)", errno, strerror(errno));
    } else {
        snprintf(out, sizeof(out), "type=0x%llx bsize=%lld blocks=%lld bfree=%lld",
                 (unsigned long long) st.f_type, (long long) st.f_bsize,
                 (long long) st.f_blocks, (long long) st.f_bfree);
    }
    env->ReleaseStringUTFChars(jpath, path);
    return mk(env, out);
}

// T3 step 10: io_uring_setup from a guest.
extern "C" JNIEXPORT jstring JNICALL
Java_dev_dualapp_canary_EnvProbe_ioUringSetup(JNIEnv *env, jclass) {
    char params[256] = {0};
#ifdef __NR_io_uring_setup
    long fd = syscall(__NR_io_uring_setup, 8, params);
#else
    long fd = syscall(425, 8, params);  // arm64 __NR_io_uring_setup
#endif
    int e = errno;
    char out[128];
    if (fd < 0) {
        snprintf(out, sizeof(out), "io_uring_setup DENIED errno=%d (%s)", e, strerror(e));
    } else {
        snprintf(out, sizeof(out), "io_uring_setup OK fd=%ld", fd);
        close((int) fd);
    }
    return mk(env, out);
}

// T3 step 7: fchmod on a read-only handle, then reopen /proc/self/fd for writing.
extern "C" JNIEXPORT jstring JNICALL
Java_dev_dualapp_canary_EnvProbe_fchmodReopen(JNIEnv *env, jclass, jstring jpath) {
    const char *path = env->GetStringUTFChars(jpath, nullptr);
    char out[512];
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        snprintf(out, sizeof(out), "open read-only failed errno=%d (%s)", errno, strerror(errno));
    } else {
        int r = fchmod(fd, 0644);
        int e1 = errno;
        char proc[64];
        snprintf(proc, sizeof(proc), "/proc/self/fd/%d", fd);
        int fd2 = open(proc, O_RDWR);
        int e2 = errno;
        snprintf(out, sizeof(out), "fchmod=%s errno=%d; reopen=%s errno=%d (%s)",
                 r == 0 ? "ALLOWED" : "DENIED", e1,
                 fd2 >= 0 ? "ALLOWED" : "DENIED", e2, strerror(e2));
        if (fd2 >= 0) close(fd2);
        close(fd);
    }
    env->ReleaseStringUTFChars(jpath, path);
    return mk(env, out);
}

// T3 step 9: copy_file_range speed (raw syscall; not in the public SDK stubs).
extern "C" JNIEXPORT jstring JNICALL
Java_dev_dualapp_canary_EnvProbe_copyRange(JNIEnv *env, jclass, jstring jsrc, jstring jdst, jlong size) {
    const char *src = env->GetStringUTFChars(jsrc, nullptr);
    const char *dst = env->GetStringUTFChars(jdst, nullptr);
    char out[256];
    int sfd = open(src, O_RDONLY);
    int dfd = open(dst, O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (sfd < 0 || dfd < 0) {
        snprintf(out, sizeof(out), "open failed sfd=%d dfd=%d errno=%d (%s)",
                 sfd, dfd, errno, strerror(errno));
    } else {
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        long long copied = 0;
        while (copied < size) {
#ifdef __NR_copy_file_range
            long n = syscall(__NR_copy_file_range, sfd, nullptr, dfd, nullptr,
                             (size_t) (size - copied), 0);
#else
            long n = -1;
#endif
            if (n <= 0) break;
            copied += n;
        }
        fsync(dfd);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        double ms = (t1.tv_sec - t0.tv_sec) * 1000.0 + (t1.tv_nsec - t0.tv_nsec) / 1e6;
        snprintf(out, sizeof(out), "bytes=%lld ms=%.0f MBps=%.0f",
                 copied, ms, copied / 1048576.0 / (ms / 1000.0));
    }
    if (sfd >= 0) close(sfd);
    if (dfd >= 0) close(dfd);
    env->ReleaseStringUTFChars(jsrc, src);
    env->ReleaseStringUTFChars(jdst, dst);
    return mk(env, out);
}

// T3 step 8: inotify on the guest's external folder.
extern "C" JNIEXPORT jstring JNICALL
Java_dev_dualapp_canary_EnvProbe_inotifyTest(JNIEnv *env, jclass, jstring jdir) {
    const char *dir = env->GetStringUTFChars(jdir, nullptr);
    char out[256];
    int fd = inotify_init1(IN_NONBLOCK);
    if (fd < 0) {
        snprintf(out, sizeof(out), "inotify_init failed errno=%d (%s)", errno, strerror(errno));
    } else {
        int w = inotify_add_watch(fd, dir, IN_CREATE | IN_CLOSE_WRITE | IN_OPEN);
        char path[512];
        snprintf(path, sizeof(path), "%s/event", dir);
        int f = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (f >= 0) close(f);
        char buf[4096];
        ssize_t n = read(fd, buf, sizeof(buf));
        snprintf(out, sizeof(out), "watch=%d read=%zd", w, n);
        close(fd);
    }
    env->ReleaseStringUTFChars(jdir, dir);
    return mk(env, out);
}
