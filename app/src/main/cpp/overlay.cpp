// =============================================================================
//  overlay.cpp — EGL/GLES3 bootstrap, ImGui theme/widgets, and the six tabs
// =============================================================================
//  Reading order for learners:
//    1. Introspection implementation  (find + read the demo game via bytes)
//    2. EGL + ImGui lifecycle         (surface -> context -> frames)
//    3. Theme + custom toggle widget  (the "dark glass" look)
//    4. The six tabs                  (all values come from the introspection
//                                      snapshot; all changes go through the
//                                      demo game's public API)
// =============================================================================

#include "overlay.h"

#include <cfloat>
#include <cinttypes>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <thread>

#include <GLES3/gl3.h>
#include <android/log.h>

#include "imgui/imgui.h"
#include "imgui_impl/imgui_impl_opengl3.h"

#define LOG_TAG "Overlay"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace overlay {

namespace {

// -----------------------------------------------------------------------------
// Small utilities
// -----------------------------------------------------------------------------

uint64_t NowMs() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
}

ImVec4 Accent(float alpha = 1.0f, float mul = 1.0f) {
    return ImVec4(kAccentR * mul, kAccentG * mul, kAccentB * mul, alpha);
}

// Monospace text (numbers, addresses) — font resolved after Init loads fonts.
ImFont* g_fontMono = nullptr;
void TextMono(const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (g_fontMono) ImGui::PushFont(g_fontMono);
    ImGui::TextUnformatted(buf);
    if (g_fontMono) ImGui::PopFont();
}

// "label ..... value" row with a fixed label column (printf-style value).
void ValueRow(const char* label, const char* fmt, ...) {
    char buf[384];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    ImGui::TextDisabled("%s", label);
    ImGui::SameLine(150.0f);
    TextMono("%s", buf);
}

// PlotLines getter over a plain array (history buffers shift-append).
float HistoryGetter(void* data, int idx) {
    auto* arr = static_cast<std::array<float, Introspection::kHistoryLen>*>(data);
    return (*arr)[(size_t)idx];
}

// Worker-state names shared by the Workers tab.
const char* WorkerStateName(int32_t s) {
    switch (s) {
        case demo::kWorkerMining:  return "mining";
        case demo::kWorkerHauling: return "hauling";
        case demo::kWorkerResting: return "resting";
        default:                   return "idle";
    }
}

} // namespace

// =============================================================================
//  1. Introspection — the engine
// =============================================================================

void Introspection::Init() {
    // --- metadata: build + parse + validate ---------------------------------
    // In a real project the blob would come from global-metadata.dat inside
    // the APK and the offsets would come from the dumped binary; here both are
    // generated from the code that is running — but every parsing step is the
    // real thing: magic check, version check, section bounds checks, string
    // table resolution, type/field table walks.
    metadataBlob_ = il2cpp::BuildDemoMetadataBlob();
    const bool parsed = metadata_.Parse(metadataBlob_.data(), metadataBlob_.size());
    LOGI("metadata blob %zu bytes, parsed=%s, %zu classes",
         metadataBlob_.size(), parsed ? "ok" : "FAILED", metadata_.classes().size());
    diag_.layoutValidation = parsed ? metadata_.ValidateAgainstGameLayout()
                                    : "metadata parse FAILED";

    // --- self identity --------------------------------------------------------
    diag_.pid     = mem::SelfPid();
    diag_.cmdline = mem::SelfCmdline();

    // process_vm_readv(self) smoke test — proof the cross-process syscall API
    // works, pointed at our own (harmless) stack address.
    int32_t probe = 0x0BADC0DE;
    int32_t back  = 0;
    diag_.processVmSelfTest =
            mem::SelfTestProcessVmReadv((uint64_t)(uintptr_t)&probe, sizeof(probe), &back);

    maps_ = mem::ReadSelfMaps();
    diag_.regionCount = maps_.size();
    diag_.state = "scanning";
    LOGI("introspection init: pid=%d cmdline=%s regions=%zu pvrr=%" PRId64,
         diag_.pid, diag_.cmdline.c_str(), maps_.size(), diag_.processVmSelfTest);
}

uint64_t Introspection::ObjectAddress(const char* cls) const {
    const il2cpp::FieldInfo* f = metadata_.FindField("GameRoot", cls);
    if (!f || diag_.rootAddress == 0) return 0;
    return diag_.rootAddress + (uint64_t)f->offset;
}

uint64_t Introspection::FieldAddress(const char* cls, const char* field) const {
    const il2cpp::FieldInfo* f = metadata_.FindField(cls, field);
    if (!f) return 0;
    return ObjectAddress(cls) + (uint64_t)f->offset;
}

