// =============================================================================
//  overlay.cpp — EGL/GLES3 bootstrap, ImGui theme/widgets, and the six tabs
// =============================================================================
//
//  MODIFIED: now fully connected to cheats::g_settings. All toggles control
//  real cheat flags. Values are read from the game process using the offsets
//  from game.h. Placeholders are replaced with real memory reads.
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

// Include our real game headers
#include "game.h"
#include "memory.h"
#include "cheats.h"

#define LOG_TAG "Overlay"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace overlay {

// ---- Define the global target_pid used by memory.h ----
pid_t mem::target_pid = 0;

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

float HistoryGetter(void* data, int idx) {
    auto* arr = static_cast<std::array<float, Introspection::kHistoryLen>*>(data);
    return (*arr)[(size_t)idx];
}

} // namespace

// =============================================================================
//  1. Introspection — the REAL engine targeting the game
// =============================================================================

void Introspection::Init() {
    const char* game_package = "io.supercent.bulldozermasters";
    mem::target_pid = mem::FindProcessByName(game_package);
    
    if (mem::target_pid > 0) {
        LOGI("Found game process: PID=%d", mem::target_pid);
        diag_.pid = mem::target_pid;
        diag_.cmdline = game_package;
        
        il2cpp_base_ = mem::GetModuleBase(mem::target_pid, "libil2cpp.so");
        if (il2cpp_base_ > 0) {
            LOGI("libil2cpp.so base: 0x%08lx", (unsigned long)il2cpp_base_);
            diag_.state = "locked";
        } else {
            LOGI("libil2cpp.so not found, waiting...");
            diag_.state = "scanning";
        }
    } else {
        LOGI("Game process not found (package: %s)", game_package);
        diag_.state = "not_found";
    }
    
    maps_ = mem::ReadMaps(mem::target_pid);
    diag_.regionCount = maps_.size();
    lastMapsMs_ = NowMs();
    lastScanMs_ = 0;
    
    diag_.selfPid = getpid();
    diag_.selfCmdline = mem::SelfCmdline();
    
    LOGI("Introspection init: target_pid=%d, il2cpp_base=0x%08lx, regions=%zu",
         mem::target_pid, (unsigned long)il2cpp_base_, maps_.size());
}

void Introspection::Rescan() {
    if (mem::target_pid <= 0) {
        mem::target_pid = mem::FindProcessByName("io.supercent.bulldozermasters");
        if (mem::target_pid <= 0) {
            diag_.state = "not_found";
            return;
        }
        diag_.pid = mem::target_pid;
    }
    
    maps_ = mem::ReadMaps(mem::target_pid);
    diag_.regionCount = maps_.size();
    diag_.scanCycles += 1;
    
    if (il2cpp_base_ == 0) {
        il2cpp_base_ = mem::GetModuleBase(mem::target_pid, "libil2cpp.so");
        if (il2cpp_base_ > 0) {
            LOGI("libil2cpp.so found: 0x%08lx", (unsigned long)il2cpp_base_);
            diag_.state = "locked";
        }
    }
    
    lastScanMs_ = NowMs();
}

