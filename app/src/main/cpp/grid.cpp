#include "grid.h"

#include <algorithm>
#include <cmath>

namespace {
// Target pitch between tile centres. Large on purpose: this is a kid-facing
// grid, so fewer, bigger targets beat a dense list.
constexpr float kTargetPitchPx = 320.0f;
constexpr int kMinColumns = 2;
constexpr int kMaxColumns = 6;

// Fraction of the pitch actually filled by the icon; the rest is gutter.
constexpr float kIconFill = 0.68f;

// Label strip height below the icon, as a fraction of pitch_. Added on top
// of pitch_ to form rowPitch_, the full per-row height.
constexpr float kLabelStripFill = 0.20f;

// Movement past this (px) means the gesture was a scroll, not a tap.
constexpr float kTapSlopPx = 24.0f;

// Fling decay. velocity *= exp(-kFriction * dt)
constexpr float kFriction = 5.0f;
constexpr float kMinFlingSpeed = 20.0f;  // px/s; below this we stop

// Clamps runaway velocity from a single jittery sample.
constexpr float kMaxFlingSpeed = 12000.0f;

// Overscroll past either end is resisted, not blocked: only this fraction of
// the excess drag distance is actually applied. A deliberately simple linear
// damping, not the true iOS logarithmic falloff -- the point is "it gives a
// little and pushes back", not a physically exact model.
constexpr float kOverscrollResistance = 0.35f;

// A fling that reaches a bound hands off to the spring-back below instead of
// hard-stopping; this caps how far it can carry past the edge first.
constexpr float kMaxOverscrollPx = 80.0f;

// Spring-back convergence rate once released while overscrolled: fraction of
// the remaining distance to the bound closed per second.
constexpr float kSpringBack = 12.0f;

// Applies drag `delta` to `scroll`, damping any excess past [0, maxScroll].
float applyDrag(float scroll, float delta, float maxScroll) {
    const float next = scroll + delta;
    if (next < 0.0f) {
        return next * kOverscrollResistance;  // damp the excess below 0
    }
    if (next > maxScroll) {
        return maxScroll + (next - maxScroll) * kOverscrollResistance;
    }
    return next;
}
}  // namespace

void Grid::setViewport(int w, int h) {
    viewW_ = w;
    viewH_ = h;
    layout();
}

void Grid::clear() {
    // Textures are owned by the GL context and deleted by the Renderer, which
    // walks this list before calling clear().
    tiles_.clear();
    scroll_ = 0;
    velocity_ = 0;
    contentH_ = 0;
}

void Grid::add(AppTile tile) { tiles_.push_back(std::move(tile)); }

void Grid::layout() {
    if (viewW_ <= 0) return;

    columns_ = std::clamp(
        static_cast<int>(std::lround(viewW_ / kTargetPitchPx)), kMinColumns, kMaxColumns);
    pitch_ = static_cast<float>(viewW_) / static_cast<float>(columns_);
    iconSize_ = pitch_ * kIconFill;
    labelStripH_ = pitch_ * kLabelStripFill;
    rowPitch_ = pitch_ + labelStripH_;

    const int rows =
        static_cast<int>((tiles_.size() + columns_ - 1) / static_cast<size_t>(columns_));
    contentH_ = static_cast<float>(rows) * rowPitch_;

    // A relayout (rotation, catalog refresh) can leave scroll past the new end.
    scroll_ = std::clamp(scroll_, 0.0f, maxScroll());
}

float Grid::maxScroll() const {
    return std::max(0.0f, contentH_ - static_cast<float>(viewH_));
}

TileRect Grid::iconRectOf(size_t i) const {
    const int col = static_cast<int>(i) % columns_;
    const int row = static_cast<int>(i) / columns_;
    const float inset = (pitch_ - iconSize_) * 0.5f;
    return TileRect{
        static_cast<float>(col) * pitch_ + inset,
        static_cast<float>(row) * rowPitch_ + inset - scroll_,
        iconSize_,
        iconSize_,
    };
}

TileRect Grid::labelRectOf(size_t i) const {
    const int col = static_cast<int>(i) % columns_;
    const int row = static_cast<int>(i) / columns_;
    return TileRect{
        static_cast<float>(col) * pitch_,
        static_cast<float>(row) * rowPitch_ + pitch_ - scroll_,
        pitch_,
        labelStripH_,
    };
}