void Introspection::Rescan() {
    maps_ = mem::ReadSelfMaps();      // always scan against a FRESH map
    diag_.regionCount   = maps_.size();
    diag_.scanCycles   += 1;
    diag_.candidatesSeen = 0;

    // Scan target: writable private memory (heap / pthread stacks / .data+.bss
    // of our own libs). We skip the main [stack]: the real object is heap
    // allocated, and thread stacks are where STALE COPIES of it linger (old
    // snapshot buffers) — the last thing we want to lock onto.
    std::vector<mem::MemoryRegion> scanRegions;
    scanRegions.reserve(maps_.size());
    size_t scannedRegions = 0, scannedBytes = 0;
    for (const auto& r : maps_) {
        if (!r.IsReadable() || !r.IsWritable() || !r.IsPrivate()) continue;
        if (r.pathname == "[stack]") continue;
        ++scannedRegions;
        scannedBytes += (size_t)r.Size();
        scanRegions.push_back(r);
    }
    diag_.scannedRegions = scannedRegions;
    diag_.scannedBytes   = scannedBytes;

    const uint32_t magic = demo::kMagicHeader;
    auto verify = [this](uint64_t addr) {
        diag_.candidatesSeen += 1;
        // Stage 1 — structural validation: envelope, version, payload size,
        // footer AND checksum. Cheap pattern hits lie; the checksum does not.
        demo::GameRoot probe{};
        if (!mem::SafeReadValue(addr, &probe, maps_) || !demo::ValidateRoot(probe))
            return false;

        // Stage 2 — liveness: the live simulation mutates every ~100 ms tick,
        // so its checksum keeps changing. A stale, frozen byte-copy of the
        // object (left over on some thread's stack from an old snapshot)
        // validates structurally but will not move. Waiting one tick period
        // and comparing checksums is what separates them. (This is the same
        // family of trick as "does the value change?" filtering in classic
        // memory scanners.)
        const uint32_t c1 = probe.checksum;
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        demo::GameRoot probe2{};
        if (!mem::SafeReadValue(addr, &probe2, maps_) || !demo::ValidateRoot(probe2))
            return false;
        return probe2.checksum != c1;
    };
    const uint64_t hit = mem::ScanForPattern(scanRegions, &magic, sizeof(magic),
                                             /*align*/ 8, verify);

    lastScanMs_ = NowMs();
    if (hit != 0) {
        diag_.rootAddress = hit;
        diag_.state = "locked";
        LOGI("scan locked: GameRoot @ 0x%08" PRIx64 " after %" PRIu64 " cycles, %zu candidates",
             hit, diag_.scanCycles, (size_t)diag_.candidatesSeen);
    } else {
        diag_.state = "scanning";
    }
}

bool Introspection::TrySnapshot() {
    // The tick thread keeps mutating while we copy, so a plain read can tear.
    // The checksum envelope catches that; we just retry — with a 10 Hz tick
    // and a ~1.6 KB struct, a clean copy lands within a few attempts.
    for (int attempt = 0; attempt < 4; ++attempt) {
        demo::GameRoot tmp{};
        const size_t n = mem::SafeRead(diag_.rootAddress, sizeof(tmp), &tmp, maps_);
        if (n != sizeof(tmp)) {
            diag_.readsFailed += 1;
            return false;                     // mapping vanished? rescan next
        }
        if (demo::ValidateRoot(tmp)) {
            snapshot_ = tmp;
            diag_.snapshotsOk += 1;
            diag_.state = "locked";
            return true;
        }
        diag_.snapshotsTorn += 1;             // checksum mismatch → torn read
    }
    return false;
}

void Introspection::SampleHistory() {
    // ~10 Hz is plenty for sparklines and keeps the arrays cheap.
    if (++samplesDone_ % 6 != 0) return;

    auto append = [](std::array<float, kHistoryLen>& a, float v) {
        std::memmove(a.data(), a.data() + 1, (kHistoryLen - 1) * sizeof(float));
        a[kHistoryLen - 1] = v;
    };
    float oreAll = 0.0f;
    for (const auto& o : snapshot_.ores) oreAll += o.amount;

    append(history_.coins,  (float)snapshot_.economy.coins);
    append(history_.health, snapshot_.player.health);
    append(history_.oreAll, oreAll);
}

void Introspection::Update() {
    const uint64_t now = NowMs();

    // Refresh the region cache every few seconds so SafeRead's bounds stay true.
    if (now - lastMapsMs_ >= 5000) {
        maps_ = mem::ReadSelfMaps();
        diag_.regionCount = maps_.size();
        lastMapsMs_ = now;
    }

    if (diag_.rootAddress == 0 || rescanRequested_) {
        if (rescanRequested_ || now - lastScanMs_ >= 500) {
            rescanRequested_ = false;
            Rescan();
        }
    } else if (now - lastValidateMs_ >= 2000) {
        // Periodic re-validation: prove the lock is still on a live object.
        demo::GameRoot probe{};
        if (!(mem::SafeReadValue(diag_.rootAddress, &probe, maps_) &&
              demo::ValidateRoot(probe))) {
            diag_.state = "lost";
            diag_.rootAddress = 0;
        }
        lastValidateMs_ = now;
    }

    if (diag_.rootAddress != 0 && TrySnapshot()) {
        hasSnapshot_ = true;
        SampleHistory();
    }
}

// =============================================================================
//  2. EGL + ImGui lifecycle
// =============================================================================

namespace {

EGLConfig ChooseConfigWithAlpha(EGLDisplay dpy) {
    // ALPHA_SIZE 8 is what makes the panel translucent over the app behind it
    // (the SurfaceView's holder is set to TRANSLUCENT on the Java side).
    const EGLint attrs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_NONE };
    EGLConfig cfg = nullptr;
    EGLint n = 0;
    eglChooseConfig(dpy, attrs, &cfg, 1, &n);
    return n > 0 ? cfg : nullptr;
}

} // namespace