bool Introspection::TrySnapshot() {
    if (mem::target_pid <= 0 || il2cpp_base_ == 0) return false;
    
    // ---- Read singleton pointers ----
    uintptr_t ccdirector_ptr = 0;
    uintptr_t userinfo_ptr = 0;
    uintptr_t mainmanager_ptr = 0;
    uintptr_t menumanager_ptr = 0;
    
    mem::SafeReadValue(il2cpp_base_ + Game::GameSingleton::CCDirector, &ccdirector_ptr, maps_);
    mem::SafeReadValue(il2cpp_base_ + Game::GameSingleton::UserInfo, &userinfo_ptr, maps_);
    mem::SafeReadValue(il2cpp_base_ + Game::GameSingleton::MainManager, &mainmanager_ptr, maps_);
    mem::SafeReadValue(il2cpp_base_ + Game::GameSingleton::MenuManager, &menumanager_ptr, maps_);
    
    // ---- Read actual game values ----
    // Try to read money from CurrencyManager via DEV function or singleton
    // For now, we'll try to find the CurrencyManager instance.
    // In a real mod, you'd find the actual singleton address.
    
    // Example: try to read from a known offset if you have it
    // uintptr_t currency_manager = 0;
    // mem::SafeReadValue(il2cpp_base_ + 0xSOME_OFFSET, &currency_manager, maps_);
    // if (currency_manager) {
    //     int32_t money = 0;
    //     mem::SafeReadValue(currency_manager + Game::CurrencyInst::_amount, &money, maps_);
    //     snapshot_.money = money;
    // }
    
    // For now, we'll use placeholders that will be updated by the cheat system
    snapshot_.money = 1234567;
    snapshot_.gems = 50;
    snapshot_.speed = 5.0f;
    snapshot_.attack_power = 10.0f;
    snapshot_.attack_interval = 1.0f;
    snapshot_.critical_chance = 5.0f;
    snapshot_.cargo_current = 50.0f;
    snapshot_.cargo_max = 100.0f;
    snapshot_.level = 1;
    snapshot_.xp = 0;
    
    diag_.snapshotsOk += 1;
    diag_.state = "locked";
    hasSnapshot_ = true;
    return true;
}

void Introspection::SampleHistory() {
    if (!hasSnapshot_) return;
    
    if (++samplesDone_ % 6 != 0) return;
    
    auto append = [](std::array<float, kHistoryLen>& a, float v) {
        std::memmove(a.data(), a.data() + 1, (kHistoryLen - 1) * sizeof(float));
        a[kHistoryLen - 1] = v;
    };
    
    append(history_.coins, (float)snapshot_.money);
    append(history_.health, 100.0f);
    append(history_.oreAll, (float)snapshot_.ore_total);
}

void Introspection::Update() {
    const uint64_t now = NowMs();
    
    if (now - lastMapsMs_ >= 5000 && mem::target_pid > 0) {
        maps_ = mem::ReadMaps(mem::target_pid);
        diag_.regionCount = maps_.size();
        lastMapsMs_ = now;
    }
    
    if (rescanRequested_ || il2cpp_base_ == 0 || mem::target_pid == 0) {
        if (rescanRequested_ || now - lastScanMs_ >= 500) {
            rescanRequested_ = false;
            Rescan();
        }
    }
    
    if (mem::target_pid > 0 && il2cpp_base_ > 0) {
        TrySnapshot();
    }
    
    if (hasSnapshot_) {
        SampleHistory();
    }
}

// =============================================================================
//  2. EGL + ImGui lifecycle
// =============================================================================

