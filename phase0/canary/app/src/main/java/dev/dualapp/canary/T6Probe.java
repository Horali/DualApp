package dev.dualapp.canary;

import android.os.ParcelFileDescriptor;
import android.system.ErrnoException;
import android.system.Os;
import android.system.OsConstants;
import android.system.StructStat;
import android.util.Log;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.FileReader;
import java.io.FileWriter;
import java.io.RandomAccessFile;
import java.nio.ByteBuffer;
import java.nio.channels.FileChannel;
import java.nio.channels.NonWritableChannelException;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.Arrays;
import java.util.HashMap;
import java.util.Map;

/** Canary v0.3 T6 storage conformance suite (C1-C22, Java + JNI, ext + int). */
public final class T6Probe {
    private static final String TAG = "CANARY_T6";

    private T6Probe() {
    }

    public static String runFromFiles(File intRoot, File extRoot) {
        String cmd = readText(new File(extRoot, "t6_cmd.txt"));
        if (cmd == null || cmd.trim().isEmpty()) cmd = "ALL";
        cmd = cmd.trim();

        Map<String, String> meta = readMeta(new File(extRoot, "t6_meta.txt"));
        String sha1m = meta.getOrDefault("sha1m", "");
        String sha64m = meta.getOrDefault("sha64m", "");
        String sha1000 = meta.getOrDefault("sha1m_1000", "");
        long c21ExtMtime = Long.parseLong(meta.getOrDefault("c21_ext_mtime", "0"));
        long c21IntMtime = Long.parseLong(meta.getOrDefault("c21_int_mtime", "0"));

        File extT6 = new File(extRoot, "t6");
        File intT6 = new File(intRoot, "t6");

        StringBuilder sb = new StringBuilder();
        if ("ALL".equals(cmd)) {
            for (int i = 1; i <= 21; i++) {
                String c = "C" + i;
                runCaseBothMounts(sb, c, extT6, intT6, sha1m, sha64m, sha1000, c21ExtMtime, c21IntMtime);
            }
        } else {
            runCaseBothMounts(sb, cmd, extT6, intT6, sha1m, sha64m, sha1000, c21ExtMtime, c21IntMtime);
        }

        try (FileWriter fw = new FileWriter(new File(extRoot, "t6_last.txt"), false)) {
            fw.write(sb.toString());
        } catch (Exception e) {
            line("write t6_last failed: " + e);
        }
        line("T6 DONE cmd=" + cmd);
        return sb.toString();
    }

    private static void runCaseBothMounts(StringBuilder sb, String c, File extT6, File intT6,
                                          String sha1m, String sha64m, String sha1000,
                                          long c21ExtMtime, long c21IntMtime) {
        if ("VERIFY_U1".equals(c)) {
            String re = EnvProbe.t6Jni(c, extT6.getAbsolutePath(), sha1m, sha64m, sha1000, c21ExtMtime);
            emit(sb, c + " EXT JNI " + re);
            String ri = EnvProbe.t6Jni(c, intT6.getAbsolutePath(), sha1m, sha64m, sha1000, c21IntMtime);
            emit(sb, c + " INT JNI " + ri);
            return;
        }
        String je = runJava(c, extT6, sha1m, sha64m, sha1000, c21ExtMtime);
        emit(sb, c + " EXT JAVA " + je);
        String ne = EnvProbe.t6Jni(c, extT6.getAbsolutePath(), sha1m, sha64m, sha1000, c21ExtMtime);
        emit(sb, c + " EXT JNI " + ne);

        String ji = runJava(c, intT6, sha1m, sha64m, sha1000, c21IntMtime);
        emit(sb, c + " INT JAVA " + ji);
        String ni = EnvProbe.t6Jni(c, intT6.getAbsolutePath(), sha1m, sha64m, sha1000, c21IntMtime);
        emit(sb, c + " INT JNI " + ni);
    }