bool App::Init(ANativeWindow* window) {
    window_ = window;
    ANativeWindow_acquire(window_);
    width_  = ANativeWindow_getWidth(window_);
    height_ = ANativeWindow_getHeight(window_);

    // --- EGL: display -> config -> window surface -> GLES3 context ----------
    eglDisplay_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!eglInitialize(eglDisplay_, nullptr, nullptr)) {
        LOGE("eglInitialize failed");
        return false;
    }
    eglBindAPI(EGL_OPENGL_ES_API);
    const EGLConfig cfg = ChooseConfigWithAlpha(eglDisplay_);
    if (cfg == nullptr) {
        LOGE("no suitable EGLConfig (GLES3 + RGBA8888)");
        return false;
    }
    eglSurface_ = eglCreateWindowSurface(eglDisplay_, cfg, window_, nullptr);
    const EGLint ctxAttrs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    eglContext_ = eglCreateContext(eglDisplay_, cfg, EGL_NO_CONTEXT, ctxAttrs);
    if (eglSurface_ == EGL_NO_SURFACE || eglContext_ == EGL_NO_CONTEXT ||
        !eglMakeCurrent(eglDisplay_, eglSurface_, eglContext_, eglContext_)) {
        LOGE("EGL surface/context setup failed: 0x%x", (unsigned)eglGetError());
        return false;
    }
    eglSwapInterval(eglDisplay_, 1);   // vsync

    if (!introInitialized_) {
        intro_.Init();
        introInitialized_ = true;
    }

    if (!imguiInitialized_) {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();

        // No imgui.ini writes: this app persists its own settings as JSON.
        io.IniFilename = nullptr;

        // Fonts: prefer crisp system fonts at the density-correct size;
        // gracefully fall back to ImGui's embedded bitmap font.
        const float fontPx = 16.0f * density_;
        ImFont* regular = io.Fonts->AddFontFromFileTTF(
                "/system/fonts/Roboto-Regular.ttf", fontPx);
        if (regular == nullptr) io.Fonts->AddFontDefault();
        g_fontMono = io.Fonts->AddFontFromFileTTF(
                "/system/fonts/RobotoMono-Regular.ttf", fontPx - 1.0f);
        if (g_fontMono == nullptr)
            g_fontMono = io.Fonts->AddFontFromFileTTF(
                    "/system/fonts/DroidSansMono.ttf", fontPx - 1.0f);
        if (g_fontMono == nullptr) g_fontMono = io.Fonts->Fonts[0];

        ImGui_ImplOpenGL3_Init("#version 300 es");
        imguiInitialized_ = true;
        LOGI("ImGui ready (fonts %.0fpx, %s mono)", fontPx,
             g_fontMono ? "system" : "builtin");
    }

    lastFrame_ = std::chrono::steady_clock::now();
    return true;
}

void App::Resize(int width, int height) {
    width_ = width;
    height_ = height;
}

void App::Shutdown() {
    if (imguiInitialized_) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui::DestroyContext();
        imguiInitialized_ = false;
        g_fontMono = nullptr;
    }
    if (eglDisplay_ != EGL_NO_DISPLAY) {
        eglMakeCurrent(eglDisplay_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (eglSurface_ != EGL_NO_SURFACE) eglDestroySurface(eglDisplay_, eglSurface_);
        if (eglContext_ != EGL_NO_CONTEXT) eglDestroyContext(eglDisplay_, eglContext_);
    }
    eglSurface_ = EGL_NO_SURFACE;
    eglContext_ = EGL_NO_CONTEXT;
    if (window_ != nullptr) {
        ANativeWindow_release(window_);
        window_ = nullptr;
    }
}

bool App::Frame() {
    if (window_ == nullptr || eglSurface_ == EGL_NO_SURFACE) return true;

    const auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - lastFrame_).count();
    lastFrame_ = now;
    if (dt <= 0.0f || dt > 0.5f) dt = 1.0f / 60.0f;

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)width_, (float)height_);
    io.DeltaTime   = dt;

    // Drive the introspection engine once per frame.
    intro_.Update();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();

    DrawMainWindow();

    ImGui::Render();
    glViewport(0, 0, width_, height_);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);   // fully transparent background
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    eglSwapBuffers(eglDisplay_, eglSurface_);

    if (wantClose_) {
        wantClose_ = false;
        if (closeHandler_) closeHandler_();  // tells Java to remove the panel
        return false;
    }
    return true;
}

void App::OnTouch(int action, float x, float y) {
    if (!imguiInitialized_) return;
    // Feed Android touch events into ImGui's event queue. One pointer is all a
    // mouse-driven UI needs; multi-touch would map extra pointers onto
    // io.AddMouseViewportEvent/additional mouse sources.
    ImGuiIO& io = ImGui::GetIO();
    switch (action) {
        case 0:  io.AddMousePosEvent(x, y); io.AddMouseButtonEvent(0, true);  break; // DOWN
        case 1:  io.AddMousePosEvent(x, y); io.AddMouseButtonEvent(0, false); break; // UP
        case 2:  io.AddMousePosEvent(x, y);                                   break; // MOVE
        case 3:  io.AddMouseButtonEvent(0, false);                            break; // CANCEL
        default: break;
    }
}

