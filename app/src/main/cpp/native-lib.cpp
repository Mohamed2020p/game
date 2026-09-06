// =============================================================================
//  native-lib.cpp — JNI bridge between Java overlay service and the C++
//                   real game memory system
// =============================================================================
//
//  Thread map:
//    Java UI thread     : touch events, surface create/destroy, settings I/O
//    C++ render thread  : owns EGL context + ImGui context, runs Frame()
//    C++ scanner thread : finds the game process and libil2cpp.so base
//
//  All demo_game references have been removed. This now targets the REAL
//  game (io.supercent.bulldozermasters) using the offsets from game.h.
// =============================================================================

#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

#include "overlay.h"
#include "memory.h"
#include "game.h"
#include "cheats.h"

#define LOG_TAG "NativeLib"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

overlay::App g_app;                       // engine + ImGui app (process-wide)

std::thread g_renderThread;               // owns EGL + ImGui while a surface lives
std::atomic<bool> g_renderRunning{false};
std::mutex g_renderMutex;                 // serializes start/stop of the thread
std::condition_variable g_renderCv;

std::thread g_scannerThread;              // scans for the game process
std::atomic<bool> g_scannerRunning{false};
std::atomic<bool> g_gameFound{false};

JavaVM* g_vm = nullptr;                   // cached in JNI_OnLoad
jobject g_callbackTarget = nullptr;       // global ref to DebugService
jmethodID g_onSettingChanged = nullptr;   // (String key, boolean isFloat, float value)V
jmethodID g_onPanelClosed = nullptr;      // ()V
jmethodID g_onGameFound = nullptr;        // ()V — notify Java that game was found
std::mutex g_callbackMutex;

// ---- Package name of the target game ----
const char* TARGET_PACKAGE = "io.supercent.bulldozermasters";

// -----------------------------------------------------------------------------
// Scoped JNI environment for callbacks from native threads.
// -----------------------------------------------------------------------------
struct JniAttach {
    JNIEnv* env = nullptr;
    bool attachedHere = false;

    explicit JniAttach() {
        if (!g_vm) return;
        if (g_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK)
            return;
        JavaVMAttachArgs args{kJNI_VERSION_1_6, const_cast<char*>("overlay-native"), nullptr};
        if (g_vm->AttachCurrentThread(&env, &args) == JNI_OK) attachedHere = true;
    }
    ~JniAttach() {
        if (attachedHere && g_vm) g_vm->DetachCurrentThread();
    }
};

void NotifySettingChanged(const char* key, bool isFloat, float value) {
    std::lock_guard<std::mutex> lock(g_callbackMutex);
    JniAttach att;
    if (!att.env || !g_callbackTarget || !g_onSettingChanged) return;
    jstring jkey = att.env->NewStringUTF(key);
    if (!jkey) return;
    att.env->CallVoidMethod(g_callbackTarget, g_onSettingChanged,
                            jkey, (jboolean)(isFloat ? 1 : 0), value);
    att.env->DeleteLocalRef(jkey);
    if (att.env->ExceptionCheck()) {
        LOGE("onNativeSettingChanged threw; clearing");
        att.env->ExceptionClear();
    }
}

void NotifyPanelClosed() {
    std::lock_guard<std::mutex> lock(g_callbackMutex);
    JniAttach att;
    if (!att.env || !g_callbackTarget || !g_onPanelClosed) return;
    att.env->CallVoidMethod(g_callbackTarget, g_onPanelClosed);
    if (att.env->ExceptionCheck()) {
        LOGE("onOverlayPanelClosed threw; clearing");
        att.env->ExceptionClear();
    }
}

void NotifyGameFound() {
    std::lock_guard<std::mutex> lock(g_callbackMutex);
    JniAttach att;
    if (!att.env || !g_callbackTarget || !g_onGameFound) return;
    att.env->CallVoidMethod(g_callbackTarget, g_onGameFound);
    if (att.env->ExceptionCheck()) {
        LOGE("onGameFound threw; clearing");
        att.env->ExceptionClear();
    }
}