    private static String runJava(String c, File dir, String sha1m, String sha64m,
                                  String sha1000, long c21Mtime) {
        try {
            switch (c) {
                case "C1": {
                    File f1 = new File(dir, "c01_java.bin");
                    File f64 = new File(dir, "c01_64m.bin");
                    String s1 = shaFile(f1, -1);
                    String s64 = shaFile(f64, -1);
                    String smap;
                    try (RandomAccessFile raf = new RandomAccessFile(f1, "r");
                         FileChannel ch = raf.getChannel()) {
                        ByteBuffer bb = ch.map(FileChannel.MapMode.READ_ONLY, 0, f1.length());
                        MessageDigest md = MessageDigest.getInstance("SHA-256");
                        byte[] buf = new byte[65536];
                        while (bb.hasRemaining()) {
                            int n = Math.min(buf.length, bb.remaining());
                            bb.get(buf, 0, n);
                            md.update(buf, 0, n);
                        }
                        smap = hex(md.digest());
                    }
                    boolean ok = sha1m.equals(s1) && sha1m.equals(smap) && sha64m.equals(s64)
                            && isPh(f1) && isPh(f64);
                    return (ok ? "PASS" : "FAIL") + " s1=" + sha1m.equals(s1)
                            + " map=" + sha1m.equals(smap) + " s64=" + sha64m.equals(s64);
                }
                case "C2": {
                    File f = new File(dir, "c02_java.bin");
                    try (FileOutputStream fos = new FileOutputStream(f, true)) {
                        fos.write("APPEND_C2_BYTES!".getBytes(StandardCharsets.UTF_8));
                    }
                    boolean ok = f.length() == 1048576L + 16 && sha1m.equals(shaFile(f, 1048576L))
                            && "APPEND_C2_BYTES!".equals(readSlice(f, 1048576L, 16)) && !hasTag(f);
                    return (ok ? "PASS" : "FAIL") + " len=" + f.length() + " tag=" + hasTag(f);
                }
                case "C3": {
                    File f = new File(dir, "c03_java.bin");
                    Os.chmod(f.getAbsolutePath(), 0644);
                    try (FileOutputStream fos = new FileOutputStream(f, true)) {
                        fos.write("APPEND_C3_BYTES!".getBytes(StandardCharsets.UTF_8));
                    }
                    boolean ok = f.length() == 1048576L + 16 && sha1m.equals(shaFile(f, 1048576L))
                            && "APPEND_C3_BYTES!".equals(readSlice(f, 1048576L, 16)) && !hasTag(f);
                    return (ok ? "PASS" : "FAIL") + " len=" + f.length() + " tag=" + hasTag(f);
                }
                case "C4": {
                    File f = new File(dir, "c04_java.bin");
                    File ref = new File(dir, "c01_java.bin");
                    try (ParcelFileDescriptor pfd = ParcelFileDescriptor.open(f, ParcelFileDescriptor.MODE_READ_ONLY)) {
                        Os.fchmod(pfd.getFileDescriptor(), 0644);
                        boolean phBefore = isPh(f);
                        try (RandomAccessFile raf = new RandomAccessFile("/proc/self/fd/" + pfd.getFd(), "rw")) {
                            raf.seek(0);
                            raf.write("REOPEN_C4_WRITE!".getBytes(StandardCharsets.UTF_8));
                        }
                        boolean ok = phBefore && f.length() == 1048576L
                                && "REOPEN_C4_WRITE!".equals(readSlice(f, 0, 16))
                                && slicesEqual(f, ref, 16, 1048576L - 16) && !hasTag(f);
                        return (ok ? "PASS" : "FAIL") + " phBefore=" + phBefore + " len=" + f.length();
                    }
                }
                case "C5": {
                    File f = new File(dir, "c05_java.bin");
                    try (ParcelFileDescriptor pfd = ParcelFileDescriptor.open(f, ParcelFileDescriptor.MODE_READ_ONLY)) {
                        Os.chmod("/proc/self/fd/" + pfd.getFd(), 0666);
                    }
                    int mode = Os.stat(f.getAbsolutePath()).st_mode & 0777;
                    boolean ok = mode == 0666 && isPh(f);
                    return (ok ? "PASS" : "FAIL") + " mode=" + Integer.toOctalString(mode) + " ph=" + isPh(f);
                }
                case "C6": {
                    File f = new File(dir, "c06_java.bin");
                    byte[] msg = "TRUNC_C6_NEW_CONTENT_32_BYTES!!_".getBytes(StandardCharsets.UTF_8);
                    try (FileOutputStream fos = new FileOutputStream(f, false)) {
                        fos.write(msg);
                    }
                    boolean ok = f.length() == 32 && "TRUNC_C6_NEW_CONTENT_32_BYTES!!_".equals(readSlice(f, 0, 32))
                            && !hasTag(f);
                    return (ok ? "PASS" : "FAIL") + " len=" + f.length() + " tag=" + hasTag(f);
                }
                case "C7": {
                    File f = new File(dir, "c07_java.bin");
                    try (RandomAccessFile raf = new RandomAccessFile(f, "rw")) {
                        raf.setLength(1000);
                    }
                    boolean ok = f.length() == 1000 && sha1000.equals(shaFile(f, -1)) && !hasTag(f);
                    return (ok ? "PASS" : "FAIL") + " len=" + f.length();
                }
                case "C8": {
                    File f = new File(dir, "c08_java.bin");
                    int err = 0;
                    try (ParcelFileDescriptor pfd = ParcelFileDescriptor.open(f, ParcelFileDescriptor.MODE_READ_ONLY)) {
                        Os.ftruncate(pfd.getFileDescriptor(), 1000);
                    } catch (ErrnoException e) {
                        err = e.errno;
                    }
                    boolean ok = err == OsConstants.EINVAL && f.length() == 1048576L && isPh(f);
                    return (ok ? "PASS" : "FAIL") + " errno=" + err + "(" + OsConstants.errnoName(err) + ")";
                }
                case "C9": {
                    File f = new File(dir, "c09_java.bin");
                    File ref = new File(dir, "c01_java.bin");
                    try (RandomAccessFile raf = new RandomAccessFile(f, "rw")) {
                        raf.seek(4096);
                        raf.write("PATCH_C9_MIDDLE!".getBytes(StandardCharsets.UTF_8));
                    }
                    boolean ok = f.length() == 1048576L
                            && "PATCH_C9_MIDDLE!".equals(readSlice(f, 4096, 16))
                            && slicesEqual(f, ref, 0, 4096)
                            && slicesEqual(f, ref, 4112, 1048576L - 4112) && !hasTag(f);
                    return (ok ? "PASS" : "FAIL") + " len=" + f.length();
                }
                case "C10": {
                    File f = new File(dir, "c10_java.bin");
                    boolean del = f.delete();
                    boolean ok = del && !f.exists();
                    return (ok ? "PASS" : "FAIL") + " del=" + del;
                }
                case "C11": {
                    File f = new File(dir, "c11_java.bin");
                    File ren = new File(dir, "c11_java_ren.bin");
                    boolean r = f.renameTo(ren);
                    boolean ok = r && !f.exists() && isPh(ren) && sha1m.equals(shaFile(ren, -1));
                    return (ok ? "PASS" : "FAIL") + " ren=" + r + " ph=" + isPh(ren);
                }
                case "C12": {
                    File f = new File(dir, "c12_java.bin");
                    File tmp = new File(dir, "c12_java_priv.tmp");
                    try (FileOutputStream fos = new FileOutputStream(tmp, false)) {
                        fos.write("PRIVATE_C12_JAVA".getBytes(StandardCharsets.UTF_8));
                    }
                    boolean r = tmp.renameTo(f);
                    boolean ok = r && f.length() == 16 && "PRIVATE_C12_JAVA".equals(readSlice(f, 0, 16))
                            && !hasTag(f);
                    return (ok ? "PASS" : "FAIL") + " ren=" + r + " len=" + f.length();
                }
                case "C13": {
                    // Java setLastModified on c13_path.bin (JNI tests exact utimensat & futimens).
                    File f = new File(dir, "c13_path.bin");
                    boolean r = f.setLastModified(1700000555L * 1000L);
                    long got = f.lastModified() / 1000L;
                    boolean ok = r && got == 1700000555L && isPh(f);
                    return (ok ? "PASS" : "FAIL") + " set=" + r + " mtime=" + got;
                }
                case "C14": {
                    File f = new File(dir, "c14_java.bin");
                    int e1 = 0;
                    try {
                        Os.setxattr(f.getAbsolutePath(), "user.dualapp.blob",
                                "fake".getBytes(StandardCharsets.UTF_8), 0);
                    } catch (ErrnoException e) {
                        e1 = e.errno;
                    }
                    Os.setxattr(f.getAbsolutePath(), "user.custom",
                            "java_ok".getBytes(StandardCharsets.UTF_8), 0);
                    String got = new String(Os.getxattr(f.getAbsolutePath(), "user.custom"),
                            StandardCharsets.UTF_8);
                    boolean ok = e1 == OsConstants.ENOTSUP && "java_ok".equals(got) && isPh(f);
                    return (ok ? "PASS" : "FAIL") + " e1=" + e1 + " got=" + got;
                }
                case "C15": {
                    File f = new File(dir, "c15_java.bin");
                    boolean sharedDenied = false;
                    boolean privHandled = false;
                    try (RandomAccessFile raf = new RandomAccessFile(f, "r");
                         FileChannel ch = raf.getChannel()) {
                        try {
                            ch.map(FileChannel.MapMode.READ_WRITE, 0, 4096);
                        } catch (NonWritableChannelException | java.io.IOException e) {
                            sharedDenied = true;
                        }
                        try {
                            ByteBuffer pb = ch.map(FileChannel.MapMode.PRIVATE, 0, 4096);
                            pb.put(0, (byte) (pb.get(0) ^ 0xFF));
                            privHandled = true;
                        } catch (NonWritableChannelException e) {
                            // Android FileChannelImpl requires "rw" even for MapMode.PRIVATE;
                            // verify READ_ONLY map succeeds on the blob handle (JNI tests raw MAP_PRIVATE).
                            ByteBuffer rb = ch.map(FileChannel.MapMode.READ_ONLY, 0, 4096);
                            privHandled = rb.remaining() == 4096;
                        }
                    }
                    boolean ok = sharedDenied && privHandled && sha1m.equals(shaFile(f, -1)) && isPh(f);
                    return (ok ? "PASS" : "FAIL") + " sharedDenied=" + sharedDenied
                            + " privHandled=" + privHandled;
                }
                case "C16": {
                    File f = new File(dir, "c16_java.bin");
                    try (ParcelFileDescriptor pfd = ParcelFileDescriptor.open(f, ParcelFileDescriptor.MODE_READ_ONLY)) {
                        String target = Os.readlink("/proc/self/fd/" + pfd.getFd());
                        boolean ok = f.getAbsolutePath().equals(target);
                        return (ok ? "PASS" : "FAIL") + " got=" + target;
                    }
                }
                case "C17": {
                    File f = new File(dir, "c17_java.bin");
                    File hl = new File(dir, "c17_java.hl");
                    int err = 0;
                    try {
                        Os.link(f.getAbsolutePath(), hl.getAbsolutePath());
                        hl.delete();
                    } catch (ErrnoException e) {
                        err = e.errno;
                    }
                    boolean ok = err != 0;
                    return (ok ? "PASS" : "FAIL") + " errno=" + err + "(" + OsConstants.errnoName(err) + ")";
                }
                case "C18": {
                    String r = EnvProbe.rawOpenRead(new File(dir, "c18.bin").getAbsolutePath());
                    boolean ok = r.contains("n=16 hex=00000000000000000000000000000000");
                    return (ok ? "PASS" : "FAIL") + " " + r;
                }
                case "C19":
                    return "PASS (delegated to JNI raw syscall write)";
                case "C20":
                    return "PASS (delegated to JNI O_NOFOLLOW)";
                case "C21": {
                    File f = new File(dir, "c21.bin");
                    String[] names = dir.list();
                    boolean listed = names != null && Arrays.asList(names).contains("c21.bin");
                    long mtime = f.lastModified() / 1000L;
                    boolean ok = listed && f.length() == 1048576L && mtime == c21Mtime && isPh(f);
                    return (ok ? "PASS" : "FAIL") + " listed=" + listed + " len=" + f.length()
                            + " mtime=" + mtime + "(exp=" + c21Mtime + ")";
                }
                case "C22_CHECK": {
                    File ref = new File(dir, "c01_java.bin");
                    boolean ok = new File(dir, "c02_java.bin").length() == 1048576L + 16
                            && new File(dir, "c03_java.bin").length() == 1048576L + 16
                            && "REOPEN_C4_WRITE!".equals(readSlice(new File(dir, "c04_java.bin"), 0, 16))
                            && new File(dir, "c06_java.bin").length() == 32
                            && new File(dir, "c07_java.bin").length() == 1000
                            && slicesEqual(new File(dir, "c09_java.bin"), ref, 0, 4096)
                            && !new File(dir, "c10_java.bin").exists()
                            && isPh(new File(dir, "c11_java_ren.bin"))
                            && sha1m.equals(shaFile(new File(dir, "c11_java_ren.bin"), -1))
                            && "PRIVATE_C12_JAVA".equals(readSlice(new File(dir, "c12_java.bin"), 0, 16))
                            && isPh(ref) && sha1m.equals(shaFile(ref, -1))
                            && isPh(new File(dir, "c01_64m.bin")) && sha64m.equals(shaFile(new File(dir, "c01_64m.bin"), -1));
                    return (ok ? "PASS" : "FAIL") + " java_persist_and_redirect";
                }
                default:
                    return "FAIL unknown case " + c;
            }
        } catch (Throwable t) {
            return "FAIL EX " + t;
        }
    }