// -----------------------------------------------------------------------------
// Settings plumbing
// -----------------------------------------------------------------------------

void App::SetSetting(const char* key, bool isFloat, float value) {
    std::lock_guard<std::mutex> lock(settingsMutex_);
    if (std::strcmp(key, kKeyUiScale) == 0)   settings_.uiScale   = value;
    else if (std::strcmp(key, kKeyBgOpacity) == 0) settings_.bgOpacity = value;
    else if (std::strcmp(key, kKeyShowFps) == 0)   settings_.showFps = value != 0.0f;
    (void)isFloat;
}

Settings App::settings() const {
    std::lock_guard<std::mutex> lock(settingsMutex_);
    return settings_;
}

// UI-originated change: apply locally AND push to Java for JSON persistence.
void App::PersistSetting(const char* key, bool isFloat, float value) {
    SetSetting(key, isFloat, value);
    if (settingsSink_) settingsSink_(key, isFloat, value);
}

// =============================================================================
//  3. Theme + widgets
// =============================================================================

void App::ApplyTheme() {
    const Settings s = settings();
    const float sc = density_ * s.uiScale;

    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowPadding    = ImVec2(12 * sc, 10 * sc);
    st.FramePadding     = ImVec2(8 * sc, 5 * sc);
    st.ItemSpacing      = ImVec2(8 * sc, 6 * sc);
    st.ItemInnerSpacing = ImVec2(6 * sc, 4 * sc);
    st.CellPadding      = ImVec2(6 * sc, 4 * sc);
    st.ScrollbarSize    = 12 * sc;
    st.GrabMinSize      = 10 * sc;

    st.WindowRounding = 10 * sc;
    st.ChildRounding  = 8 * sc;
    st.FrameRounding  = 6 * sc;
    st.GrabRounding   = 6 * sc;
    st.PopupRounding  = 8 * sc;
    st.TabRounding    = 6 * sc;
    st.WindowBorderSize = 0.0f;
    st.ChildBorderSize  = 0.0f;
    st.PopupBorderSize  = 0.0f;
    st.FrameBorderSize  = 0.0f;

    // Re-assign colors every frame from the current settings — makes the
    // opacity slider live without destructive style mutation.
    const float op = s.bgOpacity;
    ImVec4* c = st.Colors;
    c[ImGuiCol_WindowBg]        = ImVec4(0.055f, 0.067f, 0.086f, op);
    c[ImGuiCol_ChildBg]         = ImVec4(0.090f, 0.102f, 0.129f, 0.35f + 0.45f * op);
    c[ImGuiCol_PopupBg]         = ImVec4(0.070f, 0.082f, 0.106f, 0.94f);
    c[ImGuiCol_TitleBg]         = ImVec4(0.043f, 0.055f, 0.071f, op);
    c[ImGuiCol_TitleBgActive]   = ImVec4(0.055f, 0.071f, 0.090f, op);
    c[ImGuiCol_TitleBgCollapsed]= ImVec4(0.043f, 0.055f, 0.071f, 0.60f);
    c[ImGuiCol_Text]            = ImVec4(0.906f, 0.925f, 0.949f, 1.00f);
    c[ImGuiCol_TextDisabled]    = ImVec4(0.545f, 0.580f, 0.640f, 1.00f);
    c[ImGuiCol_Border]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_FrameBg]         = ImVec4(0.118f, 0.137f, 0.173f, 0.90f);
    c[ImGuiCol_FrameBgHovered]  = ImVec4(0.145f, 0.169f, 0.212f, 0.95f);
    c[ImGuiCol_FrameBgActive]   = ImVec4(0.165f, 0.196f, 0.247f, 1.00f);
    c[ImGuiCol_Button]          = ImVec4(0.133f, 0.153f, 0.192f, 1.00f);
    c[ImGuiCol_ButtonHovered]   = Accent(1.00f, 0.35f);
    c[ImGuiCol_ButtonActive]    = Accent(1.00f, 0.55f);
    c[ImGuiCol_Header]          = Accent(0.35f);
    c[ImGuiCol_HeaderHovered]   = Accent(0.50f);
    c[ImGuiCol_HeaderActive]    = Accent(0.65f);
    c[ImGuiCol_CheckMark]       = Accent(1.00f);
    c[ImGuiCol_SliderGrab]      = Accent(1.00f);
    c[ImGuiCol_SliderGrabActive]= Accent(1.00f, 0.75f);
    c[ImGuiCol_Separator]       = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
    c[ImGuiCol_SeparatorHovered]= Accent(0.45f);
    c[ImGuiCol_SeparatorActive] = Accent(0.65f);
    c[ImGuiCol_Tab]             = ImVec4(0.090f, 0.102f, 0.129f, 0.85f);
    c[ImGuiCol_TabActive]      = Accent(0.22f);
    c[ImGuiCol_TabHovered]      = Accent(0.40f);
    c[ImGuiCol_TabUnfocused]       = ImVec4(0.078f, 0.090f, 0.114f, 0.85f);
    c[ImGuiCol_TabUnfocusedActive] = Accent(0.14f);
    c[ImGuiCol_PlotLines]       = Accent(1.00f);
    c[ImGuiCol_PlotLinesHovered]= Accent(1.00f, 0.75f);
    c[ImGuiCol_PlotHistogram]   = Accent(0.85f);
    c[ImGuiCol_PlotHistogramHovered] = Accent(1.00f);
    c[ImGuiCol_ScrollbarBg]     = ImVec4(0.043f, 0.055f, 0.071f, 0.40f);
    c[ImGuiCol_ScrollbarGrab]   = ImVec4(0.220f, 0.251f, 0.310f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered] = Accent(0.55f);
    c[ImGuiCol_ScrollbarGrabActive]  = Accent(0.80f);
    c[ImGuiCol_ResizeGrip]      = ImVec4(1, 1, 1, 0.04f);
    c[ImGuiCol_ResizeGripHovered]    = Accent(0.35f);
    c[ImGuiCol_ResizeGripActive]     = Accent(0.60f);
    c[ImGuiCol_ModalWindowDimBg]= ImVec4(0.00f, 0.00f, 0.00f, 0.50f);
}