TileRect Grid::cellRectOf(size_t i) const {
    const int col = static_cast<int>(i) % columns_;
    const int row = static_cast<int>(i) / columns_;
    const float inset = (pitch_ - iconSize_) * 0.5f;
    return TileRect{
        static_cast<float>(col) * pitch_ + inset,
        static_cast<float>(row) * rowPitch_ + inset - scroll_,
        iconSize_,
        iconSize_ + labelStripH_,
    };
}

void Grid::onDown(float x, float y) {
    dragging_ = true;
    downX_ = lastX_ = x;
    downY_ = lastY_ = y;
    travel_ = 0;
    velocity_ = 0;  // catching a moving grid stops it
}

void Grid::onMove(float x, float y, float dtSeconds) {
    if (!dragging_) return;

    const float dy = y - lastY_;
    travel_ += std::abs(x - lastX_) + std::abs(dy);

    // Dragging up (negative dy) scrolls the content down. Past either end
    // this is damped rather than clamped -- see applyDrag().
    scroll_ = applyDrag(scroll_, -dy, maxScroll());

    if (dtSeconds > 0.0f) {
        const float sample = -dy / dtSeconds;
        // Light smoothing; a single bad sample should not dominate the fling.
        velocity_ = std::clamp(
            velocity_ * 0.4f + sample * 0.6f, -kMaxFlingSpeed, kMaxFlingSpeed);
    }

    lastX_ = x;
    lastY_ = y;
}

int Grid::onUp(float x, float y) {
    if (!dragging_) return kNoTile;
    dragging_ = false;

    travel_ += std::abs(x - lastX_) + std::abs(y - lastY_);
    if (travel_ <= kTapSlopPx) {
        velocity_ = 0;
        return hitTest(downX_, downY_);
    }
    return kNoTile;
}

void Grid::onCancel() {
    dragging_ = false;
    velocity_ = 0;
}

void Grid::advance(float dtSeconds) {
    if (dragging_) return;

    const float limit = maxScroll();

    if (velocity_ != 0.0f) {
        scroll_ += velocity_ * dtSeconds;
        velocity_ *= std::exp(-kFriction * dtSeconds);
        if (std::abs(velocity_) < kMinFlingSpeed) velocity_ = 0.0f;

        // A fling that reaches a bound hands off to the spring-back below
        // instead of hard-stopping; cap how far it can carry past the edge
        // first so a fast flick can't fling the content arbitrarily far out.
        scroll_ = std::clamp(scroll_, -kMaxOverscrollPx, limit + kMaxOverscrollPx);
        if (scroll_ < 0.0f || scroll_ > limit) velocity_ = 0.0f;
    }

    // Spring back to the nearest bound: overscroll left over from a drag
    // release or a fling that ran past the edge eases back instead of
    // snapping. busy() reports true while this runs, so the render thread
    // doesn't idle-gate mid-bounce.
    if (scroll_ < 0.0f || scroll_ > limit) {
        const float bound = scroll_ < 0.0f ? 0.0f : limit;
        scroll_ += (bound - scroll_) * std::min(1.0f, kSpringBack * dtSeconds);
        if (std::abs(scroll_ - bound) < 0.5f) scroll_ = bound;  // end the animation
    }
}

int Grid::hitTest(float x, float y) const {
    if (pitch_ <= 0.0f || rowPitch_ <= 0.0f) return kNoTile;

    const int col = static_cast<int>(x / pitch_);
    const int row = static_cast<int>((y + scroll_) / rowPitch_);
    if (col < 0 || col >= columns_ || row < 0) return kNoTile;

    const size_t index = static_cast<size_t>(row) * static_cast<size_t>(columns_) +
                         static_cast<size_t>(col);
    if (index >= tiles_.size()) return kNoTile;

    // Taps landing in the side gutters, or below the label strip, should
    // miss rather than launch a neighbour the child wasn't aiming at. The
    // tap zone covers the icon and its label together (cellRectOf), not
    // just the icon square, so a small child aiming at the word under the
    // icon doesn't miss it.
    const TileRect r = cellRectOf(index);
    if (x < r.x || x > r.x + r.w || y < r.y || y > r.y + r.h) return kNoTile;

    return static_cast<int>(index);
}