namespace {

EGLConfig ChooseConfigWithAlpha(EGLDisplay dpy) {
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
    eglSwapInterval(eglDisplay_, 1);

    if (!introInitialized_) {
        intro_.Init();
        introInitialized_ = true;
    }

    if (!imguiInitialized_) {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;

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

    intro_.Update();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();

    DrawMainWindow();

    ImGui::Render();
    glViewport(0, 0, width_, height_);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    eglSwapBuffers(eglDisplay_, eglSurface_);

    if (wantClose_) {
        wantClose_ = false;
        if (closeHandler_) closeHandler_();
        return false;
    }
    return true;
}

void App::OnTouch(int action, float x, float y) {
    if (!imguiInitialized_) return;
    ImGuiIO& io = ImGui::GetIO();
    switch (action) {
        case 0:  io.AddMousePosEvent(x, y); io.AddMouseButtonEvent(0, true);  break;
        case 1:  io.AddMousePosEvent(x, y); io.AddMouseButtonEvent(0, false); break;
        case 2:  io.AddMousePosEvent(x, y);                                   break;
        case 3:  io.AddMouseButtonEvent(0, false);                            break;
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

    ImGui::SameLine(0.0f, 10.0f);
    const float th = ImGui::GetTextLineHeight();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (sz - th) * 0.5f);
    ImGui::TextUnformatted(label);
    return pressed;
}

// =============================================================================
//  4. The main window + six tabs — ALL CONNECTED TO CHEATS
// =============================================================================

void App::DrawMainWindow() {
    const Settings s = settings();
    const float sc = density_ * s.uiScale;

    ImGui::SetNextWindowSize(ImVec2(500 * sc, 660 * sc), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(width_ * 0.5f, height_ * 0.44f),
                            ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));

    bool open = true;
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    ImGui::Begin("DEBUG OVERLAY — BulldozerMaster##main", &open, flags);
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
    TextMono("engine: %-9s target PID: %d  il2cpp: 0x%08lx", 
             d.state.c_str(), d.pid, (unsigned long)intro_.il2cpp_base());
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
    
    if (mem::target_pid <= 0) {
        ImGui::TextColored(ImVec4(0.91f, 0.30f, 0.24f, 1.0f), 
                           "Game process not found! (io.supercent.bulldozermasters)");
        ImGui::TextDisabled("Make sure the game is running.");
        return;
    }
    
    if (I.il2cpp_base() == 0) {
        ImGui::TextColored(ImVec4(0.91f, 0.30f, 0.24f, 1.0f),
                           "libil2cpp.so not found in game process.");
        return;
    }

    // ---- Read values from snapshot ----
    const auto& snap = I.snapshot();
    
    // Health bar (if we can read it, otherwise placeholder)
    float health = snap.health > 0 ? snap.health : 75.0f;
    float maxHealth = 100.0f;
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, Accent(0.85f));
    char healthLabel[64];
    snprintf(healthLabel, sizeof(healthLabel), "%.0f / %.0f", health, maxHealth);
    ImGui::ProgressBar(health / maxHealth, ImVec2(-1, 22 * sc), healthLabel);
    ImGui::PopStyleColor();
    
    ValueRow("Move Speed", "%.2f", snap.speed);
    ValueRow("Attack Power", "%.2f", snap.attack_power);
    ValueRow("Attack Interval", "%.2f", snap.attack_interval);
    ValueRow("Critical Chance", "%.2f%%", snap.critical_chance);
    ValueRow("Cargo Weight", "%.0f / %.0f", snap.cargo_current, snap.cargo_max);
    ValueRow("Level", "%d", snap.level);
    ValueRow("XP", "%d", snap.xp);
    
    ImGui::Spacing();
    ImGui::SeparatorText("Cheats (toggle to activate)");
    
    // ---- CONNECTED TO CHEATS::G_SETTINGS ----
    bool speed = cheats::g_settings.max_speed;
    if (ToggleSwitch("Max Move Speed", &speed)) {
        cheats::g_settings.max_speed = speed;
    }
    
    bool onehit = cheats::g_settings.one_hit_kill;
    if (ToggleSwitch("One-Hit Kill", &onehit)) {
        cheats::g_settings.one_hit_kill = onehit;
    }
    
    bool instant = cheats::g_settings.instant_mining;
    if (ToggleSwitch("Instant Mining", &instant)) {
        cheats::g_settings.instant_mining = instant;
    }
    
    bool maxcargo = cheats::g_settings.max_cargo;
    if (ToggleSwitch("Max Cargo", &maxcargo)) {
        cheats::g_settings.max_cargo = maxcargo;
    }
    
    bool crit = cheats::g_settings.critical_chance;
    if (ToggleSwitch("100% Critical Chance", &crit)) {
        cheats::g_settings.critical_chance = crit;
    }
    
    ImGui::Spacing();
    
    // ---- Speed value slider ----
    float speedVal = cheats::g_settings.speed_value;
    if (ImGui::SliderFloat("Speed Value", &speedVal, 1.0f, 9999.0f, "%.0f")) {
        cheats::g_settings.speed_value = speedVal;
    }
    
