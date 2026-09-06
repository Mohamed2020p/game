package com.example.debugoverlay;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.PixelFormat;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.provider.Settings;
import android.util.Log;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.WindowManager;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;

/**
 * Foreground service that owns the overlay UI:
 *
 *   [1] a 30dp floating pickaxe button, draggable anywhere, tap = open/close
 *   [2] a full-screen translucent SurfaceView when the panel is open; every
 *       touch on it is forwarded to the native ImGui renderer
 *   [3] settings persistence: native UI changes arrive via
 *       onNativeSettingChanged(...) and are serialized to settings.json
 *   [4] auto-scans for the game process (io.supercent.bulldozermasters)
 *
 * Everything the panel draws is produced by libnative-lib.so rendering into
 * the SurfaceView's Surface through EGL + OpenGL ES 3.
 */
public class DebugService extends Service {

    private static final String TAG = "DebugService";
    private static final String CHANNEL_ID = "debug_overlay_demo";
    private static final int NOTIFICATION_ID = 42;
    private static final String SETTINGS_FILE = "settings.json";

    /** Simple flag MainActivity reads to label its start/stop button. */
    public static volatile boolean isRunning = false;

    private WindowManager wm;
    private final Handler main = new Handler(Looper.getMainLooper());

    private View button;
    private WindowManager.LayoutParams buttonParams;
    private SurfaceView panel;
    private WindowManager.LayoutParams panelParams;

    private float density = 2.0f;
    private final JSONObject settingsState = new JSONObject();
    private final Runnable saveRunnable = this::saveSettingsNow;