// -----------------------------------------------------------------------------
// Scanner thread — finds the game process and libil2cpp.so
// -----------------------------------------------------------------------------
void ScannerThreadFunc() {
    LOGI("Scanner thread started, looking for %s", TARGET_PACKAGE);
    
    while (g_scannerRunning.load(std::memory_order_acquire)) {
        pid_t pid = mem::FindProcessByName(TARGET_PACKAGE);
        if (pid > 0) {
            mem::target_pid = pid;
            uintptr_t base = mem::GetModuleBase(pid, "libil2cpp.so");
            if (base > 0) {
                LOGI("Found game: PID=%d, libil2cpp.so base=0x%08lx", pid, (unsigned long)base);
                g_gameFound.store(true, std::memory_order_release);
                NotifyGameFound();
                // Keep running to monitor if the game exits
            } else {
                LOGI("Game found (PID=%d) but libil2cpp.so not loaded yet", pid);
            }
        } else {
            if (g_gameFound.load()) {
                // Game was running but disappeared
                g_gameFound.store(false, std::memory_order_release);
                mem::target_pid = 0;
                LOGI("Game process lost");
            }
        }
        
        // Scan every 2 seconds
        for (int i = 0; i < 20 && g_scannerRunning.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    LOGI("Scanner thread stopped");
}

// -----------------------------------------------------------------------------
// Render thread body
// -----------------------------------------------------------------------------
void RenderThreadFunc(ANativeWindow* window) {
    if (!g_app.Init(window)) {
        LOGE("overlay init failed on this surface");
        g_app.Shutdown();
        ANativeWindow_release(window);
        g_renderRunning.store(false, std::memory_order_release);
        return;
    }

    g_app.SetSettingsSink([](const char* key, bool isFloat, float value) {
        NotifySettingChanged(key, isFloat, value);
    });
    g_app.SetCloseHandler([] {
        NotifyPanelClosed();
    });

    LOGI("Render thread running");
    while (g_renderRunning.load(std::memory_order_acquire)) {
        // Apply cheats if game is found
        if (g_gameFound.load() && mem::target_pid > 0) {
            // intro_.Update() is called inside g_app.Frame()
            // Apply cheats after intro updates its state
        }
        if (!g_app.Frame()) break;
    }
    g_app.Shutdown();
    ANativeWindow_release(window);
    g_renderRunning.store(false, std::memory_order_release);
    LOGI("Render thread stopped");
}

bool StopRenderThread() {
    if (!g_renderRunning.load()) return false;
    g_renderRunning.store(false, std::memory_order_release);
    if (g_renderThread.joinable()) g_renderThread.join();
    return true;
}

bool StopScannerThread() {
    if (!g_scannerRunning.load()) return false;
    g_scannerRunning.store(false, std::memory_order_release);
    if (g_scannerThread.joinable()) g_scannerThread.join();
    return true;
}

// -----------------------------------------------------------------------------
// JNI methods
// -----------------------------------------------------------------------------
jmethodID FindMethodOrDie(JNIEnv* env, jclass clazz, const char* name, const char* sig) {
    jmethodID id = env->GetMethodID(clazz, name, sig);
    if (!id) {
        LOGE("method not found: %s %s", name, sig);
        env->FatalError("NativeBridge callback method missing");
    }
    return id;
}

// ---- Surface management ----
void Native_SetSurface(JNIEnv* env, jclass, jobject surface) {
    std::lock_guard<std::mutex> lock(g_renderMutex);
    StopRenderThread();
    if (!surface) return;

    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (!window) {
        LOGE("ANativeWindow_fromSurface returned null");
        return;
    }
    g_renderRunning.store(true, std::memory_order_release);
    g_renderThread = std::thread(RenderThreadFunc, window);
}

void Native_ClearSurface(JNIEnv*, jclass) {
    std::lock_guard<std::mutex> lock(g_renderMutex);
    StopRenderThread();
}

void Native_SurfaceChanged(JNIEnv*, jclass, jint w, jint h) {
    g_app.Resize(w, h);
}

// ---- Touch input ----
void Native_OnTouch(JNIEnv*, jclass, jint action, jfloat x, jfloat y) {
    g_app.OnTouch((int)action, x, y);
}

// ---- Density ----
void Native_SetDensity(JNIEnv*, jclass, jfloat d) {
    g_app.SetDensity(d);
}

// ---- Settings ----
void Native_SetSetting(JNIEnv* env, jclass, jstring key, jboolean isFloat, jfloat value) {
    const char* k = env->GetStringUTFChars(key, nullptr);
    if (!k) return;
    g_app.SetSetting(k, isFloat != JNI_FALSE, value);
    env->ReleaseStringUTFChars(key, k);
}

// ---- Callback target ----
void Native_SetCallbackTarget(JNIEnv* env, jclass, jobject target) {
    std::lock_guard<std::mutex> lock(g_callbackMutex);
    JniAttach att;
    if (g_callbackTarget) {
        att.env->DeleteGlobalRef(g_callbackTarget);
        g_callbackTarget = nullptr;
    }
    if (!target) return;
    g_callbackTarget = att.env->NewGlobalRef(target);
    jclass clazz = att.env->GetObjectClass(g_callbackTarget);
    g_onSettingChanged = FindMethodOrDie(
            att.env, clazz, "onNativeSettingChanged", "(Ljava/lang/String;ZF)V");
    g_onPanelClosed = FindMethodOrDie(att.env, clazz, "onOverlayPanelClosed", "()V");
    g_onGameFound = FindMethodOrDie(att.env, clazz, "onGameFound", "()V");
    att.env->DeleteLocalRef(clazz);
    LOGI("callback target registered");
}

// ---- Scanner control ----
void Native_StartScanner(JNIEnv*, jclass) {
    if (g_scannerRunning.load()) return;
    g_scannerRunning.store(true, std::memory_order_release);
    g_scannerThread = std::thread(ScannerThreadFunc);
    LOGI("Scanner started");
}

void Native_StopScanner(JNIEnv*, jclass) {
    StopScannerThread();
    LOGI("Scanner stopped");
}

void Native_ForceRescan(JNIEnv*, jclass) {
    // Trigger a rescan by resetting the found flag and forcing the scanner to check
    g_gameFound.store(false, std::memory_order_release);
    mem::target_pid = 0;
    // The scanner will find it on its next iteration
    LOGI("Force rescan triggered");
}

// ---- Cheat settings ----
void Native_SetCheat(JNIEnv* env, jclass, jstring key, jboolean value) {
    const char* k = env->GetStringUTFChars(key, nullptr);
    if (!k) return;
    
    // Update the cheat settings
    if (strcmp(k, "unlimited_money") == 0) cheats::g_settings.unlimited_money = (value != JNI_FALSE);
    else if (strcmp(k, "free_upgrades") == 0) cheats::g_settings.free_upgrades = (value != JNI_FALSE);
    else if (strcmp(k, "max_speed") == 0) cheats::g_settings.max_speed = (value != JNI_FALSE);
    else if (strcmp(k, "one_hit_kill") == 0) cheats::g_settings.one_hit_kill = (value != JNI_FALSE);
    else if (strcmp(k, "instant_mining") == 0) cheats::g_settings.instant_mining = (value != JNI_FALSE);
    else if (strcmp(k, "max_cargo") == 0) cheats::g_settings.max_cargo = (value != JNI_FALSE);
    else if (strcmp(k, "critical_chance") == 0) cheats::g_settings.critical_chance = (value != JNI_FALSE);
    else if (strcmp(k, "vehicle_speed") == 0) cheats::g_settings.vehicle_speed = (value != JNI_FALSE);
    else if (strcmp(k, "unlimited_fuel") == 0) cheats::g_settings.unlimited_fuel = (value != JNI_FALSE);
    else if (strcmp(k, "vehicle_damage") == 0) cheats::g_settings.vehicle_damage = (value != JNI_FALSE);
    else if (strcmp(k, "vehicle_one_hit") == 0) cheats::g_settings.vehicle_one_hit = (value != JNI_FALSE);
    else if (strcmp(k, "vehicle_cargo") == 0) cheats::g_settings.vehicle_cargo = (value != JNI_FALSE);
    else if (strcmp(k, "one_hit_break") == 0) cheats::g_settings.one_hit_break = (value != JNI_FALSE);
    else if (strcmp(k, "max_ore_drop") == 0) cheats::g_settings.max_ore_drop = (value != JNI_FALSE);
    else if (strcmp(k, "auto_dig_speed") == 0) cheats::g_settings.auto_dig_speed = (value != JNI_FALSE);
    else if (strcmp(k, "unlimited_oil") == 0) cheats::g_settings.unlimited_oil = (value != JNI_FALSE);
    else if (strcmp(k, "worker_speed") == 0) cheats::g_settings.worker_speed = (value != JNI_FALSE);
    else if (strcmp(k, "worker_stamina") == 0) cheats::g_settings.worker_stamina = (value != JNI_FALSE);
    else if (strcmp(k, "worker_attack") == 0) cheats::g_settings.worker_attack = (value != JNI_FALSE);
    else if (strcmp(k, "worker_cargo") == 0) cheats::g_settings.worker_cargo = (value != JNI_FALSE);
    else if (strcmp(k, "instant_smelt") == 0) cheats::g_settings.instant_smelt = (value != JNI_FALSE);
    else if (strcmp(k, "max_critical_smelt") == 0) cheats::g_settings.max_critical_smelt = (value != JNI_FALSE);
    else if (strcmp(k, "max_sales_price") == 0) cheats::g_settings.max_sales_price = (value != JNI_FALSE);
    else if (strcmp(k, "god_mode") == 0) cheats::g_settings.god_mode = (value != JNI_FALSE);
    else if (strcmp(k, "unlock_all") == 0) cheats::g_settings.unlock_all = (value != JNI_FALSE);
    else if (strcmp(k, "free_iap") == 0) cheats::g_settings.free_iap = (value != JNI_FALSE);
    
    env->ReleaseStringUTFChars(key, k);
}

void Native_SetCheatFloat(JNIEnv* env, jclass, jstring key, jfloat value) {
    const char* k = env->GetStringUTFChars(key, nullptr);
    if (!k) return;
    
    if (strcmp(k, "speed_value") == 0) cheats::g_settings.speed_value = value;
    else if (strcmp(k, "damage_multiplier") == 0) cheats::g_settings.damage_multiplier = (int)value;
    
    env->ReleaseStringUTFChars(key, k);
}

// ---- Get game status ----
jboolean Native_IsGameFound(JNIEnv*, jclass) {
    return g_gameFound.load() ? JNI_TRUE : JNI_FALSE;
}

jint Native_GetGamePid(JNIEnv*, jclass) {
    return (jint)mem::target_pid;
}

jlong Native_GetIl2CppBase(JNIEnv*, jclass) {
    // We need to get the base from overlay::App's introspection
    // For now, return the cached value from the scanner
    // This will be handled better when we connect overlay with cheats
    return 0;
}

// ---- JNI method table ----
const JNINativeMethod kMethods[] = {
    // Surface
    {"setSurface",        "(Landroid/view/Surface;)V",                  (void*)Native_SetSurface},
    {"clearSurface",      "()V",                                        (void*)Native_ClearSurface},
    {"surfaceChanged",    "(II)V",                                      (void*)Native_SurfaceChanged},
    // Touch
    {"onTouch",           "(IFF)V",                                     (void*)Native_OnTouch},
    // Settings
    {"setDensity",        "(F)V",                                       (void*)Native_SetDensity},
    {"setSetting",        "(Ljava/lang/String;ZF)V",                    (void*)Native_SetSetting},
    {"setCallbackTarget", "(Ljava/lang/Object;)V",                      (void*)Native_SetCallbackTarget},
    // Scanner
    {"startScanner",      "()V",                                        (void*)Native_StartScanner},
    {"stopScanner",       "()V",                                        (void*)Native_StopScanner},
    {"forceRescan",       "()V",                                        (void*)Native_ForceRescan},
    // Cheats
    {"setCheat",          "(Ljava/lang/String;Z)V",                     (void*)Native_SetCheat},
    {"setCheatFloat",     "(Ljava/lang/String;F)V",                     (void*)Native_SetCheatFloat},
    // Status
    {"isGameFound",       "()Z",                                        (void*)Native_IsGameFound},
    {"getGamePid",        "()I",                                        (void*)Native_GetGamePid},
    {"getIl2CppBase",     "()J",                                        (void*)Native_GetIl2CppBase},
};

} // namespace

// -----------------------------------------------------------------------------
// Library load/unload
// -----------------------------------------------------------------------------
jint JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
    g_vm = vm;
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK)
        return JNI_ERR;

    jclass clazz = env->FindClass("com/example/debugoverlay/NativeBridge");
    if (!clazz) return JNI_ERR;
    if (env->RegisterNatives(clazz, kMethods,
                             sizeof(kMethods) / sizeof(kMethods[0])) != JNI_OK)
        return JNI_ERR;
    env->DeleteLocalRef(clazz);

    LOGI("Native lib loaded, %zu JNI methods registered, target: %s",
         sizeof(kMethods) / sizeof(kMethods[0]), TARGET_PACKAGE);
    return JNI_VERSION_1_6;
}

void JNI_OnUnload(JavaVM*, void*) {
    StopScannerThread();
    StopRenderThread();
    std::lock_guard<std::mutex> lock(g_callbackMutex);
    g_callbackTarget = nullptr;
    LOGI("Native lib unloaded");
}
