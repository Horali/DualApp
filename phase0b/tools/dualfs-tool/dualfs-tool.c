// dualfs-tool: maintenance for the DualFs M8 prototype (Phase 0b T5).
// Static-ish arm64 C, run under the engine UID with `run-as`.
//
// Commands:
//   harvest <config>            hash files, publish blobs, create placeholders
//   seed <config> <manifest>    create placeholders from <sha> <size> <relpath>
//   rehydrate <config>          turn placeholders back into full private files
//   verify-tree <config>        check every placeholder against its blob
//   scrub <config>              hash blobs, quarantine mismatches
//   stats <config>              count placeholders and blobs
//   cleanup <config>            delete tmp/ orphans
//   recover <config>            cleanup + replay the harvest journal
//
// Config keys: pool, root, tmp, locks, pkg, user, minbytes, include=, exclude=
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <ftw.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include <sys/syscall.h>
#include <limits.h>

#define TAG_TAG "user.dualapp.blob"
#define DEFAULT_MINBYTES 65536L

typedef struct {
    char **v;
    size_t n, cap;
} strlist;

static void sl_init(strlist *l) { l->v = NULL; l->n = l->cap = 0; }
static void sl_add(strlist *l, const char *s) {
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 64;
        l->v = realloc(l->v, l->cap * sizeof(char *));
    }
    l->v[l->n++] = strdup(s);
}
static void sl_free(strlist *l) {
    for (size_t i = 0; i < l->n; i++) free(l->v[i]);
    free(l->v);
    sl_init(l);
}

typedef struct {
    char pool[PATH_MAX];
    char root[PATH_MAX];
    char tmp[PATH_MAX];
    char locks[PATH_MAX];
    char pkg[256];
    int user;
    long minbytes;
    strlist includes;
    strlist excludes;
} cfg_t;

static char *trim(char *s) {
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
    return s;
}

static int load_cfg(const char *path, cfg_t *c) {
    memset(c, 0, sizeof(*c));
    c->minbytes = DEFAULT_MINBYTES;
    sl_init(&c->includes);
    sl_init(&c->excludes);
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); return -1; }
    char line[PATH_MAX + 64];
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = trim(line);
        char *v = trim(eq + 1);
        if (!strcmp(k, "pool")) snprintf(c->pool, sizeof(c->pool), "%s", v);
        else if (!strcmp(k, "root")) snprintf(c->root, sizeof(c->root), "%s", v);
        else if (!strcmp(k, "tmp")) snprintf(c->tmp, sizeof(c->tmp), "%s", v);
        else if (!strcmp(k, "locks")) snprintf(c->locks, sizeof(c->locks), "%s", v);
        else if (!strcmp(k, "pkg")) snprintf(c->pkg, sizeof(c->pkg), "%s", v);
        else if (!strcmp(k, "user")) c->user = atoi(v);
        else if (!strcmp(k, "minbytes")) c->minbytes = atol(v);
        else if (!strcmp(k, "include")) sl_add(&c->includes, v);
        else if (!strcmp(k, "exclude")) sl_add(&c->excludes, v);
    }
    fclose(f);
    if (!c->pool[0] || !c->root[0]) {
        fprintf(stderr, "config needs pool= and root=\n");
        return -1;
    }
    if (!c->tmp[0]) snprintf(c->tmp, sizeof(c->tmp), "%s/tmp", c->pool);
    if (!c->locks[0]) snprintf(c->locks, sizeof(c->locks), "%s/locks", c->pool);
    return 0;
}

static void mkdirs(const char *path) {
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
    mkdir(tmp, 0777);
}

static int excluded(const cfg_t *c, const char *rel) {
    if (strstr(rel, "login_data/")) return 1;
    const char *base = strrchr(rel, '/');
    base = base ? base + 1 : rel;
    static const char *ident[] = {
            ".DNA_", ".DNA_FLAG", ".gp_uuid", "INSTALLATION", "AdjustAttribution",
            "__app_uuid__", "snssdk_openudid.xml", "DeviceSettings.xml", "Cdid.xml", NULL};
    for (int i = 0; ident[i]; i++) if (!strcmp(base, ident[i])) return 1;
    for (size_t i = 0; i < c->excludes.n; i++)
        if (strstr(rel, c->excludes.v[i])) return 1;
    return 0;
}

