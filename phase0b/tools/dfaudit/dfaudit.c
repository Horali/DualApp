// dfaudit: inotify watcher for the DualFs hook-coverage audit (Phase 0b T5).
// Usage: dfaudit <root> [seconds]   (0 or omitted = run until killed)
// Prints one line per event: <time> <masks> <path> <placeholder|private>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <ftw.h>
#include <sys/inotify.h>
#include <sys/xattr.h>
#include <limits.h>

#define TAG "user.dualapp.blob"
#define WATCH_MASK (IN_OPEN | IN_ACCESS | IN_MODIFY | IN_CLOSE_WRITE | \
                    IN_MOVED_FROM | IN_MOVED_TO | IN_DELETE | IN_CREATE)
#define MAX_WATCHES 4096

typedef struct {
    int wd;
    char path[PATH_MAX];
} watch_t;

static watch_t g_watches[MAX_WATCHES];
static int g_nwatches;
static int g_fd;

static int add_watch_cb(const char *path, const struct stat *st, int type, struct FTW *ftw) {
    (void) st;
    (void) ftw;
    if (type != FTW_D) return 0;
    if (g_nwatches >= MAX_WATCHES) {
        fprintf(stderr, "watch limit reached (%d)\n", MAX_WATCHES);
        return 1;
    }
    int wd = inotify_add_watch(g_fd, path, WATCH_MASK);
    if (wd < 0) {
        fprintf(stderr, "watch %s: %s\n", path, strerror(errno));
        return 0;
    }
    g_watches[g_nwatches].wd = wd;
    snprintf(g_watches[g_nwatches].path, PATH_MAX, "%s", path);
    g_nwatches++;
    return 0;
}

static const char *watch_path(int wd) {
    for (int i = 0; i < g_nwatches; i++)
        if (g_watches[i].wd == wd) return g_watches[i].path;
    return NULL;
}

static int is_placeholder(const char *path) {
    char buf[256];
    ssize_t n = getxattr(path, TAG, buf, sizeof(buf) - 1);
    return n > 0;
}

static void mask_str(uint32_t mask, char *out, size_t n) {
    out[0] = 0;
    struct {
        uint32_t bit;
        const char *name;
    } names[] = {
            {IN_ACCESS, "ACCESS"}, {IN_MODIFY, "MODIFY"}, {IN_OPEN, "OPEN"},
            {IN_CLOSE_WRITE, "CLOSE_WRITE"}, {IN_MOVED_FROM, "MOVED_FROM"},
            {IN_MOVED_TO, "MOVED_TO"}, {IN_DELETE, "DELETE"}, {IN_CREATE, "CREATE"},
            {IN_DELETE_SELF, "DELETE_SELF"}, {IN_MOVE_SELF, "MOVE_SELF"},
            {IN_ATTRIB, "ATTRIB"}, {0, NULL}};
    for (int i = 0; names[i].name; i++) {
        if (mask & names[i].bit) {
            if (out[0]) strncat(out, ",", n - strlen(out) - 1);
            strncat(out, names[i].name, n - strlen(out) - 1);
        }
    }
    if (!out[0]) snprintf(out, n, "0x%x", mask);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: dfaudit <root> [seconds]\n");
        return 2;
    }
    const char *root = argv[1];
    int seconds = argc > 2 ? atoi(argv[2]) : 0;

    g_fd = inotify_init1(IN_NONBLOCK);
    if (g_fd < 0) {
        perror("inotify_init1");
        return 1;
    }
    if (nftw(root, add_watch_cb, 16, FTW_PHYS) != 0) perror("nftw");
    fprintf(stderr, "watching %d dirs under %s\n", g_nwatches, root);

    time_t start = time(NULL);
    char buf[64 * 1024] __attribute__((aligned(8)));
    while (1) {
        ssize_t n = read(g_fd, buf, sizeof(buf));
        if (n > 0) {
            for (char *p = buf; p < buf + n;) {
                struct inotify_event *e = (struct inotify_event *) p;
                char full[PATH_MAX];
                const char *base = watch_path(e->wd);
                if (base) snprintf(full, sizeof(full), "%s/%s", base, e->name);
                else snprintf(full, sizeof(full), "?/%s", e->name);
                char masks[128];
                mask_str(e->mask, masks, sizeof(masks));
                printf("%ld %s %s %s\n", (long) time(NULL), masks, full,
                       is_placeholder(full) ? "placeholder" : "private");
                fflush(stdout);
                p += sizeof(struct inotify_event) + e->len;
            }
        } else if (n < 0 && errno != EAGAIN) {
            perror("read");
            break;
        }
        if (seconds > 0 && time(NULL) - start >= seconds) break;
        usleep(100000);
    }
    close(g_fd);
    return 0;
}
