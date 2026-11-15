package dev.dualapp.packloader;

import android.app.Activity;
import android.os.Bundle;
import android.util.Log;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

public class MainActivity extends Activity {
    private static final String TAG = "PACK";
    private TextView out;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        out = new TextView(this);
        out.setTextIsSelectable(true);
        out.setPadding(32, 96, 32, 32);

        ScrollView sv = new ScrollView(this);
        sv.addView(out);

        Button run = new Button(this);
        run.setText("Run hook spike");
        run.setOnClickListener(v -> {
            String r = HookSpike.spike(getFilesDir().getAbsolutePath());
            out.setText(r);
            Log.i(TAG, "SPIKE DONE");
        });

        LinearLayout ll = new LinearLayout(this);
        ll.setOrientation(LinearLayout.VERTICAL);
        ll.setPadding(0, 0, 0, 150);
        ll.addView(sv, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));
        ll.addView(run, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        setContentView(ll);
    }
}
