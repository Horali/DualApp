package dev.dualapp.canary;

/** Native probes used by the Canary v0.3 env page (Phase 0b T2 / T3 / T6). */
public final class EnvProbe {
    static {
        System.loadLibrary("canary");
    }

    private EnvProbe() {
    }

    /** Old API: __system_property_get. */
    public static native String oldPropGet(String name);

    /** Modern API: __system_property_find + __system_property_read_callback. */
    public static native String modernPropFind(String name);

    /** T2 step 3: hard-coded /data/data and /sdcard opens from native code. */
    public static native String escapeTest();

    /** T3 step 4: setxattr on a given path. */
    public static native String setxattrTest(String path);

    /** T6 C18: raw syscall open/read, bypassing libc hooks. */
    public static native String rawOpenRead(String path);

    /** T3 step 11: RLIMIT_NOFILE in a guest. */
    public static native String rlimitNofile();

    /** T3 step 1: statfs f_type and block size. */
    public static native String fsType(String path);

    /** T3 step 10: io_uring_setup from a guest. */
    public static native String ioUringSetup();

    /** T3 step 7: fchmod on a read-only handle, then reopen /proc/self/fd. */
    public static native String fchmodReopen(String path);

    /** T3 step 9: copy_file_range speed. */
    public static native String copyRange(String src, String dst, long size);

    /** T3 step 8: inotify on a guest folder. */
    public static native String inotifyTest(String dir);

    /** T6 conformance suite: run one case (or C22_CHECK / VERIFY_U1) from JNI/libc. */
    public static native String t6Jni(String caseId, String dir, String sha1m, String sha64m,
                                      String sha1000, long c21Mtime);
}