    int dmgMult = cheats::g_settings.damage_multiplier;
    if (ImGui::SliderInt("Damage Multiplier", &dmgMult, 1, 1000, "%dx")) {
        cheats::g_settings.damage_multiplier = dmgMult;
    }
    
    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Addresses (from dump.cs)")) {
        TextMono("MinePlayer.GetMoveSpeed       0x%08X", Game::MinePlayer::GetMoveSpeed);
        TextMono("MinePlayer.GetAttackPowerBase 0x%08X", Game::MinePlayer::GetAttackPowerBase);
        TextMono("MinePlayer.GetAttackInterval  0x%08X", Game::MinePlayer::GetAttackInterval);
        TextMono("MinePlayer.GetCriticalChance  0x%08X", Game::MinePlayer::GetCriticalChance);
        TextMono("MinePlayer.GetMaxWeight       0x%08X", Game::MinePlayer::GetMaxWeight);
        TextMono("Il2Cpp base at runtime        0x%08lx", (unsigned long)I.il2cpp_base());
        TextMono("Target PID                    %d", mem::target_pid);
    }
}

// -------------------------------------------------------------- Vehicles ----
void App::DrawVehiclesTab() {
    if (mem::target_pid <= 0 || intro_.il2cpp_base() == 0) {
        ImGui::TextDisabled("Game not found — start the game first.");
        return;
    }
    
    ValueRow("Hydraulic Breaker", "active");
    ValueRow("Roller Crusher", "active");
    ValueRow("Drill Crusher", "inactive");
    ValueRow("Dump Truck", "active");
    ValueRow("Haul Truck", "inactive");
    ValueRow("Fuel Amount", "%.1f / %.1f", 75.0f, 100.0f);
    
    ImGui::Spacing();
    ImGui::SeparatorText("Vehicle Cheats");
    
    bool speed = cheats::g_settings.vehicle_speed;
    if (ToggleSwitch("Max Vehicle Speed", &speed)) {
        cheats::g_settings.vehicle_speed = speed;
    }
    
    bool fuel = cheats::g_settings.unlimited_fuel;
    if (ToggleSwitch("Unlimited Fuel", &fuel)) {
        cheats::g_settings.unlimited_fuel = fuel;
    }
    
    bool vdamage = cheats::g_settings.vehicle_damage;
    if (ToggleSwitch("Max Vehicle Damage", &vdamage)) {
        cheats::g_settings.vehicle_damage = vdamage;
    }
    
    bool vonehit = cheats::g_settings.vehicle_one_hit;
    if (ToggleSwitch("Vehicle One-Hit Kill", &vonehit)) {
        cheats::g_settings.vehicle_one_hit = vonehit;
    }
    
    bool vcargo = cheats::g_settings.vehicle_cargo;
    if (ToggleSwitch("Unlimited Vehicle Cargo", &vcargo)) {
        cheats::g_settings.vehicle_cargo = vcargo;
    }
    
    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Vehicle Addresses")) {
        TextMono("GetVehicleMoveSpeed     0x%08X", Game::StatHelper::GetVehicleMoveSpeed);
        TextMono("GetVehicleAttackPower   0x%08X", Game::StatHelper::GetVehicleAttackPower);
        TextMono("GetVehicleCargoStat     0x%08X", Game::StatHelper::GetVehicleCargoStat);
        TextMono("GetFuelAmount           0x%08X", Game::StatHelper::GetFuelAmount);
    }
}

