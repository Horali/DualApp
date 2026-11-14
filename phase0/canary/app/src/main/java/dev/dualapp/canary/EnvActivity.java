package dev.dualapp.canary;

import android.app.Activity;
import android.os.Build;
import android.os.Bundle;
import android.os.Process;
import android.provider.Settings;
import android.util.Log;
import android.view.KeyEvent;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.security.MessageDigest;

/**
 * Canary v0.3 env page (Phase 0b T2 step 2).
 *
 * Shows, side by side, what different readers see:
 *   - Java Build.MODEL / Build.FINGERPRINT
 *   - old native __system_property_get
 *   - modern native __system_property_find / read_callback
 *   - getprop run in a child process
 *   - Settings.Secure.ANDROID_ID (logged as a hash only)
 *
 * The escape test (T2 step 3) is behind its own button because it writes
 * one 1-byte file in /sdcard/Download and deletes it immediately.
 */
public class EnvActivity extends Activity {
    private static final String TAG = "CANARY_ENV";

    private final StringBuilder sb = new StringBuilder();
    private TextView out;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        out = new TextView(this);
        out.setTextIsSelectable(true);
        out.setPadding(32, 96, 32, 32);

        ScrollView sv = new ScrollView(this);
        sv.addView(out);

        Button env = new Button(this);
        env.setText("Run env probe");
        env.setOnClickListener(v -> {
            sb.setLength(0);
            runEnv();
            show();
        });

        Button esc = new Button(this);
        esc.setText("Run escape test");
        esc.setOnClickListener(v -> {
            String r = EnvProbe.escapeTest();
            line("escape=" + r);
            show();
        });

        LinearLayout ll = new LinearLayout(this);
        ll.setOrientation(LinearLayout.VERTICAL);
        // targetSdk 35 draws edge-to-edge: keep the buttons above the navigation bar.
        ll.setPadding(0, 0, 0, 150);
        ll.addView(sv, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));
        ll.addView(env, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        ll.addView(esc, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        setContentView(ll);

        runEnv();
        show();
    }

    /** Test hook: pressing X runs the T2 step 3 escape test without the button. */
    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        if (keyCode == KeyEvent.KEYCODE_X) {
            line("escape=" + EnvProbe.escapeTest());
            show();
            return true;
        }
        return super.onKeyDown(keyCode, event);
    }

    private void runEnv() {
        line("java.model=" + Build.MODEL);
        line("java.fingerprint=" + Build.FINGERPRINT);
        line("java.brand=" + Build.BRAND + " device=" + Build.DEVICE
                + " sdk=" + Build.VERSION.SDK_INT);
        line("native.old.model=" + EnvProbe.oldPropGet("ro.product.model"));
        line("native.old.fingerprint=" + EnvProbe.oldPropGet("ro.build.fingerprint"));
        line("native.modern.model=" + EnvProbe.modernPropFind("ro.product.model"));
        line("native.modern.fingerprint=" + EnvProbe.modernPropFind("ro.build.fingerprint"));
        line("child.getprop.model=" + getprop("ro.product.model"));

        String id = Settings.Secure.getString(getContentResolver(), Settings.Secure.ANDROID_ID);
        line("androidId.hash=" + sha12(id) + " len=" + (id == null ? -1 : id.length()));

        line("uid=" + Process.myUid());
        line("dataDir=" + getApplicationInfo().dataDir);
        line("extFilesDir=" + getExternalFilesDir(null));
        line("rlimit.nofile=" + EnvProbe.rlimitNofile());

        Log.i(TAG, "ENV DONE");
    }

    private void line(String s) {
        sb.append(s).append('\n');
        Log.i(TAG, s);
    }

    private void show() {
        out.setText(sb.toString());
    }

    private static String getprop(String name) {
        try {
            java.lang.Process p = Runtime.getRuntime().exec(new String[]{"getprop", name});
            BufferedReader r = new BufferedReader(new InputStreamReader(p.getInputStream()));
            String s = r.readLine();
            p.waitFor();
            return s == null ? "<empty>" : s;
        } catch (Exception e) {
            return "ERR " + e;
        }
    }

    private static String sha12(String s) {
        if (s == null) return "<null>";
        try {
            MessageDigest md = MessageDigest.getInstance("SHA-256");
            byte[] d = md.digest(s.getBytes("UTF-8"));
            StringBuilder b = new StringBuilder();
            for (int i = 0; i < 6; i++) b.append(String.format("%02x", d[i]));
            return b.toString();
        } catch (Exception e) {
            return "ERR";
        }
    }
}
