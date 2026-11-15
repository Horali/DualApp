package dev.dualapp.canary;

import android.system.ErrnoException;
import android.system.Os;
import android.system.OsConstants;
import android.system.StructStat;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.RandomAccessFile;

/** Canary v0.3 T3 guest-side filesystem probe. Logs to tag CANARY_T3. */
public final class T3Probe {
    private static final String TAG = "CANARY_T3";

    private T3Probe() {
    }

    public static void run(File intDir, File extDir) {
        intDir.mkdirs();
        extDir.mkdirs();
        line("intDir=" + intDir);
        line("extDir=" + extDir);
        try { fs(intDir, extDir); } catch (Throwable t) { line("fs=ERR " + t); }
        try { sparse(extDir); } catch (Throwable t) { line("sparse=ERR " + t); }
        try { small(extDir); } catch (Throwable t) { line("small=ERR " + t); }
        try { xattr(extDir); } catch (Throwable t) { line("xattr=ERR " + t); }
        try { rename(intDir, extDir); } catch (Throwable t) { line("rename=ERR " + t); }
        try { fchmod(extDir); } catch (Throwable t) { line("fchmod=ERR " + t); }
        try { copyRange(extDir); } catch (Throwable t) { line("copyRange=ERR " + t); }
        // io_uring_setup is deliberately not called here: Android's seccomp policy
        // kills the process with SIGSYS (syscall 425). The result is recorded in
        // phase0b/results/t3/t3-fs-probe.txt.
        try { inotify(extDir); } catch (Throwable t) { line("inotify=ERR " + t); }
        try { line("rlimit=" + EnvProbe.rlimitNofile()); } catch (Throwable t) { line("rlimit=ERR " + t); }
        line("T3 DONE");
    }

    private static void fs(File intDir, File extDir) throws Exception {
        line("fs.int=" + EnvProbe.fsType(intDir.getAbsolutePath()));
        line("fs.ext=" + EnvProbe.fsType(extDir.getAbsolutePath()));
        line("dev.int=" + Os.stat(intDir.getAbsolutePath()).st_dev);
        line("dev.ext=" + Os.stat(extDir.getAbsolutePath()).st_dev);
    }

    private static void sparse(File dir) throws Exception {
        File f = new File(dir, "sparse.bin");
        try (RandomAccessFile raf = new RandomAccessFile(f, "rw")) {
            raf.setLength(64L * 1024 * 1024);
        }
        StructStat st = Os.stat(f.getAbsolutePath());
        int b0, b1;
        try (RandomAccessFile raf = new RandomAccessFile(f, "r")) {
            raf.seek(0);
            b0 = raf.read();
            raf.seek(64L * 1024 * 1024 - 1);
            b1 = raf.read();
        }
        line("sparse.size=" + st.st_size + " blocks=" + st.st_blocks
                + " zeros=" + (b0 == 0 && b1 == 0));
        f.delete();
    }

    private static void small(File dir) throws Exception {
        for (int n : new int[]{1, 3, 4096, 65536}) {
            File f = new File(dir, "small-" + n);
            try (FileOutputStream fos = new FileOutputStream(f)) {
                fos.write(new byte[n]);
            }
            StructStat st = Os.stat(f.getAbsolutePath());
            line("small.size=" + n + " blocks=" + st.st_blocks);
            f.delete();
        }
    }

    private static void xattr(File dir) throws Exception {
        File f = new File(dir, "xattr.bin");
        f.createNewFile();
        try {
            Os.setxattr(f.getAbsolutePath(), "user.dualapp.blob", "test".getBytes("UTF-8"), 0);
            line("xattr.set=OK");
        } catch (ErrnoException e) {
            line("xattr.set=FAIL errno=" + e.errno);
        }
        try {
            byte[] v = Os.getxattr(f.getAbsolutePath(), "user.dualapp.blob");
            line("xattr.get=" + new String(v, "UTF-8"));
        } catch (ErrnoException e) {
            line("xattr.get=FAIL errno=" + e.errno);
        }
        File f2 = new File(dir, "xattr2.bin");
        line("xattr.rename=" + f.renameTo(f2));
        try {
            byte[] v = Os.getxattr(f2.getAbsolutePath(), "user.dualapp.blob");
            line("xattr.afterRename=" + new String(v, "UTF-8"));
        } catch (ErrnoException e) {
            line("xattr.afterRename=FAIL errno=" + e.errno);
        }
        f2.delete();
    }

    private static void rename(File intDir, File extDir) throws Exception {
        File a = new File(extDir, "r1.bin");
        a.createNewFile();
        File b = new File(extDir, "r2.bin");
        try {
            Os.rename(a.getAbsolutePath(), b.getAbsolutePath());
            line("rename.sameFs=OK");
        } catch (ErrnoException e) {
            line("rename.sameFs=FAIL errno=" + e.errno);
        }
        File c = new File(intDir, "r3.bin");
        try {
            Os.rename(b.getAbsolutePath(), c.getAbsolutePath());
            line("rename.cross=OK");
        } catch (ErrnoException e) {
            line("rename.cross=FAIL errno=" + e.errno
                    + " (" + OsConstants.errnoName(e.errno) + ")");
        }
        b.delete();
        c.delete();
    }

    private static void fchmod(File dir) throws Exception {
        File f = new File(dir, "fchmod.bin");
        f.createNewFile();
        Os.chmod(f.getAbsolutePath(), 0444);
        line("fchmodReopen=" + EnvProbe.fchmodReopen(f.getAbsolutePath()));
        f.delete();
    }

    private static void copyRange(File dir) throws Exception {
        File src = new File(dir, "src.bin");
        File dst = new File(dir, "dst.bin");
        byte[] buf = new byte[1 << 20];
        new java.util.Random(1).nextBytes(buf);
        try (FileOutputStream fos = new FileOutputStream(src)) {
            for (int i = 0; i < 64; i++) fos.write(buf);
        }
        line("copy_file_range=" + EnvProbe.copyRange(
                src.getAbsolutePath(), dst.getAbsolutePath(), src.length()));
        src.delete();
        dst.delete();
    }

    private static void inotify(File dir) throws Exception {
        File wd = new File(dir, "watch");
        wd.mkdirs();
        line("inotify=" + EnvProbe.inotifyTest(wd.getAbsolutePath()));
        new File(wd, "event").delete();
        wd.delete();
    }

    private static void line(String s) {
        Log.i(TAG, s);
    }
}