static int included(const cfg_t *c, const char *rel) {
    if (c->includes.n == 0) return 1;
    for (size_t i = 0; i < c->includes.n; i++)
        if (strstr(rel, c->includes.v[i])) return 1;
    return 0;
}

// ---------- SHA-256 ----------
typedef struct {
    uint32_t h[8];
    uint64_t len;
    unsigned char buf[64];
    size_t n;
} sha256_ctx;

static const uint32_t K256[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

static uint32_t ror32(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

static void sha256_block(sha256_ctx *c, const unsigned char *p) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t) p[i * 4] << 24) | ((uint32_t) p[i * 4 + 1] << 16) |
               ((uint32_t) p[i * 4 + 2] << 8) | p[i * 4 + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ror32(w[i - 15], 7) ^ ror32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ror32(w[i - 2], 17) ^ ror32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = c->h[0], b = c->h[1], cc = c->h[2], d = c->h[3];
    uint32_t e = c->h[4], f = c->h[5], g = c->h[6], h = c->h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t S1 = ror32(e, 6) ^ ror32(e, 11) ^ ror32(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t t1 = h + S1 + ch + K256[i] + w[i];
        uint32_t S0 = ror32(a, 2) ^ ror32(a, 13) ^ ror32(a, 22);
        uint32_t maj = (a & b) ^ (a & cc) ^ (b & cc);
        uint32_t t2 = S0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = cc; cc = b; b = a; a = t1 + t2;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d;
    c->h[4] += e; c->h[5] += f; c->h[6] += g; c->h[7] += h;
}

static void sha256_init(sha256_ctx *c) {
    c->h[0] = 0x6a09e667; c->h[1] = 0xbb67ae85; c->h[2] = 0x3c6ef372; c->h[3] = 0xa54ff53a;
    c->h[4] = 0x510e527f; c->h[5] = 0x9b05688c; c->h[6] = 0x1f83d9ab; c->h[7] = 0x5be0cd19;
    c->len = 0;
    c->n = 0;
}

static void sha256_update(sha256_ctx *c, const void *data, size_t len) {
    const unsigned char *p = data;
    c->len += len;
    while (len) {
        size_t take = 64 - c->n;
        if (take > len) take = len;
        memcpy(c->buf + c->n, p, take);
        c->n += take;
        p += take;
        len -= take;
        if (c->n == 64) { sha256_block(c, c->buf); c->n = 0; }
    }
}

static void sha256_final(sha256_ctx *c, unsigned char out[32]) {
    uint64_t bits = c->len * 8;
    size_t i = c->n;
    c->buf[i++] = 0x80;
    if (i > 56) {
        while (i < 64) c->buf[i++] = 0;
        sha256_block(c, c->buf);
        i = 0;
    }
    while (i < 56) c->buf[i++] = 0;
    for (int j = 0; j < 8; j++) c->buf[56 + j] = (unsigned char) (bits >> (56 - j * 8));
    sha256_block(c, c->buf);
    for (int j = 0; j < 8; j++) {
        out[j * 4] = (unsigned char) (c->h[j] >> 24);
        out[j * 4 + 1] = (unsigned char) (c->h[j] >> 16);
        out[j * 4 + 2] = (unsigned char) (c->h[j] >> 8);
        out[j * 4 + 3] = (unsigned char) c->h[j];
    }
}

static int hash_file(const char *path, char out[65]) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    sha256_ctx c;
    sha256_init(&c);
    unsigned char buf[1 << 16];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) sha256_update(&c, buf, (size_t) n);
    close(fd);
    if (n < 0) return -1;
    unsigned char d[32];
    sha256_final(&c, d);
    for (int i = 0; i < 32; i++) sprintf(out + i * 2, "%02x", d[i]);
    out[64] = 0;
    return 0;
}

// ---------- placeholder helpers ----------
static void blob_path(const cfg_t *c, const char *sha, char *out, size_t n) {
    snprintf(out, n, "%s/blobs/%c%c/%s", c->pool, sha[0], sha[1], sha);
}

