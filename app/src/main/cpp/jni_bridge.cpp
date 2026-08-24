// The entire C++ <-> Kotlin boundary. Symbol names are derived from
// os.monkey.shell.Native; changing either side without the other yields an
// UnsatisfiedLinkError at first touch.
//
// Kotlin `object` members compile to *instance* methods, hence the jobject
// second parameter rather than jclass.

#include <android/log.h>
#include <android/native_window_jni.h>
#include <jni.h>

#include <string>

#include "renderer.h"

namespace {

constexpr const char* kTag = "monkey";
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, kTag, __VA_ARGS__)

Renderer gRenderer;

// Global ref to the ShellActivity, plus its cached launchApp method.
jobject gActivity = nullptr;
jmethodID gLaunchApp = nullptr;

std::string toStdString(JNIEnv* env, jstring value) {
    if (value == nullptr) return {};
    const char* chars = env->GetStringUTFChars(value, nullptr);
    if (chars == nullptr) return {};
    std::string out(chars);
    env->ReleaseStringUTFChars(value, chars);
    return out;
}

}  // namespace

extern "C" {

JNIEXPORT void JNICALL Java_os_monkey_shell_Native_setActivity(JNIEnv* env, jobject,
                                                               jobject activity) {
    if (gActivity != nullptr) {
        env->DeleteGlobalRef(gActivity);
        gActivity = nullptr;
        gLaunchApp = nullptr;
    }
    if (activity == nullptr) return;

    gActivity = env->NewGlobalRef(activity);
    jclass cls = env->GetObjectClass(activity);
    gLaunchApp = env->GetMethodID(cls, "launchApp", "(Ljava/lang/String;)V");
    env->DeleteLocalRef(cls);
    if (gLaunchApp == nullptr) LOGE("launchApp(String) not found; taps will do nothing");
}

JNIEXPORT void JNICALL Java_os_monkey_shell_Native_surfaceCreated(JNIEnv* env, jobject,
                                                                  jobject surface, jint width,
                                                                  jint height) {
    // Acquires a reference; Renderer::stop() releases it.
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (window == nullptr) {
        LOGE("ANativeWindow_fromSurface returned null");
        return;
    }
    gRenderer.start(window, width, height);
}

JNIEXPORT void JNICALL Java_os_monkey_shell_Native_surfaceDestroyed(JNIEnv*, jobject) {
    // Blocks until the render thread has released the window.
    gRenderer.stop();
}

JNIEXPORT void JNICALL Java_os_monkey_shell_Native_clearApps(JNIEnv*, jobject) {
    gRenderer.clearApps();
}

JNIEXPORT void JNICALL Java_os_monkey_shell_Native_addApp(JNIEnv* env, jobject, jstring label,
                                                          jstring pkg, jobject iconPixels,
                                                          jint iconWidth, jint iconHeight,
                                                          jobject labelPixels, jint labelWidth,
                                                          jint labelHeight) {
    // Null means the buffer was not allocated with allocateDirect. Bail rather
    // than upload garbage into a texture.
    void* iconAddress = env->GetDirectBufferAddress(iconPixels);
    if (iconAddress == nullptr) {
        LOGE("addApp: icon pixel buffer is not direct");
        return;
    }
    // A missing/non-direct label buffer isn't fatal -- the tile just draws
    // without a label -- but null it out explicitly rather than pass a
    // dangling address through.
    void* labelAddress = labelPixels != nullptr ? env->GetDirectBufferAddress(labelPixels)
                                                 : nullptr;
    gRenderer.addApp(toStdString(env, label), toStdString(env, pkg),
                     static_cast<const uint8_t*>(iconAddress), iconWidth, iconHeight,
                     static_cast<const uint8_t*>(labelAddress), labelWidth, labelHeight);
}

JNIEXPORT void JNICALL Java_os_monkey_shell_Native_appsReady(JNIEnv*, jobject) {
    gRenderer.appsReady();
}

JNIEXPORT void JNICALL Java_os_monkey_shell_Native_touch(JNIEnv* env, jobject, jint action,
                                                         jfloat x, jfloat y) {
    const std::string pkg = gRenderer.onTouch(action, x, y);
    if (pkg.empty() || gActivity == nullptr || gLaunchApp == nullptr) return;

    // Already on the UI thread, so no AttachCurrentThread is needed.
    jstring value = env->NewStringUTF(pkg.c_str());
    env->CallVoidMethod(gActivity, gLaunchApp, value);
    env->DeleteLocalRef(value);
}

}  // extern "C"