    private static boolean hasTag(File f) {
        try {
            return Os.getxattr(f.getAbsolutePath(), "user.dualapp.blob").length > 0;
        } catch (Throwable t) {
            return false;
        }
    }

    private static boolean isPh(File f) {
        try {
            StructStat st = Os.lstat(f.getAbsolutePath());
            if (!OsConstants.S_ISREG(st.st_mode)) return false;
            if (st.st_size > 0 && st.st_blocks > 8) return false;
            return hasTag(f);
        } catch (Throwable t) {
            return false;
        }
    }

    private static String shaFile(File f, long maxBytes) throws Exception {
        MessageDigest md = MessageDigest.getInstance("SHA-256");
        try (FileInputStream in = new FileInputStream(f)) {
            byte[] buf = new byte[65536];
            long rem = maxBytes;
            while (rem != 0) {
                int want = (rem > 0 && rem < buf.length) ? (int) rem : buf.length;
                int n = in.read(buf, 0, want);
                if (n <= 0) break;
                md.update(buf, 0, n);
                if (rem > 0) rem -= n;
            }
        }
        return hex(md.digest());
    }

    private static String hex(byte[] d) {
        StringBuilder s = new StringBuilder(d.length * 2);
        for (byte b : d) s.append(String.format("%02x", b));
        return s.toString();
    }

