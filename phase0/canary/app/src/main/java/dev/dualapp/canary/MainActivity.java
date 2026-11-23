package dev.dualapp.canary;

import android.app.Activity;
import android.content.Intent;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.database.sqlite.SQLiteDatabase;
import android.os.Build;
import android.os.Bundle;
import android.os.Process;
import android.util.Log;
import android.view.KeyEvent;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.FileWriter;
import java.io.IOException;
import java.util.UUID;

/**
 * Phase 0 isolation canary.
 *
 * Every launch it reads what earlier launches left in four storage kinds
 * (SharedPreferences, SQLite, internal file, app-specific external file),
 * checks that they agree with each other, then appends one more record to each.
 *
 * Two instances are isolated when each keeps its own instanceId and its own
 * counters. Persistence holds when the counters keep growing across force-stop
 * and reboot. Results go to logcat (tag CANARY) and to the screen.
 *
 * The app reads only its own storage. It sends nothing over the network.
 */
public class MainActivity extends Activity {
    private static final String TAG = "CANARY";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        StringBuilder out = new StringBuilder();

        // 1. SharedPreferences
        SharedPreferences prefs = getSharedPreferences("canary", MODE_PRIVATE);
        String id = prefs.getString("instanceId", null);
        boolean firstRun = id == null;
        if (firstRun) id = UUID.randomUUID().toString().substring(0, 8);
        int prefRuns = prefs.getInt("runs", 0);

        // 2. SQLite
        SQLiteDatabase db = openOrCreateDatabase("canary.db", MODE_PRIVATE, null);
        db.execSQL("CREATE TABLE IF NOT EXISTS runs (n INTEGER, ts INTEGER, instanceId TEXT)");
        int dbRuns = 0;
        String dbIds = "";
        try (Cursor c = db.rawQuery("SELECT COUNT(*), GROUP_CONCAT(DISTINCT instanceId) FROM runs", null)) {
            if (c.moveToFirst()) { dbRuns = c.getInt(0); dbIds = c.isNull(1) ? "" : c.getString(1); }
        }

        // 3. Internal file, 4. app-specific external file
        File internal = new File(getFilesDir(), "canary.txt");
        File extDir = getExternalFilesDir(null);
        File external = extDir == null ? null : new File(extDir, "canary.txt");
        int intLines = countLines(internal);
        int extLines = external == null ? -1 : countLines(external);

        int n = prefRuns + 1;
        boolean consistent = prefRuns == dbRuns && dbRuns == intLines && intLines == extLines
                && (dbIds.isEmpty() || dbIds.equals(id));

        // Write this run
        prefs.edit().putString("instanceId", id).putInt("runs", n).commit();
        db.execSQL("INSERT INTO runs VALUES (?, ?, ?)", new Object[]{n, System.currentTimeMillis(), id});
        db.close();
        String line = id + " run " + n + " " + System.currentTimeMillis() + "\n";
        append(internal, line);
        if (external != null) append(external, line);

        out.append("instanceId=").append(id).append(firstRun ? " (new)" : "").append('\n')
           .append("run=").append(n).append('\n')
           .append("before: prefs=").append(prefRuns).append(" db=").append(dbRuns)
           .append(" int=").append(intLines).append(" ext=").append(extLines).append('\n')
           .append("dbInstanceIds=").append(dbIds).append('\n')
           .append("consistent=").append(consistent).append('\n')
           .append("pid=").append(Process.myPid()).append(" uid=").append(Process.myUid()).append('\n')
           .append("dataDir=").append(getApplicationInfo().dataDir).append('\n')
           .append("filesDir=").append(getFilesDir()).append('\n')
           .append("extFilesDir=").append(extDir).append('\n')
           .append("reported model=").append(Build.MODEL)
           .append(" sdk=").append(Build.VERSION.SDK_INT).append('\n')
           .append("reported fingerprint=").append(Build.FINGERPRINT).append('\n');

        File shared = new File(getFilesDir(), "shared.bin");
        if (shared.exists()) out.append(sharedProbe(shared));

        for (String l : out.toString().split("\n")) Log.i(TAG, l);
        Log.i(TAG, "SUMMARY id=" + id + " run=" + n + " consistent=" + consistent
                + " prefs=" + prefRuns + " db=" + dbRuns + " int=" + intLines + " ext=" + extLines);

