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
#include <sys/mman.h>
#include <dirent.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

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

// ---------- T6 conformance native helpers ----------
namespace {

struct Sha256 {
    uint32_t h[8];
    uint64_t len;
    uint8_t buf[64];
    size_t n;
    void init() {
        h[0] = 0x6a09e667; h[1] = 0xbb67ae85; h[2] = 0x3c6ef372; h[3] = 0xa54ff53a;
        h[4] = 0x510e527f; h[5] = 0x9b05688c; h[6] = 0x1f83d9ab; h[7] = 0x5be0cd19;
        len = 0; n = 0;
    }
    static uint32_t rotr(uint32_t x, int s) { return (x >> s) | (x << (32 - s)); }
    void block(const uint8_t *p) {
        static const uint32_t K[64] = {
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
            0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
            0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
            0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
            0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
            0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
            0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
        };
        uint32_t w[64];
        for (int i = 0; i < 16; i++)
            w[i] = ((uint32_t)p[i*4]<<24)|((uint32_t)p[i*4+1]<<16)|((uint32_t)p[i*4+2]<<8)|p[i*4+3];
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = rotr(w[i-15],7)^rotr(w[i-15],18)^(w[i-15]>>3);
            uint32_t s1 = rotr(w[i-2],17)^rotr(w[i-2],19)^(w[i-2]>>10);
            w[i] = w[i-16]+s0+w[i-7]+s1;
        }
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],k=h[7];
        for (int i = 0; i < 64; i++) {
            uint32_t S1 = rotr(e,6)^rotr(e,11)^rotr(e,25);
            uint32_t ch = (e&f)^((~e)&g);
            uint32_t t1 = k+S1+ch+K[i]+w[i];
            uint32_t S0 = rotr(a,2)^rotr(a,13)^rotr(a,22);
            uint32_t maj = (a&b)^(a&c)^(b&c);
            uint32_t t2 = S0+maj;
            k=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=k;
    }
    void update(const void *data, size_t sz) {
        const uint8_t *p = (const uint8_t *)data;
        len += sz;
        while (sz > 0) {
            size_t cp = 64 - n;
            if (cp > sz) cp = sz;
            memcpy(buf + n, p, cp);
            n += cp; p += cp; sz -= cp;
            if (n == 64) { block(buf); n = 0; }
        }
    }
    void finish(char out[65]) {
        buf[n++] = 0x80;
        if (n > 56) { memset(buf+n, 0, 64-n); block(buf); n = 0; }
        memset(buf+n, 0, 56-n);
        uint64_t bits = len * 8;
        for (int i = 0; i < 8; i++) buf[56+i] = (uint8_t)(bits >> (56 - 8*i));
        block(buf);
        for (int i = 0; i < 8; i++) snprintf(out + i*8, 9, "%08x", h[i]);
        out[64] = 0;
    }
};

static bool shaRead(const char *path, char out[65], off_t maxBytes = -1) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return false;
    Sha256 s; s.init();
    uint8_t buf[65536];
    off_t rem = maxBytes;
    while (rem != 0) {
        size_t want = sizeof(buf);
        if (rem > 0 && (off_t)want > rem) want = (size_t)rem;
        ssize_t n = read(fd, buf, want);
        if (n < 0) { close(fd); return false; }
        if (n == 0) break;
        s.update(buf, (size_t)n);
        if (rem > 0) rem -= n;
    }
    close(fd);
    s.finish(out);
    return true;
}

static bool shaFread(const char *path, char out[65]) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    Sha256 s; s.init();
    uint8_t buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) s.update(buf, n);
    bool err = ferror(f) != 0;
    fclose(f);
    if (err) return false;
    s.finish(out);
    return true;
}

static bool shaMmap(const char *path, char out[65]) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return false;
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size <= 0) { close(fd); return false; }
    void *p = mmap(nullptr, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (p == MAP_FAILED) return false;
    Sha256 s; s.init();
    s.update(p, (size_t)st.st_size);
    munmap(p, (size_t)st.st_size);
    s.finish(out);
    return true;
}

static bool isPh(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0 || !S_ISREG(st.st_mode)) return false;
    if (st.st_size > 0 && st.st_blocks > 8) return false;
    char buf[256];
    return getxattr(path, "user.dualapp.blob", buf, sizeof(buf) - 1) > 0;
}