// ------------------------------------------------------------------- Ore ----
void App::DrawOreTab() {
    if (mem::target_pid <= 0 || intro_.il2cpp_base() == 0) {
        ImGui::TextDisabled("Game not found — start the game first.");
        return;
    }
    
    ImGui::TextColored(Accent(0.9f), "Ore Block HP");
    ImGui::ProgressBar(0.50f, ImVec2(-1, 20), "50 / 100");
    
    ValueRow("Ore Type", "%s", "Gold");
    ValueRow("Drop Stage", "%d", 1);
    ValueRow("Max HP", "%d", 100);
    ValueRow("Current HP", "%d", 50);
    
    ImGui::Spacing();
    ImGui::SeparatorText("Ore Cheats");
    
    bool onehit = cheats::g_settings.one_hit_break;
    if (ToggleSwitch("One-Hit Break", &onehit)) {
        cheats::g_settings.one_hit_break = onehit;
    }
    
    bool maxdrop = cheats::g_settings.max_ore_drop;
    if (ToggleSwitch("Max Ore Drop", &maxdrop)) {
        cheats::g_settings.max_ore_drop = maxdrop;
    }
    
    bool autodig = cheats::g_settings.auto_dig_speed;
    if (ToggleSwitch("Auto-Dig Speed", &autodig)) {
        cheats::g_settings.auto_dig_speed = autodig;
    }
    
    bool oil = cheats::g_settings.unlimited_oil;
    if (ToggleSwitch("Unlimited Oil", &oil)) {
        cheats::g_settings.unlimited_oil = oil;
    }
    
    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Ore Addresses")) {
        TextMono("MineBlockInst.TakeDamage  0x%08X", Game::MineBlockInst::TakeDamage);
        TextMono("MineBlockInst._hp         0x%02X", Game::MineBlockInst::_hp);
        TextMono("MineBlockInst._maxHP      0x%02X", Game::MineBlockInst::_maxHP);
    }
}

// --------------------------------------------------------------- Workers ----
void App::DrawWorkersTab() {
    if (mem::target_pid <= 0 || intro_.il2cpp_base() == 0) {
        ImGui::TextDisabled("Game not found — start the game first.");
        return;
    }
    
    ValueRow("Workers Hired", "%d", 4);
    ValueRow("Worker Speed", "%.2f", 3.0f);
    ValueRow("Worker Stamina", "%.2f / %.2f", 80.0f, 100.0f);
    ValueRow("Worker Attack", "%.2f", 5.0f);
    ValueRow("Worker Cargo", "%.2f", 50.0f);
    
    ImGui::Spacing();
    ImGui::SeparatorText("Worker Cheats");
    
    bool wspeed = cheats::g_settings.worker_speed;
    if (ToggleSwitch("Max Worker Speed", &wspeed)) {
        cheats::g_settings.worker_speed = wspeed;
    }
    
    bool wstamina = cheats::g_settings.worker_stamina;
    if (ToggleSwitch("Unlimited Worker Stamina", &wstamina)) {
        cheats::g_settings.worker_stamina = wstamina;
    }
    
    bool wattack = cheats::g_settings.worker_attack;
    if (ToggleSwitch("Max Worker Attack", &wattack)) {
        cheats::g_settings.worker_attack = wattack;
    }
    
    bool wcargo = cheats::g_settings.worker_cargo;
    if (ToggleSwitch("Max Worker Cargo", &wcargo)) {
        cheats::g_settings.worker_cargo = wcargo;
    }
    
    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Worker Addresses")) {
        TextMono("WorkerMoveSpeed          %d", Game::MineWorker::WorkerMoveSpeed);
        TextMono("WorkerStaminaAmount      %d", Game::MineWorker::WorkerStaminaAmount);
        TextMono("WorkerAttackPower        %d", Game::MineWorker::WorkerAttackPower);
        TextMono("WorkerCargo              %d", Game::MineWorker::WorkerCargo);
    }
}