static int read_tag(const char *path, char *sha, size_t n, off_t *size) {
    char buf[256];
    ssize_t r = getxattr(path, TAG_TAG, buf, sizeof(buf) - 1);
    if (r <= 0) return -1;
    buf[r] = 0;
    char *c = strchr(buf, ':');
    if (!c) return -1;
    *c = 0;
    snprintf(sha, n, "%s", buf);
    *size = strtoll(c + 1, NULL, 10);
    return 0;
}

static int is_placeholder(const char *path, char *sha, size_t n, off_t *size) {
    struct stat st;
    if (lstat(path, &st) != 0) return 0;
    if (!S_ISREG(st.st_mode)) return 0;
    // Allow the one 4 KiB inode block the tag costs; a real write allocates >= size/2.
    if (st.st_size > 0 && (long long) st.st_blocks * 512 >= st.st_size / 2) return 0;
    if (read_tag(path, sha, n, size) != 0) return 0;
    return strlen(sha) == 64 && *size == st.st_size;
}

static int create_placeholder(const char *path, const char *sha, off_t size,
                              mode_t mode, const struct timespec ts[2]) {
    int fd = open(path, O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, mode);
    if (fd < 0) {
        fprintf(stderr, "create open %s: %s\n", path, strerror(errno));
        return -1;
    }
    if (ftruncate(fd, size) != 0) {
        fprintf(stderr, "create truncate %s: %s\n", path, strerror(errno));
        close(fd);
        unlink(path);
        return -1;
    }
    if (fchmod(fd, mode) != 0) {
        fprintf(stderr, "create chmod %s: %s\n", path, strerror(errno));
    }
    if (futimens(fd, ts) != 0) {
        fprintf(stderr, "create utimens %s: %s\n", path, strerror(errno));
    }
    close(fd);
    // Path-based setxattr: FUSE on this phone rejects fsetxattr on an fd.
    char tag[160];
    int n = snprintf(tag, sizeof(tag), "%s:%lld:1", sha, (long long) size);
    if (setxattr(path, TAG_TAG, tag, (size_t) n, 0) != 0) {
        fprintf(stderr, "create xattr %s: %s\n", path, strerror(errno));
        unlink(path);
        return -1;
    }
    return 0;
}

static void journal_append(const cfg_t *c, const char *sha, off_t size, const char *rel) {
    char dir[PATH_MAX];
    snprintf(dir, sizeof(dir), "%s/journal", c->pool);
    mkdirs(dir);
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/harvest.log", dir);
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    if (fd < 0) return;
    char line[PATH_MAX + 160];
    int n = snprintf(line, sizeof(line), "H %s %lld %s\n", sha, (long long) size, rel);
    if (write(fd, line, (size_t) n) != n) { /* ignore */ }
    fsync(fd);
    close(fd);
}

// ---------- walk ----------
static cfg_t *g_cfg;
static strlist g_files;

static int collect_cb(const char *path, const struct stat *st, int type, struct FTW *ftw) {
    (void) ftw;
    if (type != FTW_F || !S_ISREG(st->st_mode)) return 0;
    if (st->st_size < g_cfg->minbytes) return 0;
    const char *rel = path + strlen(g_cfg->root);
    if (*rel == '/') rel++;
    if (excluded(g_cfg, rel) || !included(g_cfg, rel)) return 0;
    sl_add(&g_files, path);
    return 0;
}

static int collect_placeholders_cb(const char *path, const struct stat *st, int type, struct FTW *ftw) {
    (void) ftw;
    if (type != FTW_F || !S_ISREG(st->st_mode)) return 0;
    char sha[80];
    off_t size;
    if (is_placeholder(path, sha, sizeof(sha), &size)) sl_add(&g_files, path);
    return 0;
}

static int collect_blobs_cb(const char *path, const struct stat *st, int type, struct FTW *ftw) {
    (void) ftw;
    if (type == FTW_F && S_ISREG(st->st_mode)) sl_add(&g_files, path);
    return 0;
}