static bool hasTag(const char *path) {
    char buf[256];
    return getxattr(path, "user.dualapp.blob", buf, sizeof(buf) - 1) > 0;
}

static bool cmpSlices(const char *a, const char *b, off_t off, off_t len) {
    int f1 = open(a, O_RDONLY), f2 = open(b, O_RDONLY);
    if (f1 < 0 || f2 < 0) { if (f1 >= 0) close(f1); if (f2 >= 0) close(f2); return false; }
    lseek(f1, off, SEEK_SET);
    lseek(f2, off, SEEK_SET);
    uint8_t b1[32768], b2[32768];
    off_t rem = len;
    bool ok = true;
    while (rem > 0) {
        size_t want = rem < (off_t)sizeof(b1) ? (size_t)rem : sizeof(b1);
        ssize_t n1 = read(f1, b1, want), n2 = read(f2, b2, want);
        if (n1 != (ssize_t)want || n2 != (ssize_t)want || memcmp(b1, b2, want) != 0) {
            ok = false; break;
        }
        rem -= want;
    }
    close(f1); close(f2);
    return ok;
}

static void mkPath(char *out, size_t n, const char *dir, const char *name) {
    snprintf(out, n, "%s/%s", dir, name);
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_dev_dualapp_canary_EnvProbe_t6Jni(JNIEnv *env, jclass, jstring jcase, jstring jdir,
                                       jstring jsha1m, jstring jsha64m, jstring jsha1000,
                                       jlong c21Mtime) {
    const char *cid = env->GetStringUTFChars(jcase, nullptr);
    const char *dir = env->GetStringUTFChars(jdir, nullptr);
    const char *sha1m = env->GetStringUTFChars(jsha1m, nullptr);
    const char *sha64m = env->GetStringUTFChars(jsha64m, nullptr);
    const char *sha1000 = env->GetStringUTFChars(jsha1000, nullptr);

    char out[512];
    char p[512], p2[512], ref[512];
    mkPath(ref, sizeof(ref), dir, "c01_jni.bin");

    if (!strcmp(cid, "C1")) {
        char s1[65]={}, s2[65]={}, s3[65]={}, s64[65]={};
        mkPath(p, sizeof(p), dir, "c01_jni.bin");
        mkPath(p2, sizeof(p2), dir, "c01_64m.bin");
        bool ok = shaRead(p, s1) && shaFread(p, s2) && shaMmap(p, s3) && shaRead(p2, s64)
                  && !strcmp(s1, sha1m) && !strcmp(s2, sha1m) && !strcmp(s3, sha1m)
                  && !strcmp(s64, sha64m) && isPh(p) && isPh(p2);
        snprintf(out, sizeof(out), "%s read=%s fread=%s mmap=%s 64m=%.12s",
                 ok ? "PASS" : "FAIL", !strcmp(s1, sha1m) ? "OK" : "BAD",
                 !strcmp(s2, sha1m) ? "OK" : "BAD", !strcmp(s3, sha1m) ? "OK" : "BAD", s64);
    } else if (!strcmp(cid, "C2")) {
        mkPath(p, sizeof(p), dir, "c02_jni.bin");
        int fd = open(p, O_WRONLY | O_APPEND);
        ssize_t w = fd >= 0 ? write(fd, "APPEND_C2_BYTES!", 16) : -1;
        if (fd >= 0) close(fd);
        struct stat st{}; char s1[65]={}, tail[17]={};
        bool ok = w == 16 && stat(p, &st) == 0 && st.st_size == 1048576 + 16
                  && shaRead(p, s1, 1048576) && !strcmp(s1, sha1m) && !hasTag(p);
        if (ok) {
            int f2 = open(p, O_RDONLY);
            if (f2 >= 0) { lseek(f2, 1048576, SEEK_SET); read(f2, tail, 16); close(f2); }
            if (memcmp(tail, "APPEND_C2_BYTES!", 16) != 0) ok = false;
        }
        snprintf(out, sizeof(out), "%s size=%lld tag=%d tail=%s",
                 ok ? "PASS" : "FAIL", (long long)st.st_size, hasTag(p), tail);
    } else if (!strcmp(cid, "C3")) {
        mkPath(p, sizeof(p), dir, "c03_jni.bin");
        int cr = chmod(p, 0644);
        int fd = open(p, O_WRONLY | O_APPEND);
        ssize_t w = fd >= 0 ? write(fd, "APPEND_C3_BYTES!", 16) : -1;
        if (fd >= 0) close(fd);
        struct stat st{}; char s1[65]={}, tail[17]={};
        bool ok = cr == 0 && w == 16 && stat(p, &st) == 0 && st.st_size == 1048576 + 16
                  && shaRead(p, s1, 1048576) && !strcmp(s1, sha1m) && !hasTag(p);
        if (ok) {
            int f2 = open(p, O_RDONLY);
            if (f2 >= 0) { lseek(f2, 1048576, SEEK_SET); read(f2, tail, 16); close(f2); }
            if (memcmp(tail, "APPEND_C3_BYTES!", 16) != 0) ok = false;
        }
        snprintf(out, sizeof(out), "%s chmod=%d size=%lld tail=%s",
                 ok ? "PASS" : "FAIL", cr, (long long)st.st_size, tail);
    } else if (!strcmp(cid, "C4")) {
        mkPath(p, sizeof(p), dir, "c04_jni.bin");
        int fd = open(p, O_RDONLY);
        int fr = fd >= 0 ? fchmod(fd, 0644) : -1;
        bool stillPhBeforeReopen = isPh(p);
        char proc[64]; snprintf(proc, sizeof(proc), "/proc/self/fd/%d", fd);
        int fd2 = fd >= 0 ? open(proc, O_RDWR) : -1;
        int e2 = errno;
        ssize_t w = fd2 >= 0 ? write(fd2, "REOPEN_C4_WRITE!", 16) : -1;
        if (fd2 >= 0) close(fd2);
        if (fd >= 0) close(fd);
        struct stat st{}; char head[17]={};
        int f3 = open(p, O_RDONLY);
        if (f3 >= 0) { read(f3, head, 16); close(f3); }
        bool ok = fr == 0 && stillPhBeforeReopen && fd2 >= 0 && w == 16
                  && stat(p, &st) == 0 && st.st_size == 1048576
                  && !memcmp(head, "REOPEN_C4_WRITE!", 16)
                  && cmpSlices(p, ref, 16, 1048576 - 16) && !hasTag(p);
        snprintf(out, sizeof(out), "%s fr=%d phBefore=%d reopen=%d(e=%d) w=%zd head=%s",
                 ok ? "PASS" : "FAIL", fr, stillPhBeforeReopen, fd2 >= 0, e2, w, head);
    } else if (!strcmp(cid, "C5")) {
        mkPath(p, sizeof(p), dir, "c05_jni.bin");
        int fd = open(p, O_RDONLY);
        char proc[64]; snprintf(proc, sizeof(proc), "/proc/self/fd/%d", fd);
        int r = fd >= 0 ? chmod(proc, 0666) : -1;
        if (fd >= 0) close(fd);
        struct stat st{};
        bool ok = r == 0 && stat(p, &st) == 0 && (st.st_mode & 0777) == 0666 && isPh(p);
        snprintf(out, sizeof(out), "%s r=%d mode=%o ph=%d",
                 ok ? "PASS" : "FAIL", r, (unsigned)(st.st_mode & 0777), isPh(p));
    } else if (!strcmp(cid, "C6")) {
        mkPath(p, sizeof(p), dir, "c06_jni.bin");
        const char *msg = "TRUNC_C6_NEW_CONTENT_32_BYTES!!_";
        int fd = open(p, O_WRONLY | O_TRUNC);
        ssize_t w = fd >= 0 ? write(fd, msg, 32) : -1;
        if (fd >= 0) close(fd);
        struct stat st{}; char got[33]={};
        int f2 = open(p, O_RDONLY);
        if (f2 >= 0) { read(f2, got, 32); close(f2); }
        bool ok = w == 32 && stat(p, &st) == 0 && st.st_size == 32 && !memcmp(got, msg, 32) && !hasTag(p);
        snprintf(out, sizeof(out), "%s size=%lld tag=%d", ok ? "PASS" : "FAIL", (long long)st.st_size, hasTag(p));
    } else if (!strcmp(cid, "C7")) {
        mkPath(p, sizeof(p), dir, "c07_jni.bin");
        int r = truncate(p, 1000);
        struct stat st{}; char s[65]={};
        bool ok = r == 0 && stat(p, &st) == 0 && st.st_size == 1000
                  && shaRead(p, s) && !strcmp(s, sha1000) && !hasTag(p);
        snprintf(out, sizeof(out), "%s r=%d size=%lld sha_ok=%d",
                 ok ? "PASS" : "FAIL", r, (long long)st.st_size, !strcmp(s, sha1000));
    } else if (!strcmp(cid, "C8")) {
        mkPath(p, sizeof(p), dir, "c08_jni.bin");
        int fd = open(p, O_RDONLY);
        int r = fd >= 0 ? ftruncate(fd, 1000) : 0;
        int e = errno;
        if (fd >= 0) close(fd);
        struct stat st{};
        bool ok = r == -1 && e == EINVAL && stat(p, &st) == 0 && st.st_size == 1048576 && isPh(p);
        snprintf(out, sizeof(out), "%s r=%d errno=%d(%s) ph=%d",
                 ok ? "PASS" : "FAIL", r, e, strerror(e), isPh(p));
    } else if (!strcmp(cid, "C9")) {
        mkPath(p, sizeof(p), dir, "c09_jni.bin");
        int fd = open(p, O_RDWR);
        if (fd >= 0) lseek(fd, 4096, SEEK_SET);
        ssize_t w = fd >= 0 ? write(fd, "PATCH_C9_MIDDLE!", 16) : -1;
        if (fd >= 0) close(fd);
        struct stat st{}; char mid[17]={};
        int f2 = open(p, O_RDONLY);
        if (f2 >= 0) { lseek(f2, 4096, SEEK_SET); read(f2, mid, 16); close(f2); }
        bool ok = w == 16 && stat(p, &st) == 0 && st.st_size == 1048576
                  && !memcmp(mid, "PATCH_C9_MIDDLE!", 16)
                  && cmpSlices(p, ref, 0, 4096) && cmpSlices(p, ref, 4112, 1048576 - 4112) && !hasTag(p);
        snprintf(out, sizeof(out), "%s size=%lld mid=%s", ok ? "PASS" : "FAIL", (long long)st.st_size, mid);
    } else if (!strcmp(cid, "C10")) {
        mkPath(p, sizeof(p), dir, "c10_jni.bin");
        int r = unlink(p);
        struct stat st{};
        bool ok = r == 0 && lstat(p, &st) != 0 && errno == ENOENT;
        snprintf(out, sizeof(out), "%s unlink=%d", ok ? "PASS" : "FAIL", r);
    } else if (!strcmp(cid, "C11")) {
        mkPath(p, sizeof(p), dir, "c11_jni.bin");
        mkPath(p2, sizeof(p2), dir, "c11_jni_ren.bin");
        int r = rename(p, p2);
        struct stat st{}; char s[65]={};
        bool ok = r == 0 && lstat(p, &st) != 0 && isPh(p2) && shaRead(p2, s) && !strcmp(s, sha1m);
        snprintf(out, sizeof(out), "%s rename=%d ph=%d sha_ok=%d",
                 ok ? "PASS" : "FAIL", r, isPh(p2), !strcmp(s, sha1m));
    } else if (!strcmp(cid, "C12")) {
        mkPath(p, sizeof(p), dir, "c12_jni.bin");
        mkPath(p2, sizeof(p2), dir, "c12_jni_priv.tmp");
        int fd = open(p2, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) { write(fd, "PRIVATE_C12_JNI_", 16); close(fd); }
        int r = rename(p2, p);
        struct stat st{}; char got[17]={};
        int f2 = open(p, O_RDONLY);
        if (f2 >= 0) { read(f2, got, 16); close(f2); }
        bool ok = r == 0 && stat(p, &st) == 0 && st.st_size == 16 && !memcmp(got, "PRIVATE_C12_JNI_", 16) && !hasTag(p);
        snprintf(out, sizeof(out), "%s rename=%d got=%s", ok ? "PASS" : "FAIL", r, got);
    } else if (!strcmp(cid, "C13")) {
        mkPath(p, sizeof(p), dir, "c13_path.bin");
        mkPath(p2, sizeof(p2), dir, "c13_fd.bin");
        struct timespec ts1[2] = {{1700000000, 0}, {1700000000, 0}};
        struct timespec ts2[2] = {{1700000123, 0}, {1700000123, 0}};
        int r1 = utimensat(AT_FDCWD, p, ts1, 0);
        int fd = open(p2, O_RDONLY);
        int r2 = fd >= 0 ? futimens(fd, ts2) : -1;
        if (fd >= 0) close(fd);
        struct stat st1{}, st2{};
        bool ok = r1 == 0 && r2 == 0 && stat(p, &st1) == 0 && stat(p2, &st2) == 0
                  && st1.st_mtime == 1700000000 && st2.st_mtime == 1700000123
                  && isPh(p) && isPh(p2);
        snprintf(out, sizeof(out), "%s path=%d(mt=%ld) fd=%d(mt=%ld)",
                 ok ? "PASS" : "FAIL", r1, (long)st1.st_mtime, r2, (long)st2.st_mtime);
    } else if (!strcmp(cid, "C14")) {
        mkPath(p, sizeof(p), dir, "c14_jni.bin");
        int r1 = setxattr(p, "user.dualapp.blob", "fake", 4, 0); int e1 = errno;
        int fd = open(p, O_RDONLY);
        int r2 = fd >= 0 ? fsetxattr(fd, "user.dualapp.blob", "fake", 4, 0) : 0; int e2 = errno;
        int r3 = setxattr(p, "user.custom", "jni_ok", 6, 0);
        int r4 = fd >= 0 ? fsetxattr(fd, "user.custom_fd", "fd_ok", 5, 0) : -1;
        if (fd >= 0) close(fd);
        char v1[32]={}, v2[32]={};
        ssize_t n1 = getxattr(p, "user.custom", v1, sizeof(v1)-1);
        ssize_t n2 = getxattr(p, "user.custom_fd", v2, sizeof(v2)-1);
        bool ok = r1 == -1 && e1 == ENOTSUP && r2 == -1 && e2 == ENOTSUP
                  && r3 == 0 && r4 == 0 && n1 == 6 && !strcmp(v1, "jni_ok")
                  && n2 == 5 && !strcmp(v2, "fd_ok") && isPh(p);
        snprintf(out, sizeof(out), "%s e1=%d e2=%d r3=%d r4=%d v1=%s v2=%s",
                 ok ? "PASS" : "FAIL", e1, e2, r3, r4, v1, v2);
    } else if (!strcmp(cid, "C15")) {
        mkPath(p, sizeof(p), dir, "c15_jni.bin");
        int fd = open(p, O_RDONLY);
        void *m1 = fd >= 0 ? mmap(nullptr, 4096, PROT_WRITE, MAP_SHARED, fd, 0) : nullptr;
        int e1 = errno;
        void *m2 = fd >= 0 ? mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0) : MAP_FAILED;
        uint8_t b0 = 0;
        if (m2 != MAP_FAILED) {
            volatile uint8_t *vp = (volatile uint8_t *)m2;
            b0 = vp[0];
            vp[0] = (uint8_t)(b0 ^ 0xFF);
            munmap(m2, 4096);
        }
        if (fd >= 0) close(fd);
        char s[65]={};
        bool ok = m1 == MAP_FAILED && e1 == EACCES && m2 != MAP_FAILED
                  && shaRead(p, s) && !strcmp(s, sha1m) && isPh(p);
        snprintf(out, sizeof(out), "%s shared=%s(e=%d) priv=%d sha_ok=%d",
                 ok ? "PASS" : "FAIL", m1 == MAP_FAILED ? "EACCES" : "ALLOWED", e1, m2 != MAP_FAILED, !strcmp(s, sha1m));
    } else if (!strcmp(cid, "C16")) {
        mkPath(p, sizeof(p), dir, "c16_jni.bin");
        int fd = open(p, O_RDONLY);
        char proc[64], target[512]={};
        snprintf(proc, sizeof(proc), "/proc/self/fd/%d", fd);
        ssize_t len = fd >= 0 ? readlink(proc, target, sizeof(target)-1) : -1;
        if (len >= 0) target[len] = 0;
        if (fd >= 0) close(fd);
        bool ok = len > 0 && !strcmp(target, p);
        snprintf(out, sizeof(out), "%s match=%d got=%s", ok ? "PASS" : "FAIL", !strcmp(target, p), target);
    } else if (!strcmp(cid, "C17")) {
        mkPath(p, sizeof(p), dir, "c17_jni.bin");
        mkPath(p2, sizeof(p2), dir, "c17_jni.hl");
        int r = link(p, p2);
        int e = errno;
        if (r == 0) unlink(p2);
        bool ok = r == -1;
        snprintf(out, sizeof(out), "%s r=%d errno=%d(%s)", ok ? "PASS" : "FAIL", r, e, strerror(e));
    } else if (!strcmp(cid, "C18")) {
        mkPath(p, sizeof(p), dir, "c18.bin");
        int fd = (int)syscall(SYS_openat, AT_FDCWD, p, O_RDONLY, 0);
        uint8_t buf[64]; memset(buf, 0xAA, sizeof(buf));
        ssize_t n = fd >= 0 ? syscall(SYS_read, fd, buf, sizeof(buf)) : -1;
        if (fd >= 0) syscall(SYS_close, fd);
        bool allZero = (n == 64);
        for (int i = 0; i < 64 && allZero; i++) if (buf[i] != 0) allZero = false;
        bool ok = allZero && isPh(p);
        snprintf(out, sizeof(out), "%s n=%zd allZero=%d ph=%d", ok ? "PASS" : "FAIL", n, allZero, isPh(p));
    } else if (!strcmp(cid, "C19")) {
        mkPath(p, sizeof(p), dir, "c19.bin");
        int fd = (int)syscall(SYS_openat, AT_FDCWD, p, O_WRONLY, 0);
        ssize_t w = fd >= 0 ? syscall(SYS_write, fd, "RAW_C19_PRIVATE!", 16) : -1;
        if (fd >= 0) syscall(SYS_close, fd);
        uint8_t buf[32]={};
        int f2 = open(p, O_RDONLY);
        ssize_t n = f2 >= 0 ? read(f2, buf, sizeof(buf)) : -1;
        if (f2 >= 0) close(f2);
        bool tailZero = (n == 32);
        for (int i = 16; i < 32 && tailZero; i++) if (buf[i] != 0) tailZero = false;
        struct stat st{}; stat(p, &st);
        bool ok = w == 16 && n == 32 && !memcmp(buf, "RAW_C19_PRIVATE!", 16) && tailZero && !isPh(p) && st.st_blocks > 8;
        snprintf(out, sizeof(out), "%s w=%zd blocks=%lld ph=%d", ok ? "PASS" : "FAIL", w, (long long)st.st_blocks, isPh(p));
    } else if (!strcmp(cid, "C20")) {
        mkPath(p, sizeof(p), dir, "c20.bin");
        mkPath(p2, sizeof(p2), dir, "c20_sym.bin");
        unlink(p2);
        int sr = symlink("c20.bin", p2);
        int se = errno;
        if (sr != 0 && (se == EPERM || se == EACCES)) {
            snprintf(out, sizeof(out), "PASS symlink=%d(%s) on external", se, strerror(se));
        } else if (sr == 0) {
            int fd = open(p2, O_RDONLY | O_NOFOLLOW);
            int oe = errno;
            if (fd >= 0) close(fd);
            char s[65]={};
            bool ok = fd == -1 && oe == ELOOP && shaRead(p2, s) && !strcmp(s, sha1m);
            snprintf(out, sizeof(out), "%s nofollow_e=%d(%s) follow_sha=%d",
                     ok ? "PASS" : "FAIL", oe, strerror(oe), !strcmp(s, sha1m));
        } else {
            snprintf(out, sizeof(out), "FAIL symlink r=%d e=%d(%s)", sr, se, strerror(se));
        }
    } else if (!strcmp(cid, "C21")) {
        mkPath(p, sizeof(p), dir, "c21.bin");
        DIR *d = opendir(dir);
        bool listed = false;
        if (d) {
            struct dirent *de;
            while ((de = readdir(d)) != nullptr) {
                if (!strcmp(de->d_name, "c21.bin")) { listed = true; break; }
            }
            closedir(d);
        }
        struct stat st{};
        bool ok = listed && stat(p, &st) == 0 && st.st_size == 1048576 && st.st_mtime == (time_t)c21Mtime && isPh(p);
        snprintf(out, sizeof(out), "%s listed=%d size=%lld mtime=%ld(exp=%ld)",
                 ok ? "PASS" : "FAIL", listed, (long long)st.st_size, (long)st.st_mtime, (long)c21Mtime);
    } else if (!strcmp(cid, "C22_CHECK")) {
        // Re-verify C2..C12 outcomes + placeholders still redirecting.
        char s1[65]={}, s1_64[65]={}, s7[65]={}, s11[65]={}, head[33]={};
        struct stat st{};
        bool ok = true;
        // Private files
        mkPath(p, sizeof(p), dir, "c02_jni.bin");
        ok = ok && stat(p, &st) == 0 && st.st_size == 1048576 + 16 && shaRead(p, s1, 1048576) && !strcmp(s1, sha1m) && !hasTag(p);
        mkPath(p, sizeof(p), dir, "c03_jni.bin");
        ok = ok && stat(p, &st) == 0 && st.st_size == 1048576 + 16 && shaRead(p, s1, 1048576) && !strcmp(s1, sha1m) && !hasTag(p);
        mkPath(p, sizeof(p), dir, "c04_jni.bin");
        int f4 = open(p, O_RDONLY); memset(head, 0, sizeof(head));
        if (f4 >= 0) { read(f4, head, 16); close(f4); }
        ok = ok && stat(p, &st) == 0 && st.st_size == 1048576 && !memcmp(head, "REOPEN_C4_WRITE!", 16) && !hasTag(p);
        mkPath(p, sizeof(p), dir, "c06_jni.bin");
        ok = ok && stat(p, &st) == 0 && st.st_size == 32 && !hasTag(p);
        mkPath(p, sizeof(p), dir, "c07_jni.bin");
        ok = ok && stat(p, &st) == 0 && st.st_size == 1000 && shaRead(p, s7) && !strcmp(s7, sha1000) && !hasTag(p);
        mkPath(p, sizeof(p), dir, "c09_jni.bin");
        ok = ok && stat(p, &st) == 0 && st.st_size == 1048576 && cmpSlices(p, ref, 0, 4096) && !hasTag(p);
        mkPath(p, sizeof(p), dir, "c10_jni.bin");
        ok = ok && lstat(p, &st) != 0;
        mkPath(p, sizeof(p), dir, "c11_jni_ren.bin");
        ok = ok && isPh(p) && shaRead(p, s11) && !strcmp(s11, sha1m);
        mkPath(p, sizeof(p), dir, "c12_jni.bin");
        ok = ok && stat(p, &st) == 0 && st.st_size == 16 && !hasTag(p);
        // Placeholders still redirecting
        mkPath(p, sizeof(p), dir, "c01_jni.bin");
        ok = ok && isPh(p) && shaRead(p, s1) && !strcmp(s1, sha1m);
        mkPath(p, sizeof(p), dir, "c01_64m.bin");
        ok = ok && isPh(p) && shaRead(p, s1_64) && !strcmp(s1_64, sha64m);
        mkPath(p, sizeof(p), dir, "c08_jni.bin");
        ok = ok && isPh(p) && shaRead(p, s1) && !strcmp(s1, sha1m);
        snprintf(out, sizeof(out), "%s c2..c12_persist_and_redirect", ok ? "PASS" : "FAIL");
    } else if (!strcmp(cid, "VERIFY_U1")) {
        DIR *d = opendir(dir);
        int checked = 0, bad = 0;
        if (d) {
            struct dirent *de;
            while ((de = readdir(d)) != nullptr) {
                if (de->d_name[0] == '.') continue;
                mkPath(p, sizeof(p), dir, de->d_name);
                char s[65]={};
                const char *exp = !strcmp(de->d_name, "c01_64m.bin") ? sha64m : sha1m;
                if (!isPh(p) || !shaRead(p, s) || strcmp(s, exp) != 0) bad++;
                else checked++;
            }
            closedir(d);
        }
        bool ok = checked >= 33 && bad == 0;
        snprintf(out, sizeof(out), "%s checked=%d bad=%d", ok ? "PASS" : "FAIL", checked, bad);
    } else {
        snprintf(out, sizeof(out), "FAIL unknown case %s", cid);
    }

    env->ReleaseStringUTFChars(jcase, cid);
    env->ReleaseStringUTFChars(jdir, dir);
    env->ReleaseStringUTFChars(jsha1m, sha1m);
    env->ReleaseStringUTFChars(jsha64m, sha64m);
    env->ReleaseStringUTFChars(jsha1000, sha1000);
    return mk(env, out);
}