// iOS-style toggle switch drawn with the window's ImDrawList.
bool ToggleSwitch(const char* label, bool* value, float height) {
    const float sz = height > 0.0f ? height : ImGui::GetFrameHeight() * 0.72f;
    const float width = sz * 1.75f;
    const ImVec2 pos = ImGui::GetCursorScreenPos();

    const bool pressed = ImGui::InvisibleButton(label, ImVec2(width, sz));
    if (pressed) *value = !*value;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 offCol = ImGui::GetColorU32(ImGuiCol_FrameBgActive);
    const ImU32 onCol  = ImGui::GetColorU32(Accent(0.95f));
    dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + sz),
                      *value ? onCol : offCol, sz * 0.5f);

    const float r = sz * 0.5f - 2.0f;
    const float cx = *value ? pos.x + width - r - 2.0f : pos.x + r + 2.0f;
    dl->AddCircleFilled(ImVec2(cx, pos.y + sz * 0.5f), r,
                        IM_COL32(235, 240, 246, 255));

    // Label to the right, vertically centered on the switch.
    ImGui::SameLine(0.0f, 10.0f);
    const float th = ImGui::GetTextLineHeight();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (sz - th) * 0.5f);
    ImGui::TextUnformatted(label);
    return pressed;
}

// =============================================================================
//  4. The main window + six tabs
// =============================================================================

void App::DrawMainWindow() {
    const Settings s = settings();
    const float sc = density_ * s.uiScale;

    ImGui::SetNextWindowSize(ImVec2(500 * sc, 660 * sc), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(width_ * 0.5f, height_ * 0.44f),
                            ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));

    bool open = true;
    const ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoCollapse;      // draggable via its title bar
    ImGui::Begin("DEBUG OVERLAY  —  self-introspection demo##main", &open, flags);
    if (!open) wantClose_ = true;
    ImGui::SetWindowFontScale(s.uiScale);

    // ---- status header ------------------------------------------------------
    const Diagnostics& d = intro_.diag();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float lineH = ImGui::GetTextLineHeight();
    const ImU32 dotCol = d.state == "locked"  ? IM_COL32(0x2E, 0xCC, 0x71, 255)
                       : d.state == "scanning"? IM_COL32(0xF3, 0x9C, 0x12, 255)
                                              : IM_COL32(0xE7, 0x4C, 0x3C, 255);
    dl->AddCircleFilled(ImVec2(p.x + 5 * sc, p.y + lineH * 0.5f), 4.5f * sc, dotCol);
    ImGui::Dummy(ImVec2(14 * sc, lineH));
    ImGui::SameLine();
    TextMono("engine: %-9s root @ 0x%08" PRIx64, d.state.c_str(), d.rootAddress);
    if (s.showFps) {
        ImGui::SameLine();
        const float w = ImGui::GetContentRegionAvail().x;
        const float tw = ImGui::CalcTextSize("999 fps").x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + w - tw);
        ImGui::TextColored(Accent(0.9f), "%.0f fps", ImGui::GetIO().Framerate);
    }
    ImGui::Separator();

    // ---- tabs -----------------------------------------------------------------
    if (ImGui::BeginTabBar("##maintabs", ImGuiTabBarFlags_None)) {
        if (ImGui::BeginTabItem("Player"))  { DrawPlayerTab();  ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Vehicles")){ DrawVehiclesTab();ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Ore"))     { DrawOreTab();     ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Workers")) { DrawWorkersTab(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Economy")) { DrawEconomyTab(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Misc"))    { DrawMiscTab();    ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

// ---------------------------------------------------------------- Player ----
void App::DrawPlayerTab() {
    const float sc = density_ * settings().uiScale;
    Introspection& I = intro_;
    if (!I.hasSnapshot()) {
        ImGui::TextDisabled("waiting for the scanner to lock onto GameRoot…");
        return;
    }
    const demo::Player& pl = I.snapshot().player;
    const demo::GameSettings& gs = I.snapshot().settings;

    // Health bar (live value read through the introspection snapshot).
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, Accent(0.85f));
    char overlay[64];
    snprintf(overlay, sizeof(overlay), "%.1f / %.1f", pl.health, pl.maxHealth);
    ImGui::ProgressBar(pl.health / pl.maxHealth,
                       ImVec2(-1, 22 * sc), overlay);
    ImGui::PopStyleColor();

    ValueRow("stamina", "%.2f", pl.stamina);
    ValueRow("position", "(%.1f, %.1f)", pl.posX, pl.posY);
    ValueRow("base move speed", "%.2f u/s", pl.baseMoveSpeed);
    ValueRow("level / xp", "%d  /  %d xp", pl.level, pl.xp);

    ImGui::Spacing();
    ImGui::PlotLines("##healthhist", HistoryGetter,
                     const_cast<std::array<float, Introspection::kHistoryLen>*>(
                             &I.history().health),
                     Introspection::kHistoryLen, 0, "health history",
                     0.0f, 110.0f, ImVec2(-1, 56 * sc));

    ImGui::Spacing();
    ImGui::SeparatorText("demo API (writes go through game code, never memory)");

    float speed = gs.moveSpeedMul;
    if (ImGui::SliderFloat("move speed x", &speed, 0.25f, 4.0f, "%.2fx"))
        demo::DemoGame::Instance().SetMoveSpeedMul(speed);

    bool boost = gs.staminaBoost != 0;
    if (ToggleSwitch("stamina boost", &boost))
        demo::DemoGame::Instance().SetStaminaBoost(boost);

    if (ImGui::Button("respawn player"))
        demo::DemoGame::Instance().RespawnPlayer();

    // ---- the educational part: show exactly how the address was composed ----
    ImGui::Spacing();
    if (ImGui::CollapsingHeader("how this value was found")) {
        const il2cpp::FieldInfo* fPlayer =
                I.metadata().FindField("GameRoot", "player");
        const il2cpp::FieldInfo* fHealth =
                I.metadata().FindField("Player", "health");
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.58f, 0.64f, 1.0f));
        TextMono("root      = scan(\"DGM1\") + validate      = 0x%08" PRIx64,
                 intro_.diag().rootAddress);
        if (fPlayer)
            TextMono("player    = root + GameRoot.player(%d)   = 0x%08" PRIx64,
                     fPlayer->offset, I.ObjectAddress("Player"));
        if (fHealth)
            TextMono("health    = player + Player.health(%d)   = 0x%08" PRIx64,
                     fHealth->offset, I.FieldAddress("Player", "health"));
        TextMono("read      = SafeRead(health, 4 bytes, bounds-checked)");
        ImGui::PopStyleColor();
    }
}

