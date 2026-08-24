#include "renderer.h"

#include <android/log.h>

#include <chrono>
#include <cstring>

namespace {

constexpr const char* kTag = "monkey";
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, kTag, __VA_ARGS__)

// android.view.MotionEvent action constants.
constexpr int kActionDown = 0;
constexpr int kActionUp = 1;
constexpr int kActionMove = 2;
constexpr int kActionCancel = 3;

int64_t nowNanos() {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

// Unit quad as a triangle strip: (0,0) (1,0) (0,1) (1,1).
constexpr GLfloat kQuad[] = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 1.f, 1.f};

const char* kVertexSrc = R"(
attribute vec2 aPos;      // unit quad, 0..1
uniform vec4 uRect;       // x, y, w, h in pixels
uniform vec4 uUV;         // atlas cell: u0, v0, du, dv (0..1)
uniform vec2 uView;       // viewport size in pixels
varying vec2 vUV;
void main() {
    // glTexImage2D maps the first data row (the bitmap's top) to v=0, so the
    // quad's local coords map straight onto the atlas cell, unflipped.
    vUV = uUV.xy + aPos * uUV.zw;
    vec2 p = uRect.xy + aPos * uRect.zw;
    // Screen y grows downward, clip y grows upward.
    gl_Position = vec4(p.x / uView.x * 2.0 - 1.0,
                       1.0 - p.y / uView.y * 2.0,
                       0.0, 1.0);
}
)";

const char* kFragmentSrc = R"(
precision mediump float;
uniform sampler2D uTex;
varying vec2 vUV;
void main() { gl_FragColor = texture2D(uTex, vUV); }
)";

GLuint compileShader(GLenum type, const char* src) {
    const GLuint shader = glCreateShader(type);
    if (shader == 0) return 0;
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        LOGE("shader compile failed: %s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

}  // namespace

Renderer::~Renderer() { stop(); }

void Renderer::start(ANativeWindow* window, int width, int height) {
    stop();  // idempotent, and releases any window held by a previous run
    if (window == nullptr) return;

    window_ = window;  // reference already acquired by the caller
    {
        std::lock_guard<std::mutex> lock(mutex_);
        viewW_ = width;
        viewH_ = height;
        grid_.setViewport(width, height);

        // The previous EGL context died with the old surface, so both atlas
        // textures and every slot assignment are stale. Retained pixels let
        // us simply re-upload into fresh atlases.
        for (auto& tile : grid_.tiles()) {
            tile.atlasSlot = -1;
            tile.labelAtlasSlot = -1;
        }
        iconSlots_.reset(kAtlasCapacity);
        labelSlots_.reset(kLabelCapacity);
        uploads_ = !grid_.tiles().empty();
        dirty_ = true;
    }

    running_.store(true);
    thread_ = std::thread(&Renderer::threadMain, this);
}

void Renderer::stop() {
    const bool wasRunning = running_.exchange(false);
    if (wasRunning) {
        cv_.notify_all();
        if (thread_.joinable()) thread_.join();
    }
    if (window_ != nullptr) {
        ANativeWindow_release(window_);
        window_ = nullptr;
    }
}

void Renderer::clearApps() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // Freeing a slot is pure bookkeeping, not a GL call, so it's safe to
        // do directly here on the UI thread -- unlike a texture name, which
        // would need the render thread's GL context to delete.
        for (auto& tile : grid_.tiles()) {
            iconSlots_.release(tile.atlasSlot);
            labelSlots_.release(tile.labelAtlasSlot);
        }
        grid_.clear();
        dirty_ = true;
    }
    cv_.notify_one();
}

void Renderer::addApp(std::string label, std::string pkg, const uint8_t* iconPixels, int iconW,
                      int iconH, const uint8_t* labelPixels, int labelW, int labelH) {
    if (iconPixels == nullptr || iconW <= 0 || iconH <= 0) {
        LOGE("addApp(%s): bad icon data", pkg.c_str());
        return;
    }

    AppTile tile;
    tile.label = std::move(label);
    tile.pkg = std::move(pkg);
    tile.iconW = iconW;
    tile.iconH = iconH;
    // Copy so the render thread owns the bytes; the Java ByteBuffers may be
    // collected as soon as this returns.
    tile.pixels.assign(iconPixels, iconPixels + static_cast<size_t>(iconW) * iconH * 4);

    // A missing label isn't fatal -- the tile just draws without one.
    if (labelPixels != nullptr && labelW > 0 && labelH > 0) {
        tile.labelW = labelW;
        tile.labelH = labelH;
        tile.labelPixels.assign(labelPixels, labelPixels + static_cast<size_t>(labelW) * labelH * 4);
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        grid_.add(std::move(tile));
        uploads_ = true;
    }
    // No notify: appsReady() wakes the thread once, rather than once per app.
}

void Renderer::appsReady() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        grid_.layout();
        dirty_ = true;
    }
    cv_.notify_one();
}