        TextView tv = new TextView(this);
        tv.setTextIsSelectable(true);
        tv.setPadding(32, 96, 32, 32);
        tv.setText(out.toString());
        ScrollView sv = new ScrollView(this);
        sv.addView(tv);

        Button env = new Button(this);
        env.setText("Open env page (v0.3)");
        env.setOnClickListener(v -> startActivity(new Intent(this, EnvActivity.class)));

        LinearLayout ll = new LinearLayout(this);
        ll.setOrientation(LinearLayout.VERTICAL);
        // targetSdk 35 draws edge-to-edge: keep the button above the navigation bar.
        ll.setPadding(0, 0, 0, 150);
        ll.addView(sv, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));
        ll.addView(env, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        setContentView(ll);
    }

    /** Test hook: pressing E opens the env page; pressing C runs the T6 conformance case. */
    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        if (keyCode == KeyEvent.KEYCODE_E) {
            startActivity(new Intent(this, EnvActivity.class));
            return true;
        }
        if (keyCode == KeyEvent.KEYCODE_C) {
            new Thread(() -> T6Probe.runFromFiles(getFilesDir(), getExternalFilesDir(null))).start();
            return true;
        }
        return super.onKeyDown(keyCode, event);
    }

    /**
     * Phase 0 storage probe. Runs only when the test harness has placed
     * files/shared.bin (a symlink to a 0444 blob in a shared pool).
     * Checks, from inside a real guest process, whether a guest can get
     * write access to the shared blob. It restores the blob afterwards.
     */
    private static String sharedProbe(File f) {
        StringBuilder p = new StringBuilder();
        String path = f.getAbsolutePath();
        long len = f.length();
        p.append("probe.target=").append(path).append(" len=").append(len).append('\n');
        p.append("probe.hashBefore=").append(sha(f)).append('\n');
        p.append("probe.writeAsIs=").append(tryAppend(f) ? "ALLOWED" : "DENIED").append('\n');
        boolean chmod;
        try { android.system.Os.chmod(path, 0644); chmod = true; } catch (Exception e) { chmod = false; p.append("probe.chmodError=").append(e).append('\n'); }
        p.append("probe.chmod0644=").append(chmod ? "ALLOWED" : "DENIED").append('\n');
        boolean wrote = chmod && tryAppend(f);
        p.append("probe.writeAfterChmod=").append(wrote ? "ALLOWED" : "DENIED").append('\n');
        p.append("probe.hashAfter=").append(sha(f)).append('\n');
        // restore
        try {
            if (wrote) try (java.io.RandomAccessFile raf = new java.io.RandomAccessFile(f, "rw")) { raf.setLength(len); }
            android.system.Os.chmod(path, 0444);
        } catch (Exception e) { p.append("probe.restoreError=").append(e).append('\n'); }
        p.append("probe.hashRestored=").append(sha(f)).append('\n');
        return p.toString();
    }

    private static boolean tryAppend(File f) {
        try (FileWriter w = new FileWriter(f, true)) { w.write("X"); return true; } catch (IOException e) { return false; }
    }

    private static String sha(File f) {
        try (java.io.FileInputStream in = new java.io.FileInputStream(f)) {
            java.security.MessageDigest md = java.security.MessageDigest.getInstance("SHA-256");
            byte[] buf = new byte[1 << 16]; int n;
            while ((n = in.read(buf)) > 0) md.update(buf, 0, n);
            StringBuilder s = new StringBuilder();
            for (byte b : md.digest()) s.append(String.format("%02x", b));
            return s.substring(0, 16);
        } catch (Exception e) { return "ERR " + e; }
    }

    private static int countLines(File f) {
        if (!f.exists()) return 0;
        int n = 0;
        try (BufferedReader r = new BufferedReader(new FileReader(f))) {
            while (r.readLine() != null) n++;
        } catch (IOException e) {
            Log.w(TAG, "read failed " + f + ": " + e);
            return -2;
        }
        return n;
    }

    private static void append(File f, String s) {
        try (FileWriter w = new FileWriter(f, true)) {
            w.write(s);
        } catch (IOException e) {
            Log.w(TAG, "write failed " + f + ": " + e);
        }
    }
}