// ---------- commands ----------
static int cmd_harvest(cfg_t *c) {
    g_cfg = c;
    sl_init(&g_files);
    if (nftw(c->root, collect_cb, 16, FTW_PHYS) != 0) perror("nftw");
    mkdirs(c->pool);
    char d[PATH_MAX];
    snprintf(d, sizeof(d), "%s/blobs", c->pool); mkdirs(d);
    snprintf(d, sizeof(d), "%s/staging", c->pool); mkdirs(d);
    snprintf(d, sizeof(d), "%s/journal", c->pool); mkdirs(d);
    snprintf(d, sizeof(d), "%s/quarantine", c->pool); mkdirs(d);
    mkdirs(c->tmp);

    int ok = 0, err = 0;
    for (size_t i = 0; i < g_files.n; i++) {
        const char *path = g_files.v[i];
        struct stat st;
        if (lstat(path, &st) != 0) { err++; continue; }
        char sha[65];
        if (hash_file(path, sha) != 0) { err++; continue; }
        char blob[PATH_MAX];
        blob_path(c, sha, blob, sizeof(blob));
        const char *rel = path + strlen(c->root);
        if (*rel == '/') rel++;
        if (access(blob, F_OK) != 0) {
            char stage[PATH_MAX];
            snprintf(stage, sizeof(stage), "%s/staging/%s", c->pool, sha);
            if (rename(path, stage) != 0) { err++; continue; }
            chmod(stage, 0444);
            char bdir[PATH_MAX];
            snprintf(bdir, sizeof(bdir), "%s/blobs/%c%c", c->pool, sha[0], sha[1]);
            mkdirs(bdir);
            if (rename(stage, blob) != 0) { err++; continue; }
        } else {
            unlink(path);
        }
        struct timespec ts[2] = { st.st_atim, st.st_mtim };
        if (create_placeholder(path, sha, st.st_size, st.st_mode & 07777, ts) != 0) {
            err++;
            continue;
        }
        journal_append(c, sha, st.st_size, rel);
        ok++;
    }
    printf("harvest files=%zu ok=%d err=%d\n", g_files.n, ok, err);
    sl_free(&g_files);
    return err ? 1 : 0;
}

static int cmd_seed(cfg_t *c, const char *manifest) {
    FILE *f = fopen(manifest, "r");
    if (!f) { perror(manifest); return 1; }
    char line[PATH_MAX + 200];
    int ok = 0, skip = 0, err = 0;
    while (fgets(line, sizeof(line), f)) {
        char sha[65];
        long long size;
        char rel[PATH_MAX];
        if (sscanf(line, "%64s %lld %4095[^\n]", sha, &size, rel) != 3) continue;
        if (excluded(c, rel)) { skip++; continue; }
        char blob[PATH_MAX];
        blob_path(c, sha, blob, sizeof(blob));
        if (access(blob, F_OK) != 0) { skip++; continue; }
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", c->root, rel);
        char dir[PATH_MAX];
        snprintf(dir, sizeof(dir), "%s", path);
        char *slash = strrchr(dir, '/');
        if (slash) { *slash = 0; mkdirs(dir); }
        struct stat st;
        if (lstat(path, &st) == 0) { skip++; continue; }
        struct timespec ts[2];
        clock_gettime(CLOCK_REALTIME, &ts[0]);
        ts[1] = ts[0];
        if (create_placeholder(path, sha, (off_t) size, 0644, ts) != 0) { err++; continue; }
        ok++;
    }
    fclose(f);
    printf("seed ok=%d skip=%d err=%d\n", ok, skip, err);
    return err ? 1 : 0;
}

