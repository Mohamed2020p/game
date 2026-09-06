// =============================================================================
//  overlay.h — introspection engine + ImGui overlay app (declarations)
// =============================================================================
//
//  Two cooperating pieces live here:
//
//  1. Introspection — the educational pipeline that finds and reads the demo
//     game through its bytes, never through C++ symbols:
//
//        /proc/self/maps ──► scan writable regions for the DGM1 signature
//                        ──► validate candidate (header/footer/checksum)
//                        ──► resolve field addresses from parsed IL2CPP-style
//                            metadata (class.field → offset)
//                        ──► SafeRead through the region cache, with torn-
//                            snapshot detection via checksum retry
//
//  2. App — EGL/GLES3 + ImGui lifecycle for the floating panel: theme, input
//     feed, the six tabs, and the settings sink that persists changes as JSON
//     on the Java side.
//
//  Everything stateful is intentionally readable and heavily commented — this
//  code is the lesson.
// =============================================================================

#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <EGL/egl.h>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include <android/native_window.h>

#include "demo_game.h"
#include "il2cpp_demo.h"
#include "memory.h"

namespace overlay {

// -----------------------------------------------------------------------------
// Settings (persisted by the Java layer as JSON)
// -----------------------------------------------------------------------------

struct Settings {
    float uiScale   = 1.0f;   // 0.70 .. 1.60 — whole-panel scale
    float bgOpacity = 0.86f;  // 0.40 .. 0.95 — glass tint strength
    bool  showFps   = true;   // FPS badge in the header
};

// Canonical setting keys shared with the Java side (DebugService.java).
constexpr const char* kKeyUiScale   = "ui_scale";
constexpr const char* kKeyBgOpacity = "bg_opacity";
constexpr const char* kKeyShowFps   = "show_fps";

// -----------------------------------------------------------------------------
// Introspection — find & read the demo game through memory only
// -----------------------------------------------------------------------------

struct Diagnostics {
    std::string state = "starting";     // starting | scanning | locked | lost

    pid_t     pid = 0;
    std::string cmdline;
    size_t    regionCount = 0;
    size_t    scannedRegions = 0;
    size_t    scannedBytes = 0;

    uint64_t  rootAddress = 0;          // where the scan locked on
    uint64_t  scanCycles = 0;
    uint64_t  candidatesSeen = 0;

    uint64_t  snapshotsOk = 0;          // clean snapshot reads
    uint64_t  snapshotsTorn = 0;        // checksum mismatches (tick raced us)
    uint64_t  readsFailed = 0;          // SafeRead returned short

    int64_t   processVmSelfTest = -1;   // bytes read via process_vm_readv(self)
    std::string layoutValidation;       // metadata ↔ compiled struct check
};

class Introspection {
public:
    void Init();                        // build+parse metadata, self tests
    void Update();                      // rescan/revalidate/sample — render thread

    // Resolved live values (snapshot-consistent after Update()).
    const demo::GameRoot& snapshot() const { return snapshot_; }
    bool  hasSnapshot() const { return hasSnapshot_; }
    const Diagnostics& diag() const { return diag_; }
    const il2cpp::Metadata& metadata() const { return metadata_; }
    const std::vector<mem::MemoryRegion>& maps() const { return maps_; }

    // Address arithmetic — the educational heart. Both are computed from the
    // METADATA, never from hardcoded numbers:
    //   ObjectAddress(cls)        = root + <GameRoot.cls offset>
    //   FieldAddress(cls, field)  = ObjectAddress(cls) + <cls.field offset>
    uint64_t ObjectAddress(const char* cls) const;
    uint64_t FieldAddress(const char* cls, const char* field) const;

    // Bounds-checked, metadata-driven typed read at FieldAddress().
    template <typename T>
    bool ReadField(const char* cls, const char* field, T* out) const {
        const uint64_t addr = FieldAddress(cls, field);
        if (addr == 0) return false;
        if (!mem::SafeReadValue(addr, out, maps_)) return false;
        return true;
    }

    // Force a rescan on the next Update (Misc tab button).
    void RequestRescan() { rescanRequested_ = true; }

