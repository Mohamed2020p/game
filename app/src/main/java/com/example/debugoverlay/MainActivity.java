package com.example.debugoverlay;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.widget.Button;
import android.widget.ScrollView;
import android.widget.TextView;

/**
 * Launcher activity for the BulldozerMaster debug overlay.
 *
 * Features:
 *   - Overlay permission check/grant
 *   - Start/Stop the debug service (which runs the ImGui overlay + game scanner)
 *   - Force rescan for the game process
 *   - Live status display (game found, PID, il2cpp base)
 *   - Quick cheat toggles (Unlimited Money, Max Speed, One-Hit Kill)
 */
public class MainActivity extends Activity {

    private static final int REQ_POST_NOTIFICATIONS = 1;

    private Button btnPermission;
    private Button btnService;
    private Button btnRescan;
    private TextView txtStatus;
    private TextView txtGameStatus;
    private ScrollView scrollContainer;

    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private final Runnable statusUpdater = this::refreshUi;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        btnPermission = findViewById(R.id.btn_permission);
        btnService = findViewById(R.id.btn_service);
        btnRescan = findViewById(R.id.btn_rescan);
        txtStatus = findViewById(R.id.txt_status);
        txtGameStatus = findViewById(R.id.txt_game_status);
        scrollContainer = findViewById(R.id.scroll_container);

        btnPermission.setOnClickListener(v -> {
            if (Settings.canDrawOverlays(this)) return;
            Intent i = new Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION,
                    Uri.parse("package:" + getPackageName()));
            startActivity(i);
        });

        btnService.setOnClickListener(v -> {
            if (DebugService.isRunning) {
                stopService(new Intent(this, DebugService.class));
            } else if (Settings.canDrawOverlays(this)) {
                Intent svc = new Intent(this, DebugService.class);
                if (Build.VERSION.SDK_INT >= 26) {
                    startForegroundService(svc);
                } else {
                    startService(svc);
                }
            }
            refreshUi();
        });

        btnRescan.setOnClickListener(v -> {
            if (DebugService.isRunning) {
                NativeBridge.forceRescan();
            }
            refreshUi();
        });

        if (Build.VERSION.SDK_INT >= 33 &&
                checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)
                        != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS},
                    REQ_POST_NOTIFICATIONS);
        }

        // Register callback for game found events from native
        NativeBridge.setCallbackTarget(this);
    }

    @Override
    protected void onResume() {
        super.onResume();
        refreshUi();
        // Start periodic updates every 2 seconds
        mainHandler.postDelayed(statusUpdater, 2000);
    }

    @Override
    protected void onPause() {
        super.onPause();
        mainHandler.removeCallbacks(statusUpdater);
    }

    /**
     * Called from native-lib.cpp when the game is found.
     * This is the callback registered via setCallbackTarget().
     */
    @SuppressWarnings("unused")
    public void onGameFound() {
        mainHandler.post(this::refreshUi);
    }

    private void refreshUi() {
        boolean overlayOk = Settings.canDrawOverlays(this);
        boolean serviceRunning = DebugService.isRunning;

        btnPermission.setEnabled(!overlayOk);
        btnPermission.setText(overlayOk
                ? "✓ Overlay permission granted"
                : "Grant overlay permission");

        btnService.setEnabled(overlayOk);
        btnService.setText(serviceRunning
                ? "⏹ Stop demo overlay"
                : "▶ Start demo overlay");

        btnRescan.setEnabled(serviceRunning);

        // ---- Game status ----
        boolean gameFound = NativeBridge.isGameFound();
        int pid = NativeBridge.getGamePid();
        long base = NativeBridge.getIl2CppBase();

        StringBuilder sb = new StringBuilder();
        sb.append("Service: ").append(serviceRunning ? "RUNNING" : "idle").append("\n");
        sb.append("Overlay: ").append(overlayOk ? "granted ✓" : "denied ✗").append("\n");
        sb.append("Service PID: ").append(android.os.Process.myPid()).append("\n");
        sb.append("Data dir: ").append(getFilesDir().getPath()).append("\n");

        txtStatus.setText(sb.toString());

        // ---- Game status (nicely formatted) ----
        StringBuilder gs = new StringBuilder();
        gs.append("┌─ GAME STATUS ─────────────────────\n");
        if (gameFound) {
            gs.append("│  Game: io.supercent.bulldozermasters\n");
            gs.append("│  PID:  ").append(pid).append("\n");
            gs.append("│  il2cpp base: 0x").append(Long.toHexString(base)).append("\n");
            gs.append("│  State: ").append(base != 0 ? "✅ READY" : "⏳ loading...").append("\n");
        } else {
            gs.append("│  ⏳ Scanning for game...\n");
            gs.append("│  (io.supercent.bulldozermasters)\n");
            gs.append("│  Start the game first.\n");
        }
        gs.append("└────────────────────────────────────");
        txtGameStatus.setText(gs.toString());
    }
}