// -------------------------------------------------------------- Vehicles ----
void App::DrawVehiclesTab() {
    Introspection& I = intro_;
    if (!I.hasSnapshot()) { ImGui::TextDisabled("no snapshot yet"); return; }
    const auto& snap = I.snapshot();

    ImGui::SeparatorText("demo API");
    float vmul = snap.settings.vehicleSpeedMul;
    if (ImGui::SliderFloat("vehicle speed x", &vmul, 0.25f, 4.0f, "%.2fx"))
        demo::DemoGame::Instance().SetVehicleSpeedMul(vmul);
    ImGui::Spacing();

    if (ImGui::BeginTable("##vehicles", 4,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("vehicle", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn("fuel", ImGuiTableColumnFlags_WidthStretch, 3.0f);
        ImGui::TableSetupColumn("engine", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("ops", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableHeadersRow();
        for (int i = 0; i < 4; ++i) {
            const demo::Vehicle& v = snap.vehicles[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(v.name);
            ImGui::TableNextColumn();
            const float frac = v.fuel / v.fuelMax;
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                                  frac < 0.2f ? ImVec4(0.91f, 0.30f, 0.24f, 1.0f)
                                              : Accent(0.8f));
            ImGui::ProgressBar(frac, ImVec2(-1, 0), "");
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%.1f / %.1f  (%d refuels)", v.fuel, v.fuelMax,
                                  v.refuelCount);
            ImGui::TableNextColumn();
            bool on = v.engineOn != 0;
            if (ToggleSwitch(("##eng" + std::to_string(i)).c_str(), &on, 20.0f))
                demo::DemoGame::Instance().SetEngineOn(i, on);
            ImGui::TableNextColumn();
            ImGui::PushID(i);
            if (ImGui::SmallButton("refuel"))
                demo::DemoGame::Instance().RefuelVehicle(i);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();
    ImGui::TextDisabled("tip: hover a fuel bar to see its raw fields");
}

// ------------------------------------------------------------------- Ore ----
void App::DrawOreTab() {
    Introspection& I = intro_;
    if (!I.hasSnapshot()) { ImGui::TextDisabled("no snapshot yet"); return; }
    const auto& snap = I.snapshot();

    float maxAmount = 1.0f;
    for (const auto& o : snap.ores) maxAmount = std::max(maxAmount, o.amount);

    // Tier colors: a taste of what the real games' rarity palettes look like.
    const ImVec4 tierCols[5] = {
        ImVec4(0.71f, 0.74f, 0.78f, 1),  // Iron
        ImVec4(0.87f, 0.52f, 0.28f, 1),  // Copper
        ImVec4(0.98f, 0.81f, 0.26f, 1),  // Gold
        ImVec4(0.45f, 0.78f, 0.98f, 1),  // Crystal
        ImVec4(0.42f, 0.90f, 0.48f, 1),  // Uranium
    };

    for (int i = 0; i < 5; ++i) {
        const demo::OreVein& o = snap.ores[i];
        int miners = 0;
        for (const auto& w : snap.workers)
            if (w.oreIndex == i && (w.state == demo::kWorkerMining ||
                                    w.state == demo::kWorkerHauling)) ++miners;

        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, tierCols[i]);
        ImGui::ProgressBar(o.amount / maxAmount, ImVec2(-1, 18), "");
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s  tier %d\nrichness %.2f u/s/miner\nprice %.2f c/u\n%d workers assigned",
                              o.name, o.tier, o.richness, o.price, miners);
        ImGui::SameLine(0.0f, 10.0f);
        TextMono("%-8s %10.1f", o.name, o.amount);
    }

    ImGui::Spacing();
    ImGui::PlotLines("##orehist", HistoryGetter,
                     const_cast<std::array<float, Introspection::kHistoryLen>*>(
                             &I.history().oreAll),
                     Introspection::kHistoryLen, 0, "total extracted",
                     0.0f, FLT_MAX, ImVec2(-1, 56));

    ImGui::Spacing();
    ImGui::SeparatorText("demo API");
    bool smelt = snap.settings.autoSmelt != 0;
    if (ToggleSwitch("auto-smelt (ore -> coins)", &smelt))
        demo::DemoGame::Instance().SetAutoSmelt(smelt);
}

// --------------------------------------------------------------- Workers ----
void App::DrawWorkersTab() {
    Introspection& I = intro_;
    if (!I.hasSnapshot()) { ImGui::TextDisabled("no snapshot yet"); return; }
    const auto& snap = I.snapshot();

    int stateCount[4] = {0, 0, 0, 0};
    for (const auto& w : snap.workers) {
        const int s = w.state >= 0 && w.state <= 3 ? w.state : 0;
        ++stateCount[s];
    }
    ValueRow("idle / mining", "%d / %d", stateCount[0], stateCount[1]);
    ValueRow("hauling / resting", "%d / %d", stateCount[2], stateCount[3]);
    ImGui::Spacing();

    if (ImGui::BeginTable("##workers", 4,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("worker", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn("state", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("eff", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("fatigue", ImGuiTableColumnFlags_WidthStretch, 3.0f);
        ImGui::TableHeadersRow();
        for (const auto& w : snap.workers) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            TextMono("%s", w.name);
            ImGui::TableNextColumn();
            const ImVec4 stateCol =
                    w.state == demo::kWorkerMining ? Accent(0.9f) :
                    w.state == demo::kWorkerHauling ? ImVec4(0.98f, 0.81f, 0.26f, 1) :
                    w.state == demo::kWorkerResting ? ImVec4(0.55f, 0.58f, 0.64f, 1) :
                                                       ImVec4(0.71f, 0.74f, 0.78f, 1);
            ImGui::TextColored(stateCol, "%s", WorkerStateName(w.state));
            ImGui::TableNextColumn();
            TextMono("%.2f", w.efficiency);
            ImGui::TableNextColumn();
            ImGui::ProgressBar(w.fatigue, ImVec2(-1, 12), "");
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::SeparatorText("demo API");
    float eff = snap.workers[0].efficiency;
    if (ImGui::SliderFloat("worker efficiency", &eff, 0.4f, 1.0f, "%.2f"))
        demo::DemoGame::Instance().SetWorkerEfficiency(eff);
    bool fast = snap.settings.fastHaul != 0;
    if (ToggleSwitch("fast hauling", &fast))
        demo::DemoGame::Instance().SetFastHaul(fast);
}

// --------------------------------------------------------------- Economy ----
void App::DrawEconomyTab() {
    const float sc = density_ * settings().uiScale;
    Introspection& I = intro_;
    if (!I.hasSnapshot()) { ImGui::TextDisabled("no snapshot yet"); return; }
    const demo::Economy& e = I.snapshot().economy;

    if (g_fontMono) ImGui::PushFont(g_fontMono);
    ImGui::SetWindowFontScale(settings().uiScale * 1.6f);
    ImGui::TextColored(Accent(1.0f), "%.2f", e.coins);
    ImGui::SetWindowFontScale(settings().uiScale);
    if (g_fontMono) ImGui::PopFont();
    ImGui::TextDisabled("coins");

    ValueRow("gems", "%d", e.gems);
    ValueRow("income / sec", "%.2f", e.incomePerSec);
    ValueRow("pending (uncollected)", "%.2f", e.pendingIncome);

    ImGui::Spacing();
    ImGui::PlotLines("##coinhist", HistoryGetter,
                     const_cast<std::array<float, Introspection::kHistoryLen>*>(
                             &I.history().coins),
                     Introspection::kHistoryLen, 0, "coin balance history",
                     0.0f, FLT_MAX, ImVec2(-1, 64 * sc));

    ImGui::Spacing();
    ImGui::SeparatorText("demo API");
    if (ImGui::Button("collect pending income"))
        demo::DemoGame::Instance().CollectPendingIncome();
    ImGui::Spacing();
    float ts = I.snapshot().settings.timeScale;
    if (ImGui::SliderFloat("time scale", &ts, 0.25f, 4.0f, "%.2fx"))
        demo::DemoGame::Instance().SetTimeScale(ts);

    ImGui::Spacing();
    ImGui::TextDisabled("note: the overlay never pokes these numbers into");
    ImGui::TextDisabled("memory — changes flow through the game's own code,");
    ImGui::TextDisabled("which is how a real debug menu works.");
}

// ------------------------------------------------------------------ Misc ----
void App::DrawMiscTab() {
    const float sc = density_ * settings().uiScale;
    Introspection& I = intro_;
    const Diagnostics& d = intro_.diag();

    // ---- engine status card --------------------------------------------------
    if (ImGui::CollapsingHeader("introspection engine", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("force rescan")) I.RequestRescan();
        ImGui::SameLine();
        ImGui::TextDisabled("maps re-read every 5 s; re-validation every 2 s");
        ValueRow("pid / cmdline", "%d  %s", d.pid, d.cmdline.c_str());
        ValueRow("maps regions", "%zu", d.regionCount);
        ValueRow("scan cycles", "%" PRIu64, d.scanCycles);
        ValueRow("last scan coverage", "%zu regions, %.2f MB writable",
                 d.scannedRegions, (double)d.scannedBytes / (1024.0 * 1024.0));
        ValueRow("candidates rejected", "%" PRIu64, d.candidatesSeen);
        ValueRow("snapshots ok / torn", "%" PRIu64 " / %" PRIu64,
                 d.snapshotsOk, d.snapshotsTorn);
        ValueRow("failed reads", "%" PRIu64, d.readsFailed);
        ValueRow("process_vm_readv(self)", "%" PRId64 " bytes (syscall smoke test)",
                 d.processVmSelfTest);
        ImGui::Spacing();
        ImGui::TextDisabled("metadata <-> compiled-struct validation:");
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImVec4(0.42f, 0.90f, 0.48f, 1.0f));
        ImGui::TextWrapped("%s", d.layoutValidation.c_str());
        ImGui::PopStyleColor();
    }

    // ---- IL2CPP metadata viewer ----------------------------------------------
    if (ImGui::CollapsingHeader("IL2CPP-style metadata (parsed in-process)",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
        const auto& classes = I.metadata().classes();
        ImGui::TextDisabled("%zu classes from a v%d global-metadata blob",
                            classes.size(), I.metadata().version());
        static char filter[64] = "";
        ImGui::InputText("filter##meta", filter, sizeof(filter));
        if (ImGui::BeginChild("##metatree", ImVec2(0, 180 * sc),
                              ImGuiChildFlags_Border)) {
            for (const auto& c : classes) {
                if (filter[0] != '\0' &&
                    c.name.find(filter) == std::string::npos) continue;
                if (ImGui::TreeNode(c.name.c_str(), "%s  (%zu fields)",
                                    c.name.c_str(), c.fields.size())) {
                    for (const auto& f : c.fields) {
                        TextMono("%-16s off=%-4d size=%-3d tok=0x%08X",
                                 f.name.c_str(), f.offset, f.size, f.token);
                    }
                    ImGui::TreePop();
                }
            }
            ImGui::EndChild();
        }
    }

    // ---- live /proc/self/maps viewer ------------------------------------------
    if (ImGui::CollapsingHeader("/proc/self/maps (live region list)",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
        const auto& maps = I.maps();
        static char mfilter[64] = "";
        ImGui::InputText("filter##maps", mfilter, sizeof(mfilter));
        ImGui::TextDisabled("%zu regions", maps.size());
        if (ImGui::BeginChild("##maplist", ImVec2(0, 180 * sc),
                              ImGuiChildFlags_Border)) {
            ImGuiListClipper clipper;
            clipper.Begin((int)maps.size());
            while (clipper.Step()) {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                    const mem::MemoryRegion& r = maps[(size_t)i];
                    if (mfilter[0] != '\0' &&
                        r.pathname.find(mfilter) == std::string::npos &&
                        std::to_string(r.start).find(mfilter) == std::string::npos)
                        continue;
                    const bool anon = r.IsAnonymous();
                    ImGui::TextColored(
                            anon ? ImVec4(0.55f, 0.58f, 0.64f, 1) : Accent(0.75f),
                            "%08" PRIx64 "-%08" PRIx64 " %s %8.1fK %s",
                            r.start, r.end, r.perms, (double)r.Size() / 1024.0,
                            r.pathname.empty() ? "[anon]" : r.pathname.c_str());
                }
            }
            ImGui::EndChild();
        }
    }

    // ---- overlay UI settings (persisted as JSON by the Java service) ----------
    if (ImGui::CollapsingHeader("overlay settings (persisted to settings.json)",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
        Settings s = settings();
        if (ImGui::SliderFloat("ui scale", &s.uiScale, 0.70f, 1.60f, "%.2fx"))
            PersistSetting(kKeyUiScale, true, s.uiScale);
        if (ImGui::SliderFloat("glass opacity", &s.bgOpacity, 0.40f, 0.95f, "%.2f"))
            PersistSetting(kKeyBgOpacity, true, s.bgOpacity);
        if (ToggleSwitch("show fps", &s.showFps))
            PersistSetting(kKeyShowFps, false, s.showFps ? 1.0f : 0.0f);
    }

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.48f, 0.54f, 1.0f));
    ImGui::TextWrapped("Everything in this panel is read from this app's own "
                       "process. No other process is ever inspected — reading "
                       "another app's memory requires privileges a normal app "
                       "doesn't have, and doing it to software you don't own "
                       "is where learning ends and tampering begins.");
    ImGui::PopStyleColor();
}

} // namespace overlay
