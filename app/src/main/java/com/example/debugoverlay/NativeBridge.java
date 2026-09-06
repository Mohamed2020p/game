package com.example.debugoverlay;

/**
 * Thin static facade over the native library.
 *
 * Keeping every `native` declaration in one tiny class has two benefits:
 *  - the rest of the Java code never depends on JNI details, and
 *  - the JNINativeMethod table in native-lib.cpp registers against exactly
 *    this class ("com/example/debugoverlay/NativeBridge"), so the names are
 *    checked once, at library load, instead of per-call-site.
 *
 * Threading: safe to call from the UI thread (that's all this app does).
 */
public final class NativeBridge {

    static {
        // Loads libnative-lib.so, which runs JNI_OnLoad -> RegisterNatives.
        System.loadLibrary("native-lib");
    }

    private NativeBridge() {}   // static only

    /** Starts the embedded demo-game simulation (10 Hz tick thread). */
    public static native void startDemoGame();

    /** Stops the simulation. */
    public static native void stopDemoGame();

    /** Hand the render engine a Surface to draw the ImGui panel into. */
    public static native void setSurface(android.view.Surface surface);

    /** Tear the render thread + EGL surface down (surface going away). */
    public static native void clearSurface();

    /** Surface size changed — width/height in pixels. */
    public static native void surfaceChanged(int width, int height);

    /**
     * Forward a MotionEvent to ImGui.
     * @param action MotionEvent.ACTION_DOWN / ACTION_MOVE / ACTION_UP / ACTION_CANCEL
     * @param x, y   coordinates in surface pixels
     */
    public static native void onTouch(int action, float x, float y);

    /** Screen density (DisplayMetrics.density) — sizes fonts and the panel. */
    public static native void setDensity(float density);

    /** Push one persisted setting into the native engine. */
    public static native void setSetting(String key, boolean isFloat, float value);

    /**
     * Register the object that receives callbacks from native threads.
     * The target must expose (they are looked up by name at registration):
     *   void onNativeSettingChanged(String key, boolean isFloat, float value)
     *   void onOverlayPanelClosed()
     */
    public static native void setCallbackTarget(Object target);
}