    // ---- Game status ----
    private boolean gameFound = false;
    private int gamePid = 0;
    private long il2cppBase = 0;

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    @Override
    public void onCreate() {
        super.onCreate();
        isRunning = true;
        wm = (WindowManager) getSystemService(WINDOW_SERVICE);
        density = getResources().getDisplayMetrics().density;

        goForeground();

        if (!Settings.canDrawOverlays(this)) {
            Log.w(TAG, "overlay permission missing — stopping");
            stopSelf();
            return;
        }

        // Callbacks must exist before settings load, because loading pushes
        // values INTO native (no callbacks yet), while later UI changes call
        // back OUT into onNativeSettingChanged.
        NativeBridge.setCallbackTarget(this);
        NativeBridge.setDensity(density);
        loadSettings();

        // ---- Start the scanner (targets the REAL game) ----
        NativeBridge.startScanner();

        showFloatingButton();
        Log.i(TAG, "DebugService started, scanning for io.supercent.bulldozermasters");
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent != null && "stop".equals(intent.getAction())) {
            stopSelf();
        }
        return START_NOT_STICKY;
    }

    @Override
    public void onDestroy() {
        removePanel();
        if (button != null && button.getParent() instanceof WindowManager) {
            wm.removeView(button);
            button = null;
        }
        NativeBridge.clearSurface();
        NativeBridge.stopScanner();
        main.removeCallbacks(saveRunnable);
        saveSettingsNow();
        isRunning = false;
        Log.i(TAG, "DebugService destroyed");
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;   // started service, not bound
    }

    /** Foreground notification — required within seconds of startForegroundService. */
    private void goForeground() {
        NotificationManager nm = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
        if (Build.VERSION.SDK_INT >= 26) {
            NotificationChannel ch = new NotificationChannel(CHANNEL_ID,
                    "Debug overlay", NotificationManager.IMPORTANCE_LOW);
            ch.setDescription("Keeps the BulldozerMaster debug overlay alive");
            nm.createNotificationChannel(ch);
        }
        Notification.Builder b = Build.VERSION.SDK_INT >= 26
                ? new Notification.Builder(this, CHANNEL_ID)
                : new Notification.Builder(this);
        Notification notif = b
                .setContentTitle("Debug overlay — BulldozerMaster")
                .setContentText("Educational debug overlay is active")
                .setSmallIcon(android.R.drawable.ic_menu_info_details)
                .setOngoing(true)
                .build();

        if (Build.VERSION.SDK_INT >= 34) {
            startForeground(NOTIFICATION_ID, notif,
                    ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE);
        } else {
            startForeground(NOTIFICATION_ID, notif);
        }
    }

    // -------------------------------------------------------------------------
    // [1] The 30dp floating pickaxe button
    // -------------------------------------------------------------------------

    private void showFloatingButton() {
        final int sizePx = (int) (30 * density);

        button = new PickaxeView(this);

        buttonParams = new WindowManager.LayoutParams(
                sizePx, sizePx,
                overlayWindowType(),
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                        | WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL
                        | WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS,
                PixelFormat.TRANSLUCENT);
        buttonParams.gravity = Gravity.TOP | Gravity.START;
        buttonParams.x = (int) (24 * density);
        buttonParams.y = (int) (320 * density);

        final int touchSlop = android.view.ViewConfiguration.get(this).getScaledTouchSlop();

        button.setOnTouchListener((v, ev) -> {
            switch (ev.getActionMasked()) {
                case MotionEvent.ACTION_DOWN: {
                    v.setTag(new float[]{ev.getRawX(), ev.getRawY(),
                            buttonParams.x, buttonParams.y, 0f /*moved*/});
                    return true;
                }
                case MotionEvent.ACTION_MOVE: {
                    float[] s = (float[]) v.getTag();
                    float dx = ev.getRawX() - s[0], dy = ev.getRawY() - s[1];
                    if (Math.abs(dx) > touchSlop || Math.abs(dy) > touchSlop) s[4] = 1f;
                    buttonParams.x = clamp((int) (s[2] + dx), 0,
                            screenWidth() - buttonParams.width);
                    buttonParams.y = clamp((int) (s[3] + dy), 0,
                            screenHeight() - buttonParams.height);
                    try {
                        wm.updateViewLayout(button, buttonParams);
                    } catch (IllegalArgumentException ignored) { }
                    return true;
                }
                case MotionEvent.ACTION_UP: {
                    float[] s = (float[]) v.getTag();
                    if (s[4] == 0f) togglePanel();   // a tap, not a drag
                    return true;
                }
                default:
                    return false;
            }
        });

        wm.addView(button, buttonParams);
    }

    private int overlayWindowType() {
        return Build.VERSION.SDK_INT >= 26
                ? WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
                : WindowManager.LayoutParams.TYPE_PHONE;
    }

    private static int clamp(int v, int lo, int hi) {
        return Math.max(lo, Math.min(hi, v));
    }

    private int screenWidth() {
        if (Build.VERSION.SDK_INT >= 30) {
            return wm.getCurrentWindowMetrics().getBounds().width();
        }
        android.graphics.Point p = new android.graphics.Point();
        wm.getDefaultDisplay().getRealSize(p);
        return p.x;
    }

    private int screenHeight() {
        if (Build.VERSION.SDK_INT >= 30) {
            return wm.getCurrentWindowMetrics().getBounds().height();
        }
        android.graphics.Point p = new android.graphics.Point();
        wm.getDefaultDisplay().getRealSize(p);
        return p.y;
    }

    /**
     * The pickaxe icon, drawn with Canvas primitives — no image assets needed.
     */
    private static final class PickaxeView extends View {
        private final Paint bg = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint head = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint handle = new Paint(Paint.ANTI_ALIAS_FLAG);

        PickaxeView(android.content.Context c) {
            super(c);
            bg.setColor(Color.parseColor("#E60B0E13"));
            head.setColor(Color.parseColor("#00D4FF"));
            head.setStyle(Paint.Style.STROKE);
            head.setStrokeCap(Paint.Cap.ROUND);
            handle.setColor(Color.parseColor("#00D4FF"));
            handle.setStyle(Paint.Style.STROKE);
            handle.setStrokeCap(Paint.Cap.ROUND);
        }

        @Override protected void onDraw(Canvas canvas) {
            super.onDraw(canvas);
            final float w = getWidth(), h = getHeight();
            bg.setStrokeWidth(w * 0.06f);
            head.setStrokeWidth(w * 0.09f);
            handle.setStrokeWidth(w * 0.10f);

            canvas.drawCircle(w / 2f, h / 2f, Math.min(w, h) / 2f, bg);

            // Handle: diagonal grip from lower-left toward the head.
            canvas.drawLine(w * 0.28f, h * 0.78f, w * 0.66f, h * 0.40f, handle);

            // Head: a wide arc over the top, like a pickaxe blade.
            final android.graphics.RectF oval =
                    new android.graphics.RectF(w * 0.10f, h * 0.02f, w * 0.98f, h * 0.72f);
            canvas.drawArc(oval, 195f, 120f, false, head);
        }
    }

    // -------------------------------------------------------------------------
    // [2] The ImGui panel surface
    // -------------------------------------------------------------------------

    private void togglePanel() {
        if (panel != null) removePanel();
        else addPanel();
    }

    private void addPanel() {
        if (panel != null) return;
        panel = new SurfaceView(this);
        panel.getHolder().setFormat(PixelFormat.TRANSLUCENT);

        SurfaceHolder holder = panel.getHolder();
        holder.addCallback(new SurfaceHolder.Callback() {
            @Override public void surfaceCreated(SurfaceHolder h) {
                NativeBridge.setSurface(h.getSurface());
            }
            @Override public void surfaceChanged(SurfaceHolder h, int format, int w, int ht) {
                NativeBridge.surfaceChanged(w, ht);
            }
            @Override public void surfaceDestroyed(SurfaceHolder h) {
                NativeBridge.clearSurface();
            }
        });

        panelParams = new WindowManager.LayoutParams(
                WindowManager.LayoutParams.MATCH_PARENT,
                WindowManager.LayoutParams.MATCH_PARENT,
                overlayWindowType(),
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                        | WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS,
                PixelFormat.TRANSLUCENT);

        try {
            if (button != null && button.getParent() instanceof WindowManager) {
                wm.removeView(button);
            }
            wm.addView(panel, panelParams);
            panel.setOnTouchListener((v, ev) -> {
                NativeBridge.onTouch(ev.getActionMasked(), ev.getX(), ev.getY());
                return true;
            });
            if (button != null) wm.addView(button, buttonParams);
            Log.i(TAG, "panel shown");
        } catch (WindowManager.BadTokenException e) {
            Log.e(TAG, "failed to show panel", e);
            panel = null;
            if (button != null && button.getParent() == null) {
                try { wm.addView(button, buttonParams); } catch (Exception ignored) { }
            }
        }
    }

    private void removePanel() {
        if (panel == null) return;
        NativeBridge.clearSurface();
        try {
            wm.removeView(panel);
        } catch (IllegalArgumentException ignored) { }
        panel = null;
        Log.i(TAG, "panel removed");
    }

    // -------------------------------------------------------------------------
    // [3] Native callbacks (called from native-lib.cpp)
    // -------------------------------------------------------------------------

    /** Called from NATIVE threads whenever the ImGui UI changes a setting. */
    @SuppressWarnings("unused")
    public void onNativeSettingChanged(String key, boolean isFloat, float value) {
        main.post(() -> {
            try {
                if (isFloat) settingsState.put(key, (double) value);
                else settingsState.put(key, value != 0f);
            } catch (JSONException e) {
                Log.e(TAG, "bad setting key " + key, e);
            }
            main.removeCallbacks(saveRunnable);
            main.postDelayed(saveRunnable, 400);
        });
    }

    /** Called from a native thread when the user closes the panel with [x]. */
    @SuppressWarnings("unused")
    public void onOverlayPanelClosed() {
        main.post(this::removePanel);
    }

    /** Called from native thread when the game is found. */
    @SuppressWarnings("unused")
    public void onGameFound() {
        main.post(() -> {
            gameFound = true;
            gamePid = NativeBridge.getGamePid();
            il2cppBase = NativeBridge.getIl2CppBase();
            Log.i(TAG, "Game found! PID=" + gamePid + ", il2cpp=0x" + Long.toHexString(il2cppBase));
            // Update UI? The overlay will show it.
        });
    }

    // -------------------------------------------------------------------------
    // [3] Settings persistence (org.json -> filesDir/settings.json)
    // -------------------------------------------------------------------------

    private void loadSettings() {
        File f = new File(getFilesDir(), SETTINGS_FILE);
        if (!f.exists()) return;
        try (FileInputStream in = new FileInputStream(f)) {
            byte[] bytes = new byte[(int) f.length()];
            int read = in.read(bytes);
            JSONObject o = new JSONObject(new String(bytes, 0, Math.max(read, 0),
                    StandardCharsets.UTF_8));
            java.util.Iterator<String> it = o.keys();
            while (it.hasNext()) {
                String key = it.next();
                Object v = o.get(key);
                if (v instanceof Double || v instanceof Integer || v instanceof Long
                        || v instanceof Boolean) {
                    boolean isFloat = !(v instanceof Boolean);
                    float fv = v instanceof Boolean ? (((Boolean) v) ? 1f : 0f)
                                                    : ((Number) v).floatValue();
                    settingsState.put(key, v);
                    NativeBridge.setSetting(key, isFloat, fv);
                }
            }
            Log.i(TAG, "settings loaded: " + settingsState);
        } catch (IOException | JSONException e) {
            Log.w(TAG, "could not load settings, using defaults", e);
        }
    }

    private void saveSettingsNow() {
        try {
            File dst = new File(getFilesDir(), SETTINGS_FILE);
            File tmp = new File(getFilesDir(), SETTINGS_FILE + ".tmp");
            try (FileOutputStream out = new FileOutputStream(tmp)) {
                out.write(settingsState.toString().getBytes(StandardCharsets.UTF_8));
                out.getFD().sync();
            }
            if (!tmp.renameTo(dst)) Log.w(TAG, "settings rename failed");
        } catch (IOException e) {
            Log.e(TAG, "could not save settings", e);
        }
    }
}
