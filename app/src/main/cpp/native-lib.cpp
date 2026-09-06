// =============================================================================
//  native-lib.cpp — JNI bridge between the Java overlay service and the C++
//                   introspection/rendering engine
// =============================================================================
//
//  Thread map (worth internalizing — this is the shape of every overlay app):
//
//    Java UI thread     : touch events, surface create/destroy, settings I/O
//    C++ render thread  : owns the EGL context + ImGui context, runs Frame()
//    C++ demo tick thread: mutates the simulated game state at 10 Hz
//
//  Cross-thread rules demonstrated here:
//    * EGL contexts are thread-bound: created, used and destroyed on the
//      render thread only.
//    * ImGui's input queue is designed to be fed from another thread, so
//      OnTouch from the UI thread is safe.
//    * Java callbacks from native threads use AttachCurrentThread (see
//      JniAttach below) — the classic JNI up-call pattern.
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

#include "demo_game.h"
#include "overlay.h"

#define LOG_TAG "NativeLib"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

overlay::App g_app;                       // engine + ImGui app (process-wide)

std::thread g_renderThread;               // owns EGL + ImGui while a surface lives
std::atomic<bool> g_renderRunning{false};
std::mutex g_renderMutex;                 // serializes start/stop of the thread
std::condition_variable g_renderCv;

JavaVM* g_vm = nullptr;                   // cached in JNI_OnLoad
jobject g_callbackTarget = nullptr;       // global ref to DebugService
jmethodID g_onSettingChanged = nullptr;   // (String key, boolean isFloat, float value)V
jmethodID g_onPanelClosed = nullptr;       // ()V
std::mutex g_callbackMutex;

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
        // Callbacks arrive on threads Java knows nothing about (render thread).
        // AttachCurrentThread wires them into the JVM; we must detach after.
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

// -----------------------------------------------------------------------------
// Render thread body
// -----------------------------------------------------------------------------
void RenderThreadFunc(ANativeWindow* window) {
    // EGL requires the context to be current on THIS thread for every call —
    // which is exactly why Init/Frame/Shutdown all happen right here.
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
        // Fired when the user clicks the panel's [x]; Java removes the view.
        NotifyPanelClosed();
    });

    LOGI("render thread running");
    while (g_renderRunning.load(std::memory_order_acquire)) {
        if (!g_app.Frame()) break;   // false => panel was closed by the user
    }
    g_app.Shutdown();
    ANativeWindow_release(window);   // drop the ref taken by ANativeWindow_fromSurface
    g_renderRunning.store(false, std::memory_order_release);
    LOGI("render thread stopped");
}

bool StopRenderThread() {
    if (!g_renderRunning.load()) return false;
    g_renderRunning.store(false, std::memory_order_release);
    if (g_renderThread.joinable()) g_renderThread.join();
    return true;
}

// -----------------------------------------------------------------------------
// JNI methods (registered below — the table beats name mangling by hand)
// -----------------------------------------------------------------------------
jmethodID FindMethodOrDie(JNIEnv* env, jclass clazz, const char* name, const char* sig) {
    jmethodID id = env->GetMethodID(clazz, name, sig);
    if (!id) {
        LOGE("method not found: %s %s", name, sig);
        env->FatalError("NativeBridge callback method missing");
    }
    return id;
}

void Native_SetSurface(JNIEnv* env, jclass, jobject surface) {
    std::lock_guard<std::mutex> lock(g_renderMutex);
    StopRenderThread();
    if (!surface) return;

    // ANativeWindow is the C view of the Java Surface; EGL renders into it.
    // This takes a reference we release at the end of the render thread.
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

void Native_OnTouch(JNIEnv*, jclass, jint action, jfloat x, jfloat y) {
    // ImGui 1.90+ input queues are safe to feed from a non-render thread.
    g_app.OnTouch((int)action, x, y);
}

void Native_SetDensity(JNIEnv*, jclass, jfloat d) {
    g_app.SetDensity(d);
}

void Native_SetSetting(JNIEnv* env, jclass, jstring key, jboolean isFloat, jfloat value) {
    const char* k = env->GetStringUTFChars(key, nullptr);
    if (!k) return;
    g_app.SetSetting(k, isFloat != JNI_FALSE, value);
    env->ReleaseStringUTFChars(key, k);
}

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
    att.env->DeleteLocalRef(clazz);
    LOGI("callback target registered");
}

void Native_StartDemoGame(JNIEnv*, jclass) {
    demo::DemoGame::Instance().Start();
}

void Native_StopDemoGame(JNIEnv*, jclass) {
    demo::DemoGame::Instance().Stop();
}

const JNINativeMethod kMethods[] = {
    {"startDemoGame",     "()V",                                        (void*)Native_StartDemoGame},
    {"stopDemoGame",      "()V",                                        (void*)Native_StopDemoGame},
    {"setSurface",        "(Landroid/view/Surface;)V",                  (void*)Native_SetSurface},
    {"clearSurface",      "()V",                                        (void*)Native_ClearSurface},
    {"surfaceChanged",    "(II)V",                                      (void*)Native_SurfaceChanged},
    {"onTouch",           "(IFF)V",                                     (void*)Native_OnTouch},
    {"setDensity",        "(F)V",                                       (void*)Native_SetDensity},
    {"setSetting",        "(Ljava/lang/String;ZF)V",                    (void*)Native_SetSetting},
    {"setCallbackTarget", "(Ljava/lang/Object;)V",                      (void*)Native_SetCallbackTarget},
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

    LOGI("native lib loaded, %zu JNI methods registered",
         sizeof(kMethods) / sizeof(kMethods[0]));
    return JNI_VERSION_1_6;
}

void JNI_OnUnload(JavaVM*, void*) {
    std::lock_guard<std::mutex> lock(g_callbackMutex);
    // Best-effort cleanup; the JVM is going away anyway.
    g_callbackTarget = nullptr;
}
