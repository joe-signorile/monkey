#pragma once

#include <cstdint>
#include <string>
#include <vector>

// One launchable app. Pixels are retained after upload, not freed: an EGL
// context loss (surface recreate, rotation) invalidates the atlas textures,
// and keeping the source bytes lets the render thread re-upload without
// re-querying the PackageManager (icon) or re-rasterizing text (label).
struct AppTile {
    std::string label;
    std::string pkg;
    int iconW = 0;
    int iconH = 0;
    std::vector<uint8_t> pixels;  // RGBA_8888, premultiplied, retained
    int atlasSlot = -1;           // index into Renderer's shared icon atlas, -1 if unset

    int labelW = 0;
    int labelH = 0;
    std::vector<uint8_t> labelPixels;  // RGBA_8888, premultiplied, retained
    int labelAtlasSlot = -1;           // index into Renderer's shared label atlas, -1 if unset
};

// Screen-space rectangle, y growing downward from the top of the viewport.
struct TileRect {
    float x = 0, y = 0, w = 0, h = 0;
};

// Owns the tile list, layout, scroll state and hit-testing. Pure geometry:
// knows nothing about GL or JNI.
//
// Not internally synchronized. The caller (Renderer) holds the lock.
class Grid {
public:
    static constexpr int kNoTile = -1;

    void setViewport(int w, int h);
    void clear();
    void add(AppTile tile);
    void layout();

    // --- input, all in viewport pixels ---
    void onDown(float x, float y);
    void onMove(float x, float y, float dtSeconds);
    // Returns the tapped tile index, or kNoTile if this was a scroll/fling.
    int onUp(float x, float y);
    void onCancel();

    // Advances fling decay and clamps scroll. Call once per frame.
    void advance(float dtSeconds);

    // True while a gesture, fling, or overscroll spring-back is running, i.e.
    // more frames are needed. When this is false and nothing is pending, the
    // render thread may sleep. maxScroll() is declared below but visible
    // here regardless of order (class-body member lookup).
    bool busy() const {
        return dragging_ || velocity_ != 0.0f || scroll_ < 0.0f || scroll_ > maxScroll();
    }

    std::vector<AppTile>& tiles() { return tiles_; }
    size_t count() const { return tiles_.size(); }
    // Icon rect for tile `i` with the current scroll already applied.
    TileRect iconRectOf(size_t i) const;
    // Label strip rect for tile `i`, directly below its icon rect.
    TileRect labelRectOf(size_t i) const;

private:
    float maxScroll() const;
    int hitTest(float x, float y) const;
    // Tap zone for tile `i`: icon + label strip together, excluding the side
    // gutters. Wider than iconRectOf so a small child aiming at the label
    // text doesn't miss the tile. hitTest-only; drawing uses the two rects
    // above separately.
    TileRect cellRectOf(size_t i) const;

    std::vector<AppTile> tiles_;

    int viewW_ = 0, viewH_ = 0;
    int columns_ = 1;
    float pitch_ = 0;      // column width, including gutters
    float iconSize_ = 0;   // drawn icon edge, inset inside the pitch
    float labelStripH_ = 0;// label strip height, below the icon within rowPitch_
    float rowPitch_ = 0;   // pitch_ + labelStripH_: full row height including the label
    float contentH_ = 0;

    float scroll_ = 0;    // pixels scrolled down from the top
    float velocity_ = 0;  // pixels/second, positive scrolls down

    bool dragging_ = false;
    float lastX_ = 0, lastY_ = 0;
    float downX_ = 0, downY_ = 0;
    float travel_ = 0;  // accumulated |movement| since down, for tap slop
};