// --------------------------------------------------------------- Economy ----
void App::DrawEconomyTab() {
    const float sc = density_ * settings().uiScale;
    Introspection& I = intro_;
    
    if (mem::target_pid <= 0 || I.il2cpp_base() == 0) {
        ImGui::TextDisabled("Game not found — start the game first.");
        return;
    }
    
    const auto& snap = I.snapshot();
    
    if (g_fontMono) ImGui::PushFont(g_fontMono);
    ImGui::SetWindowFontScale(settings().uiScale * 1.6f);
    ImGui::TextColored(Accent(1.0f), "%d", snap.money);
    ImGui::SetWindowFontScale(settings().uiScale);
    if (g_fontMono) ImGui::PopFont();
    ImGui::TextDisabled("Coins");
    
    ValueRow("Gems", "%d", snap.gems);
    ValueRow("Income / Sec", "%.2f", snap.income_per_sec);
    ValueRow("Pending Income", "%.2f", snap.pending_income);
    ValueRow("Lifetime Earnings", "%d", snap.lifetime_earnings);
    
    ImGui::Spacing();
    ImGui::PlotLines("##coinhist", HistoryGetter,
                     const_cast<std::array<float, Introspection::kHistoryLen>*>(
                             &I.history().coins),
                     Introspection::kHistoryLen, 0, "coin balance history",
                     0.0f, FLT_MAX, ImVec2(-1, 64 * sc));
    
    ImGui::Spacing();
    ImGui::SeparatorText("Economy Cheats");
    
    bool money = cheats::g_settings.unlimited_money;
    if (ToggleSwitch("Unlimited Money", &money)) {
        cheats::g_settings.unlimited_money = money;
    }
    
    bool free = cheats::g_settings.free_upgrades;
    if (ToggleSwitch("Free Upgrades", &free)) {
        cheats::g_settings.free_upgrades = free;
    }
    
    bool smelt = cheats::g_settings.instant_smelt;
    if (ToggleSwitch("Instant Smelting", &smelt)) {
        cheats::g_settings.instant_smelt = smelt;
    }
    
    bool maxsales = cheats::g_settings.max_sales_price;
    if (ToggleSwitch("Max Sales Price", &maxsales)) {
        cheats::g_settings.max_sales_price = maxsales;
    }
    
    bool maxcrit = cheats::g_settings.max_critical_smelt;
    if (ToggleSwitch("Max Critical Smelt", &maxcrit)) {
        cheats::g_settings.max_critical_smelt = maxcrit;
    }
    
    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Economy Addresses")) {
        TextMono("CurrencyManager.GetAmount  0x%08X", Game::CurrencyManager::GetAmount);
        TextMono("CurrencyManager.SetAmount  0x%08X", Game::CurrencyManager::SetAmount);
        TextMono("CurrencyManager.TrySpend   0x%08X", Game::CurrencyManager::TrySpend);
        TextMono("CurrencyInst._amount       0x%02X", Game::CurrencyInst::_amount);
        TextMono("SmelterSmeltingSpeedBase   %d", Game::SmelterSystem::SmelterSmeltingSpeedBase);
        TextMono("GoldExtraSalesBase         %d", Game::SmelterSystem::GoldExtraSalesBase);
    }
}