static int copy_blob_to_private(const char *blob, const char *path, mode_t mode,
                                const struct timespec ts[2]) {
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s/rehydrate-%d-%ld", g_cfg->tmp, (int) getpid(), (long) time(NULL));
    int sfd = open(blob, O_RDONLY | O_CLOEXEC);
    int dfd = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, mode);
    if (sfd < 0 || dfd < 0) {
        if (sfd >= 0) close(sfd);
        if (dfd >= 0) close(dfd);
        unlink(tmp);
        return -1;
    }
    struct stat st;
    fstat(sfd, &st);
    off_t off = 0;
    while (off < st.st_size) {
        long n = syscall(__NR_copy_file_range, sfd, &off, dfd, NULL,
                         (size_t) (st.st_size - off), 0);
        if (n <= 0) break;
    }
    if (off < st.st_size) {
        lseek(sfd, 0, SEEK_SET);
        lseek(dfd, 0, SEEK_SET);
        char buf[1 << 20];
        ssize_t n;
        while ((n = read(sfd, buf, sizeof(buf))) > 0) {
            ssize_t w = 0;
            while (w < n) {
                ssize_t k = write(dfd, buf + w, (size_t) (n - w));
                if (k <= 0) { close(sfd); close(dfd); unlink(tmp); return -1; }
                w += k;
            }
        }
    }
    fsync(dfd);
    futimens(dfd, ts);
    close(sfd);
    close(dfd);
    if (rename(tmp, path) != 0) { unlink(tmp); return -1; }
    return 0;
}

static int cmd_rehydrate(cfg_t *c) {
    g_cfg = c;
    sl_init(&g_files);
    if (nftw(c->root, collect_placeholders_cb, 16, FTW_PHYS) != 0) perror("nftw");
    mkdirs(c->tmp);
    int ok = 0, err = 0;
    for (size_t i = 0; i < g_files.n; i++) {
        const char *path = g_files.v[i];
        char sha[80];
        off_t size;
        if (!is_placeholder(path, sha, sizeof(sha), &size)) continue;
        char blob[PATH_MAX];
        blob_path(c, sha, blob, sizeof(blob));
        struct stat st;
        if (lstat(path, &st) != 0) { err++; continue; }
        struct timespec ts[2] = { st.st_atim, st.st_mtim };
        if (copy_blob_to_private(blob, path, st.st_mode & 07777, ts) != 0) { err++; continue; }
        ok++;
    }
    printf("rehydrate placeholders=%zu ok=%d err=%d\n", g_files.n, ok, err);
    sl_free(&g_files);
    return err ? 1 : 0;
}

static int cmd_verify(cfg_t *c) {
    g_cfg = c;
    sl_init(&g_files);
    if (nftw(c->root, collect_placeholders_cb, 16, FTW_PHYS) != 0) perror("nftw");
    int good = 0, bad = 0;
    for (size_t i = 0; i < g_files.n; i++) {
        const char *path = g_files.v[i];
        char sha[80];
        off_t size;
        if (!is_placeholder(path, sha, sizeof(sha), &size)) continue;
        char blob[PATH_MAX];
        blob_path(c, sha, blob, sizeof(blob));
        struct stat st;
        if (lstat(blob, &st) != 0 || st.st_size != size) {
            printf("BAD %s blob_missing_or_size\n", path);
            bad++;
        } else {
            good++;
        }
    }
    printf("verify placeholders=%zu good=%d bad=%d\n", g_files.n, good, bad);
    sl_free(&g_files);
    return bad ? 1 : 0;
}

static int cmd_scrub(cfg_t *c) {
    g_cfg = c;
    sl_init(&g_files);
    char blobs[PATH_MAX];
    snprintf(blobs, sizeof(blobs), "%s/blobs", c->pool);
    if (nftw(blobs, collect_blobs_cb, 16, FTW_PHYS) != 0) perror("nftw");
    char q[PATH_MAX];
    snprintf(q, sizeof(q), "%s/quarantine", c->pool);
    mkdirs(q);
    int ok = 0, bad = 0;
    for (size_t i = 0; i < g_files.n; i++) {
        const char *path = g_files.v[i];
        char sha[65];
        if (hash_file(path, sha) != 0) { bad++; continue; }
        const char *base = strrchr(path, '/');
        base = base ? base + 1 : path;
        if (strcmp(sha, base) != 0) {
            char dst[PATH_MAX];
            snprintf(dst, sizeof(dst), "%s/%s.%ld", q, base, (long) time(NULL));
            rename(path, dst);
            printf("QUARANTINE %s\n", path);
            bad++;
        } else {
            ok++;
        }
    }
    printf("scrub blobs=%zu ok=%d bad=%d\n", g_files.n, ok, bad);
    sl_free(&g_files);
    return bad ? 1 : 0;
}