    // History buffers for the sparklines (sampled ~10 Hz in Update(); each
    // new sample is appended at the back, oldest drops off the front).
    static constexpr int kHistoryLen = 160;
    struct History {
        std::array<float, kHistoryLen> coins{};
        std::array<float, kHistoryLen> health{};
        std::array<float, kHistoryLen> oreAll{};
    };
    const History& history() const { return history_; }

private:
    void Rescan();                      // one full scan pass
    bool TrySnapshot();                 // SafeRead + validate with retry
    void SampleHistory();

    il2cpp::Metadata metadata_;         // parsed IL2CPP-style metadata
    std::vector<uint8_t> metadataBlob_;
    std::vector<mem::MemoryRegion> maps_;

    demo::GameRoot snapshot_{};
    bool hasSnapshot_ = false;
    bool rescanRequested_ = false;
    uint64_t samplesDone_ = 0;
    uint64_t lastScanMs_ = 0;
    uint64_t lastValidateMs_ = 0;
    uint64_t lastMapsMs_ = 0;

    Diagnostics diag_{};
    History history_;
};

// -----------------------------------------------------------------------------
// App — EGL + ImGui lifecycle for the floating panel
// -----------------------------------------------------------------------------

class App {
public:
    // Screen density (from Java DisplayMetrics). Set BEFORE Init — it sizes
    // fonts and the panel so it looks identical on mdpi and xxhdpi screens.
    void SetDensity(float density) { density_ = density; }

    // Called from the render thread once a surface is available.
    bool Init(ANativeWindow* window);
    void Resize(int width, int height);
    void Shutdown();                    // releases EGL + ImGui for this surface

    // Render one frame. Returns false when the user closed the panel (the [x]
    // in its title bar) — native-lib then tears the surface down and the close
    // handler below tells Java to remove the SurfaceView.
    bool Frame();

    // Called from the UI thread (JNI) with Android MotionEvent coordinates.
    // action: 0 = DOWN, 1 = UP, 2 = MOVE, 3 = CANCEL (MotionEvent.ACTION_*).
    void OnTouch(int action, float x, float y);

    // Settings flow both ways:
    //  * Java  -> native: SetSetting on (re)load from settings.json
    //  * native -> Java: settingsSink fires on every UI-originated change;
    //             DebugService serializes it back to settings.json
    void SetSetting(const char* key, bool isFloat, float value);
    void SetSettingsSink(std::function<void(const char*, bool, float)> sink) {
        settingsSink_ = std::move(sink);
    }

    // Fired once when the user closes the panel (UI thread via native-lib).
    void SetCloseHandler(std::function<void()> handler) {
        closeHandler_ = std::move(handler);
    }

    Settings settings() const;          // thread-safe copy

    Introspection& introspection() { return intro_; }

private:
    void DrawMainWindow();
    void ApplyTheme();
    void DrawPlayerTab();
    void DrawVehiclesTab();
    void DrawOreTab();
    void DrawWorkersTab();
    void DrawEconomyTab();
    void DrawMiscTab();
    void PersistSetting(const char* key, bool isFloat, float value);

    ANativeWindow* window_ = nullptr;
    int width_ = 0, height_ = 0;
    float density_ = 2.0f;

    EGLDisplay eglDisplay_ = EGL_NO_DISPLAY;
    EGLSurface eglSurface_ = EGL_NO_SURFACE;
    EGLContext eglContext_ = EGL_NO_CONTEXT;
    bool  imguiInitialized_ = false;
    bool  wantClose_ = false;
    std::chrono::steady_clock::time_point lastFrame_;

    Settings settings_;                 // guarded by settingsMutex_
    mutable std::mutex settingsMutex_;
    std::function<void(const char*, bool, float)> settingsSink_;
    std::function<void()> closeHandler_;

    Introspection intro_;
    bool introInitialized_ = false;
};

// Custom widget shared by the tab UIs — an iOS-style toggle switch.
bool ToggleSwitch(const char* label, bool* value, float height = 0.0f);

// Accent color used across the theme (#00D4FF).
constexpr float kAccentR = 0.00f, kAccentG = 0.831f, kAccentB = 1.0f;

} // namespace overlay
