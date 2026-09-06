package com.example.debugoverlay;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.Settings;
import android.widget.Button;
import android.widget.TextView;

/**
 * Launcher activity: explains the demo, routes the user through the one
 * permission it needs (SYSTEM_ALERT_WINDOW), and starts/stops DebugService.
 *
 * The interesting engineering is NOT here — it's in:
 *   DebugService.java  (overlay window management + settings persistence)
 *   native-lib.cpp     (JNI bridge + render thread)
 *   overlay.cpp        (EGL/ImGui UI)
 *   memory.h           (procfs parsing, safe reads, scanning)
 *   il2cpp_demo.h      (IL2CPP-format metadata build + parse)
 *   demo_game.cpp      (the embedded simulation being introspected)
 */
public class MainActivity extends Activity {

    private static final int REQ_POST_NOTIFICATIONS = 1;

    private Button btnPermission;
    private Button btnService;
    private TextView txtStatus;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        btnPermission = findViewById(R.id.btn_permission);
        btnService = findViewById(R.id.btn_service);
        txtStatus = findViewById(R.id.txt_status);

        btnPermission.setOnClickListener(v -> {
            if (Settings.canDrawOverlays(this)) return;
            // The overlay permission lives in Settings, not the runtime dialog:
            // this intent opens the exact page for OUR app.
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
                    startForegroundService(svc);   // must call startForeground() soon
                } else {
                    startService(svc);             // pre-O API
                }
            }
            refreshUi();
        });

        // The service posts a foreground notification; on Android 13+ ask for
        // notification visibility (best-effort — not required to function).
        if (Build.VERSION.SDK_INT >= 33 &&
                checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)
                        != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS},
                    REQ_POST_NOTIFICATIONS);
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        refreshUi();
    }

    private void refreshUi() {
        boolean overlayOk = Settings.canDrawOverlays(this);
        btnPermission.setEnabled(!overlayOk);
        btnPermission.setText(overlayOk
                ? "Overlay permission granted" : "Grant overlay permission");
        btnService.setEnabled(overlayOk);
        btnService.setText(DebugService.isRunning
                ? "Stop demo overlay" : "Start demo overlay");
        txtStatus.setText(String.format(
                "status: %s | overlay: %s\nservice pid: %d\napp data: %s",
                DebugService.isRunning ? "running" : "idle",
                overlayOk ? "granted" : "denied",
                android.os.Process.myPid(),
                getFilesDir().getPath()));
    }
}