static int cmd_stats(cfg_t *c) {
    g_cfg = c;
    sl_init(&g_files);
    if (nftw(c->root, collect_placeholders_cb, 16, FTW_PHYS) != 0) perror("nftw");
    long long logical = 0;
    for (size_t i = 0; i < g_files.n; i++) {
        struct stat st;
        if (lstat(g_files.v[i], &st) == 0) logical += st.st_size;
    }
    printf("placeholders=%zu logical_bytes=%lld\n", g_files.n, logical);
    sl_free(&g_files);

    sl_init(&g_files);
    char blobs[PATH_MAX];
    snprintf(blobs, sizeof(blobs), "%s/blobs", c->pool);
    if (nftw(blobs, collect_blobs_cb, 16, FTW_PHYS) != 0) perror("nftw");
    long long bytes = 0;
    for (size_t i = 0; i < g_files.n; i++) {
        struct stat st;
        if (lstat(g_files.v[i], &st) == 0) bytes += st.st_size;
    }
    printf("blobs=%zu blob_bytes=%lld\n", g_files.n, bytes);
    sl_free(&g_files);
    return 0;
}

static int cmd_cleanup(cfg_t *c) {
    DIR *d = opendir(c->tmp);
    if (!d) { printf("cleanup tmp_missing\n"); return 0; }
    struct dirent *e;
    int n = 0;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char p[PATH_MAX];
        snprintf(p, sizeof(p), "%s/%s", c->tmp, e->d_name);
        if (unlink(p) == 0) n++;
    }
    closedir(d);
    printf("cleanup removed=%d\n", n);
    return 0;
}

static int cmd_recover(cfg_t *c) {
    cmd_cleanup(c);
    char j[PATH_MAX];
    snprintf(j, sizeof(j), "%s/journal/harvest.log", c->pool);
    FILE *f = fopen(j, "r");
    if (!f) { printf("recover journal_missing\n"); return 0; }
    char line[PATH_MAX + 200];
    int ok = 0, skip = 0;
    while (fgets(line, sizeof(line), f)) {
        char tag[8], sha[65], rel[PATH_MAX];
        long long size;
        if (sscanf(line, "%7s %64s %lld %4095[^\n]", tag, sha, &size, rel) != 4) continue;
        if (strcmp(tag, "H") != 0) continue;
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", c->root, rel);
        struct stat st;
        if (lstat(path, &st) == 0) { skip++; continue; }
        char blob[PATH_MAX];
        blob_path(c, sha, blob, sizeof(blob));
        if (access(blob, F_OK) != 0) { skip++; continue; }
        char dir[PATH_MAX];
        snprintf(dir, sizeof(dir), "%s", path);
        char *slash = strrchr(dir, '/');
        if (slash) { *slash = 0; mkdirs(dir); }
        struct timespec ts[2];
        clock_gettime(CLOCK_REALTIME, &ts[0]);
        ts[1] = ts[0];
        if (create_placeholder(path, sha, (off_t) size, 0644, ts) == 0) ok++;
    }
    fclose(f);
    printf("recover recreated=%d skipped=%d\n", ok, skip);
    return 0;
}

static void usage(void) {
    fprintf(stderr,
            "usage: dualfs-tool <harvest|seed|rehydrate|verify-tree|scrub|stats|cleanup|recover> <config> [manifest]\n");
}

int main(int argc, char **argv) {
    if (argc < 3) { usage(); return 2; }
    const char *cmd = argv[1];
    cfg_t c;
    if (load_cfg(argv[2], &c) != 0) return 2;

    if (!strcmp(cmd, "harvest")) return cmd_harvest(&c);
    if (!strcmp(cmd, "seed")) {
        if (argc < 4) { usage(); return 2; }
        return cmd_seed(&c, argv[3]);
    }
    if (!strcmp(cmd, "rehydrate")) return cmd_rehydrate(&c);
    if (!strcmp(cmd, "verify-tree")) return cmd_verify(&c);
    if (!strcmp(cmd, "scrub")) return cmd_scrub(&c);
    if (!strcmp(cmd, "stats")) return cmd_stats(&c);
    if (!strcmp(cmd, "cleanup")) return cmd_cleanup(&c);
    if (!strcmp(cmd, "recover")) return cmd_recover(&c);
    usage();
    return 2;
}