std::string Renderer::onTouch(int action, float x, float y) {
    std::string pkg;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const int64_t now = nowNanos();
        switch (action) {
            case kActionDown:
                lastTouchNanos_ = now;
                grid_.onDown(x, y);
                break;
            case kActionMove: {
                const float dt = static_cast<float>(now - lastTouchNanos_) / 1e9f;
                lastTouchNanos_ = now;
                grid_.onMove(x, y, dt);
                break;
            }
            case kActionUp: {
                const int hit = grid_.onUp(x, y);
                if (hit != Grid::kNoTile) pkg = grid_.tiles()[static_cast<size_t>(hit)].pkg;
                break;
            }
            case kActionCancel:
                grid_.onCancel();
                break;
            default:
                return {};
        }
        dirty_ = true;
    }
    cv_.notify_one();
    return pkg;
}

void Renderer::threadMain() {
    if (!initEgl() || !initProgram()) {
        LOGE("GL init failed; no frames will be drawn");
        destroyEgl();
        return;  // running_ stays true so stop() still joins this thread
    }

    int64_t last = nowNanos();
    while (running_.load(std::memory_order_relaxed)) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            // Idle gating: with nothing moving and nothing pending, sleep
            // instead of burning a core at vsync on a battery-powered tablet.
            cv_.wait(lock, [this] {
                return !running_.load(std::memory_order_relaxed) || dirty_ || uploads_ ||
                       grid_.busy();
            });
            if (!running_.load(std::memory_order_relaxed)) break;

            const int64_t now = nowNanos();
            float dt = static_cast<float>(now - last) / 1e9f;
            last = now;
            // After an idle sleep dt is huge; clamp so a fling cannot teleport.
            if (dt > 0.1f) dt = 0.1f;

            if (uploads_) {
                uploadPending();
                uploads_ = false;
            }
            grid_.advance(dt);
            drawFrame();
            dirty_ = grid_.busy();
        }
        // Outside the lock: this blocks until vsync.
        eglSwapBuffers(display_, surface_);
    }

    destroyEgl();
}

bool Renderer::initEgl() {
    display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display_ == EGL_NO_DISPLAY) return false;
    if (eglInitialize(display_, nullptr, nullptr) != EGL_TRUE) return false;

    const EGLint configAttribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RED_SIZE,        8,
        EGL_GREEN_SIZE,      8,
        EGL_BLUE_SIZE,       8,
        EGL_NONE,
    };
    EGLConfig config = nullptr;
    EGLint configCount = 0;
    if (eglChooseConfig(display_, configAttribs, &config, 1, &configCount) != EGL_TRUE ||
        configCount < 1) {
        return false;
    }

    // The buffer geometry must match the config, or the driver silently
    // rejects the window surface on some devices.
    EGLint format = 0;
    eglGetConfigAttrib(display_, config, EGL_NATIVE_VISUAL_ID, &format);
    ANativeWindow_setBuffersGeometry(window_, 0, 0, format);

    surface_ = eglCreateWindowSurface(display_, config, window_, nullptr);
    if (surface_ == EGL_NO_SURFACE) return false;

    const EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    context_ = eglCreateContext(display_, config, EGL_NO_CONTEXT, contextAttribs);
    if (context_ == EGL_NO_CONTEXT) return false;

    return eglMakeCurrent(display_, surface_, surface_, context_) == EGL_TRUE;
}

bool Renderer::initProgram() {
    const GLuint vs = compileShader(GL_VERTEX_SHADER, kVertexSrc);
    const GLuint fs = compileShader(GL_FRAGMENT_SHADER, kFragmentSrc);
    if (vs == 0 || fs == 0) return false;

    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glLinkProgram(program_);
    // Attached shaders live until the program is deleted; drop our refs now.
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = GL_FALSE;
    glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) {
        char log[512];
        glGetProgramInfoLog(program_, sizeof(log), nullptr, log);
        LOGE("program link failed: %s", log);
        return false;
    }

    aPos_ = glGetAttribLocation(program_, "aPos");
    uRect_ = glGetUniformLocation(program_, "uRect");
    uUV_ = glGetUniformLocation(program_, "uUV");
    uView_ = glGetUniformLocation(program_, "uView");
    uTex_ = glGetUniformLocation(program_, "uTex");

    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuad), kQuad, GL_STATIC_DRAW);

    // Two shared atlases, storage allocated once each; uploadPending() fills
    // individual cells with glTexSubImage2D as tiles arrive. Same filtering
    // for both -- linear minification/magnification, clamp at the edges so
    // neighbouring cells never bleed into each other.
    auto initAtlas = [](GLuint* texture, int w, int h) {
        glGenTextures(1, texture);
        glBindTexture(GL_TEXTURE_2D, *texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    };
    initAtlas(&atlasTexture_, kAtlasCols * kIconPx, kAtlasCols * kIconPx);
    initAtlas(&labelTexture_, kLabelCols * kLabelW, kLabelRows * kLabelH);

    // Canvas-rendered ARGB_8888 is premultiplied, so the source factor is ONE.
    // GL_SRC_ALPHA here would darken every icon edge.
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    return true;
}

