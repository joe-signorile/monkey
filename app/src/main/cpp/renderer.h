#pragma once

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <android/native_window.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "grid.h"

// Owns EGL, the GL program, the texture set and the render thread.
//
// Every public method is safe to call from the UI thread; the mutex guards the
// Grid, which is not itself synchronized.
class Renderer {
public:
    ~Renderer();

    // Starts (or restarts) rendering into `window`. Takes ownership of the
    // reference and releases it on stop().
    void start(ANativeWindow* window, int width, int height);

    // Blocks until the render thread has released the window. Callers on the
    // surfaceDestroyed path depend on that: returning early would let the
    // framework free a surface the GL thread is still drawing into.
    void stop();

    void clearApps();
    void addApp(std::string label, std::string pkg, const uint8_t* iconPixels, int iconW,
                int iconH, const uint8_t* labelPixels, int labelW, int labelH);
    void appsReady();

    // Returns the tapped tile's package name, or an empty string when the
    // gesture was a scroll rather than a tap.
    std::string onTouch(int action, float x, float y);

private:
    void threadMain();
    bool initEgl();
    void destroyEgl();
    bool initProgram();

    // GL-thread only.
    void uploadPending();
    void drawFrame();

    // Fixed-capacity free-list for an atlas's cell indices. Pure CPU
    // bookkeeping, not a GL object, so unlike a texture name it's safe to
    // mutate from any thread under mutex_ -- clearApps() (UI thread, no GL
    // context) releases slots directly. Shared by the icon and label atlases.
    struct AtlasSlots {
        std::vector<int> free;
        void reset(int capacity) {
            free.clear();
            for (int i = capacity - 1; i >= 0; --i) free.push_back(i);
        }
        int alloc() {
            if (free.empty()) return -1;
            const int slot = free.back();
            free.pop_back();
            return slot;
        }
        void release(int slot) {
            if (slot != -1) free.push_back(slot);
        }
    };

    // Fixed-capacity shared icon atlas: kAtlasCols x kAtlasCols cells of
    // kIconPx pixels each. AppTile::atlasSlot indexes into it. One bound
    // texture and one glTexSubImage2D per upload replaces one GL texture
    // name per icon.
    static constexpr int kIconPx = 128;
    static constexpr int kAtlasCols = 16;
    static constexpr int kAtlasCapacity = kAtlasCols * kAtlasCols;  // 256 icons

    // Fixed-capacity shared label atlas, same free-list scheme as the icon
    // atlas but a different cell shape (labels are wide and short, not
    // square). Kept at the same max texture dimension (2048) as the icon
    // atlas for device compatibility -- see the width/height math below.
    static constexpr int kLabelW = 256;
    static constexpr int kLabelH = 48;
    static constexpr int kLabelCols = 8;
    static constexpr int kLabelCapacity = kAtlasCapacity;  // one label per icon slot
    static constexpr int kLabelRows = kLabelCapacity / kLabelCols;

    ANativeWindow* window_ = nullptr;
    std::thread thread_;
    std::atomic<bool> running_{false};

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    Grid grid_;
    bool dirty_ = true;       // a frame is owed
    bool uploads_ = false;    // tiles are waiting for an atlas slot
    int viewW_ = 0, viewH_ = 0;
    int64_t lastTouchNanos_ = 0;

    AtlasSlots iconSlots_;
    AtlasSlots labelSlots_;

    // EGL/GL handles, render thread only.
    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLSurface surface_ = EGL_NO_SURFACE;
    EGLContext context_ = EGL_NO_CONTEXT;
    GLuint program_ = 0;
    GLuint vbo_ = 0;
    GLuint atlasTexture_ = 0;
    GLuint labelTexture_ = 0;
    GLint aPos_ = -1;
    GLint uRect_ = -1;
    GLint uUV_ = -1;
    GLint uView_ = -1;
    GLint uTex_ = -1;
};
