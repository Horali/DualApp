// Canary v0.3 native probes for Phase 0b T2 / T3 / T6.
// Read-only except escapeTest() and setxattrTest(), which are only called
// from buttons the operator presses deliberately.
#include <jni.h>
#include <android/log.h>
#include <sys/system_properties.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/xattr.h>
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