void Renderer::uploadPending() {
    glBindTexture(GL_TEXTURE_2D, atlasTexture_);
    for (auto& tile : grid_.tiles()) {
        if (tile.atlasSlot != -1 || tile.pixels.empty()) continue;

        const int slot = iconSlots_.alloc();
        if (slot == -1) {
            // monkey-boy: fixed atlas capacity (kAtlasCapacity icons) —
            // upgrade to a growable atlas if a real device's app count ever
            // exceeds it. The tile still lays out and hit-tests correctly;
            // it just draws no icon until a slot frees up.
            LOGE("atlas full (%d slots); %s has no icon texture", kAtlasCapacity,
                 tile.pkg.c_str());
            continue;
        }
        const int col = slot % kAtlasCols;
        const int row = slot / kAtlasCols;
        glTexSubImage2D(GL_TEXTURE_2D, 0, col * kIconPx, row * kIconPx, tile.iconW, tile.iconH,
                        GL_RGBA, GL_UNSIGNED_BYTE, tile.pixels.data());
        tile.atlasSlot = slot;
    }

    glBindTexture(GL_TEXTURE_2D, labelTexture_);
    for (auto& tile : grid_.tiles()) {
        if (tile.labelAtlasSlot != -1 || tile.labelPixels.empty()) continue;

        const int slot = labelSlots_.alloc();
        if (slot == -1) {
            LOGE("label atlas full (%d slots); %s has no label texture", kLabelCapacity,
                 tile.pkg.c_str());
            continue;
        }
        const int col = slot % kLabelCols;
        const int row = slot / kLabelCols;
        glTexSubImage2D(GL_TEXTURE_2D, 0, col * kLabelW, row * kLabelH, tile.labelW, tile.labelH,
                        GL_RGBA, GL_UNSIGNED_BYTE, tile.labelPixels.data());
        tile.labelAtlasSlot = slot;
    }
}

void Renderer::drawFrame() {
    glViewport(0, 0, viewW_, viewH_);
    glClearColor(0.07f, 0.08f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (grid_.count() == 0) return;

    glUseProgram(program_);
    glUniform2f(uView_, static_cast<float>(viewW_), static_cast<float>(viewH_));
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glEnableVertexAttribArray(static_cast<GLuint>(aPos_));
    glVertexAttribPointer(static_cast<GLuint>(aPos_), 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(uTex_, 0);

    const float viewH = static_cast<float>(viewH_);

    // Pass 1: icons. One texture bind for every visible tile, not per tile.
    constexpr float kIconCellUV = 1.0f / kAtlasCols;
    glBindTexture(GL_TEXTURE_2D, atlasTexture_);
    for (size_t i = 0; i < grid_.count(); ++i) {
        const AppTile& tile = grid_.tiles()[i];
        if (tile.atlasSlot == -1) continue;

        const TileRect r = grid_.iconRectOf(i);
        // Cull offscreen rows; the list is ordered, but a cheap test per tile
        // keeps this correct regardless of layout changes.
        if (r.y + r.h < 0.0f || r.y > viewH) continue;

        const int col = tile.atlasSlot % kAtlasCols;
        const int row = tile.atlasSlot / kAtlasCols;
        glUniform4f(uUV_, col * kIconCellUV, row * kIconCellUV, kIconCellUV, kIconCellUV);
        glUniform4f(uRect_, r.x, r.y, r.w, r.h);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    // Pass 2: labels. Same reasoning -- one bind for the whole pass, not one
    // per tile, since icon and label live in separate atlas textures.
    constexpr float kLabelCellU = 1.0f / kLabelCols;
    constexpr float kLabelCellV = 1.0f / kLabelRows;
    glBindTexture(GL_TEXTURE_2D, labelTexture_);
    for (size_t i = 0; i < grid_.count(); ++i) {
        const AppTile& tile = grid_.tiles()[i];
        if (tile.labelAtlasSlot == -1) continue;

        const TileRect r = grid_.labelRectOf(i);
        if (r.y + r.h < 0.0f || r.y > viewH) continue;

        const int col = tile.labelAtlasSlot % kLabelCols;
        const int row = tile.labelAtlasSlot / kLabelCols;
        glUniform4f(uUV_, col * kLabelCellU, row * kLabelCellV, kLabelCellU, kLabelCellV);
        glUniform4f(uRect_, r.x, r.y, r.w, r.h);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
}

void Renderer::destroyEgl() {
    if (display_ != EGL_NO_DISPLAY) {
        if (atlasTexture_ != 0) glDeleteTextures(1, &atlasTexture_);
        if (labelTexture_ != 0) glDeleteTextures(1, &labelTexture_);
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (context_ != EGL_NO_CONTEXT) eglDestroyContext(display_, context_);
        if (surface_ != EGL_NO_SURFACE) eglDestroySurface(display_, surface_);
        eglTerminate(display_);
    }
    display_ = EGL_NO_DISPLAY;
    surface_ = EGL_NO_SURFACE;
    context_ = EGL_NO_CONTEXT;
    program_ = 0;
    vbo_ = 0;
    atlasTexture_ = 0;
    labelTexture_ = 0;
}
