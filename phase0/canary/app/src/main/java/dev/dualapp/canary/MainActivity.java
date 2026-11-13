package dev.dualapp.canary;

import android.app.Activity;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.database.sqlite.SQLiteDatabase;
import android.os.Build;
import android.os.Bundle;
import android.os.Process;
import android.util.Log;
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

        for (String l : out.toString().split("\n")) Log.i(TAG, l);
        Log.i(TAG, "SUMMARY id=" + id + " run=" + n + " consistent=" + consistent
                + " prefs=" + prefRuns + " db=" + dbRuns + " int=" + intLines + " ext=" + extLines);

        TextView tv = new TextView(this);
        tv.setTextIsSelectable(true);
        tv.setPadding(32, 96, 32, 32);
        tv.setText(out.toString());
        ScrollView sv = new ScrollView(this);
        sv.addView(tv);
        setContentView(sv);
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