    private static String readSlice(File f, long off, int len) throws Exception {
        byte[] buf = new byte[len];
        try (RandomAccessFile raf = new RandomAccessFile(f, "r")) {
            raf.seek(off);
            raf.readFully(buf);
        }
        return new String(buf, StandardCharsets.UTF_8);
    }

    private static boolean slicesEqual(File a, File b, long off, long len) throws Exception {
        byte[] b1 = new byte[32768], b2 = new byte[32768];
        try (RandomAccessFile r1 = new RandomAccessFile(a, "r");
             RandomAccessFile r2 = new RandomAccessFile(b, "r")) {
            r1.seek(off);
            r2.seek(off);
            long rem = len;
            while (rem > 0) {
                int want = (int) Math.min(b1.length, rem);
                r1.readFully(b1, 0, want);
                r2.readFully(b2, 0, want);
                for (int i = 0; i < want; i++) if (b1[i] != b2[i]) return false;
                rem -= want;
            }
        }
        return true;
    }

    private static String readText(File f) {
        if (!f.exists()) return null;
        try (BufferedReader br = new BufferedReader(new FileReader(f))) {
            StringBuilder s = new StringBuilder();
            String l;
            while ((l = br.readLine()) != null) s.append(l).append('\n');
            return s.toString();
        } catch (Exception e) {
            return null;
        }
    }

    private static Map<String, String> readMeta(File f) {
        Map<String, String> m = new HashMap<>();
        String txt = readText(f);
        if (txt == null) return m;
        for (String l : txt.split("\n")) {
            int eq = l.indexOf('=');
            if (eq > 0) m.put(l.substring(0, eq).trim(), l.substring(eq + 1).trim());
        }
        return m;
    }

    private static void emit(StringBuilder sb, String s) {
        sb.append(s).append('\n');
        line(s);
    }

    private static void line(String s) {
        Log.i(TAG, s);
    }
}
