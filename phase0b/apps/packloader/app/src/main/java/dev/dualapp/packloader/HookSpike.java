package dev.dualapp.packloader;

public final class HookSpike {
    static {
        System.loadLibrary("packloader");
    }

    private HookSpike() {
    }

    /** Runs the T5 step 1 hook spike and returns a human-readable report. */
    public static native String spike(String filesDir);
}
