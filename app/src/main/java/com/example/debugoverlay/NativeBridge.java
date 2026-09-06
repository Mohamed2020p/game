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

    // =========================================================================
    // SURFACE MANAGEMENT — for the ImGui overlay
    // =========================================================================

    /** Hand the render engine a Surface to draw the ImGui panel into. */
    public static native void setSurface(android.view.Surface surface);

    /** Tear the render thread + EGL surface down (surface going away). */
    public static native void clearSurface();

    /** Surface size changed — width/height in pixels. */
    public static native void surfaceChanged(int width, int height);

    // =========================================================================
    // INPUT — forward touch events to ImGui
    // =========================================================================

    /**
     * Forward a MotionEvent to ImGui.
     * @param action MotionEvent.ACTION_DOWN / ACTION_MOVE / ACTION_UP / ACTION_CANCEL
     * @param x, y   coordinates in surface pixels
     */
    public static native void onTouch(int action, float x, float y);

    // =========================================================================
    // SCREEN DENSITY — for correct font sizing
    // =========================================================================

    /** Screen density (DisplayMetrics.density) — sizes fonts and the panel. */
    public static native void setDensity(float density);

    // =========================================================================
    // SETTINGS PERSISTENCE — push/pull settings from native
    // =========================================================================

    /** Push one persisted setting into the native engine. */
    public static native void setSetting(String key, boolean isFloat, float value);

    /**
     * Register the object that receives callbacks from native threads.
     * The target must expose (they are looked up by name at registration):
     *   void onNativeSettingChanged(String key, boolean isFloat, float value)
     *   void onOverlayPanelClosed()
     *   void onGameFound()
     */
    public static native void setCallbackTarget(Object target);

    // =========================================================================
    // GAME SCANNER — finds the target game process
    // =========================================================================

    /** Starts the background scanner that looks for io.supercent.bulldozermasters. */
    public static native void startScanner();

    /** Stops the scanner. */
    public static native void stopScanner();

    /** Force a rescan for the game process. */
    public static native void forceRescan();

    // =========================================================================
    // CHEAT CONTROL — toggles from the UI
    // =========================================================================

    /**
     * Set a boolean cheat toggle from the UI.
     * @param key   The cheat name (e.g., "unlimited_money", "max_speed")
     * @param value true = enabled, false = disabled
     */
    public static native void setCheat(String key, boolean value);

    /**
     * Set a float cheat value from the UI (e.g., speed value, damage multiplier).
     * @param key   The cheat name (e.g., "speed_value", "damage_multiplier")
     * @param value The float value to set
     */
    public static native void setCheatFloat(String key, float value);

    // =========================================================================
    // GAME STATUS — query the current state
    // =========================================================================

    /** Returns true if the game process was found and libil2cpp.so is loaded. */
    public static native boolean isGameFound();

    /** Returns the PID of the game process, or 0 if not found. */
    public static native int getGamePid();

    /** Returns the base address of libil2cpp.so in the game process, or 0. */
    public static native long getIl2CppBase();

    // =========================================================================
    // LEGACY / DEPRECATED — kept for compatibility (no longer used)
    // =========================================================================

    /**
     * @deprecated Replaced by startScanner() — this now does nothing.
     * The embedded demo game has been removed in favor of targeting the real game.
     */
    @Deprecated
    public static native void startDemoGame();

    /**
     * @deprecated Replaced by stopScanner() — this now does nothing.
     */
    @Deprecated
    public static native void stopDemoGame();
}