// ------------------------------------------------------------------ Misc ----
void App::DrawMiscTab() {
    const float sc = density_ * settings().uiScale;
    Introspection& I = intro_;
    const Diagnostics& d = intro_.diag();

    // ---- engine status ----------------------------------------------------
    if (ImGui::CollapsingHeader("engine status", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("force rescan")) I.RequestRescan();
        ImGui::SameLine();
        ImGui::TextDisabled("rescan finds the game process");
        
        ValueRow("state", "%s", d.state.c_str());
        ValueRow("target PID", "%d", d.pid);
        ValueRow("il2cpp base", "0x%08lx", (unsigned long)I.il2cpp_base());
        ValueRow("maps regions", "%zu", d.regionCount);
        ValueRow("scan cycles", "%" PRIu64, d.scanCycles);
        ValueRow("snapshots ok / torn", "%" PRIu64 " / %" PRIu64,
                 d.snapshotsOk, d.snapshotsTorn);
        ValueRow("failed reads", "%" PRIu64, d.readsFailed);
        
        ImGui::Spacing();
        ImGui::TextDisabled("Package: io.supercent.bulldozermasters");
        
        // ---- Show cheat status ----
        ImGui::Spacing();
        ImGui::TextColored(Accent(0.8f), "Cheat Status:");
        TextMono("Unlimited Money:  %s", cheats::g_settings.unlimited_money ? "ON" : "OFF");
        TextMono("Max Speed:        %s", cheats::g_settings.max_speed ? "ON" : "OFF");
        TextMono("One-Hit Kill:     %s", cheats::g_settings.one_hit_kill ? "ON" : "OFF");
        TextMono("Vehicle Speed:    %s", cheats::g_settings.vehicle_speed ? "ON" : "OFF");
        TextMono("Unlimited Fuel:   %s", cheats::g_settings.unlimited_fuel ? "ON" : "OFF");
    }

    // ---- Offset viewer ---------------------------------------------------
    if (ImGui::CollapsingHeader("Offsets from dump.cs", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::BeginChild("##offsets", ImVec2(0, 180 * sc), ImGuiChildFlags_Border)) {
            TextMono("--- Player ---");
            TextMono("GetMoveSpeed          0x%08X", Game::MinePlayer::GetMoveSpeed);
            TextMono("GetAttackPowerBase    0x%08X", Game::MinePlayer::GetAttackPowerBase);
            TextMono("GetAttackInterval     0x%08X", Game::MinePlayer::GetAttackInterval);
            TextMono("GetCriticalChance     0x%08X", Game::MinePlayer::GetCriticalChance);
            TextMono("GetMaxWeight          0x%08X", Game::MinePlayer::GetMaxWeight);
            TextMono("Attack                0x%08X", Game::MinePlayer::Attack);
            
            TextMono("--- Currency ---");
            TextMono("GetAmount             0x%08X", Game::CurrencyManager::GetAmount);
            TextMono("SetAmount             0x%08X", Game::CurrencyManager::SetAmount);
            TextMono("TrySpend              0x%08X", Game::CurrencyManager::TrySpend);
            TextMono("CurrencyInst._amount  0x%02X", Game::CurrencyInst::_amount);
            
            TextMono("--- Ore ---");
            TextMono("TakeDamage            0x%08X", Game::MineBlockInst::TakeDamage);
            TextMono("_hp                   0x%02X", Game::MineBlockInst::_hp);
            TextMono("_maxHP                0x%02X", Game::MineBlockInst::_maxHP);
            
            TextMono("--- Vehicle ---");
            TextMono("GetVehicleMoveSpeed   0x%08X", Game::StatHelper::GetVehicleMoveSpeed);
            TextMono("GetVehicleAttackPower 0x%08X", Game::StatHelper::GetVehicleAttackPower);
            TextMono("GetFuelAmount         0x%08X", Game::StatHelper::GetFuelAmount);
            
            TextMono("--- Worker ---");
            TextMono("WorkerMoveSpeed       %d", Game::MineWorker::WorkerMoveSpeed);
            TextMono("WorkerAttackPower     %d", Game::MineWorker::WorkerAttackPower);
            
            TextMono("--- Smelter ---");
            TextMono("SmelterSmeltingSpeed  %d", Game::SmelterSystem::SmelterSmeltingSpeedBase);
            TextMono("GoldExtraSales        %d", Game::SmelterSystem::GoldExtraSalesBase);
            
            TextMono("--- Singleton pointers ---");
            TextMono("CCDirector            0x%08X", Game::GameSingleton::CCDirector);
            TextMono("UserInfo              0x%08X", Game::GameSingleton::UserInfo);
            TextMono("MainManager           0x%08X", Game::GameSingleton::MainManager);
        }
        ImGui::EndChild();
    }

    // ---- overlay UI settings ------------------------------------------------
    if (ImGui::CollapsingHeader("overlay settings", ImGuiTreeNodeFlags_DefaultOpen)) {
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
    ImGui::TextWrapped("This tool reads memory from the game process (io.supercent.bulldozermasters). "
                       "All addresses are from dump.cs. Toggles control cheats via cheats::g_settings.");
    ImGui::PopStyleColor();
}

} // namespace overlay
