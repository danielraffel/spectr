#include "spectr/ui/band_field_view.hpp"
#include "spectr/spectr.hpp"

#include <pulp/canvas/canvas.hpp>
#include <pulp/view/visualization_bridge.hpp>

#include <algorithm>
#include <cmath>

namespace spectr {

namespace {

constexpr float kDbMin = -60.0f;
constexpr float kDbMax = +12.0f;

pulp::canvas::Color band_color(int index, std::size_t n, bool muted) {
    if (muted) return {0.55f, 0.20f, 0.23f, 1.0f};
    const float t   = static_cast<float>(index) / static_cast<float>(std::max<std::size_t>(n - 1, 1));
    const float hue = 240.0f - t * 300.0f;            // matches prototype
    const float sat = 0.75f;
    const float val = 0.60f;
    // hsv → rgb
    const float c = val * sat;
    const float h = hue / 60.0f;
    const float x = c * (1.0f - std::fabs(std::fmod(h, 2.0f) - 1.0f));
    float r = 0, g = 0, b = 0;
    if      (h < 1) { r = c; g = x; }
    else if (h < 2) { r = x; g = c; }
    else if (h < 3) { g = c; b = x; }
    else if (h < 4) { g = x; b = c; }
    else if (h < 5) { r = x; b = c; }
    else            { r = c; b = x; }
    const float m = val - c;
    return {r + m, g + m, b + m, 1.0f};
}

} // namespace

float BandFieldView::y_to_db_(float y, float height) const {
    // y=0 → +12 dB (top). y=height → -60 dB (bottom).
    const float t = std::clamp(y / std::max(height, 1.0f), 0.0f, 1.0f);
    return kDbMax - t * (kDbMax - kDbMin);
}

float BandFieldView::db_to_y_(float db, float height) const {
    const float t = (kDbMax - std::clamp(db, kDbMin, kDbMax)) / (kDbMax - kDbMin);
    return t * height;
}

int BandFieldView::band_for_x_(float x, float width, std::size_t n_visible) const {
    if (n_visible == 0 || width <= 0.0f) return -1;
    const float col_w = width / static_cast<float>(n_visible);
    const int idx = static_cast<int>(x / col_w);
    return std::clamp(idx, 0, static_cast<int>(n_visible) - 1);
}

void BandFieldView::paint(pulp::canvas::Canvas& canvas) {
    const auto w = bounds().width;
    const auto h = bounds().height;
    if (w <= 0 || h <= 0) return;

    // Background.
    canvas.set_fill_color({0.033f, 0.043f, 0.062f, 1.0f});
    canvas.fill_rect(0, 0, w, h);

    // Zero-dB baseline.
    const float zero_y = db_to_y_(0.0f, h);
    canvas.set_fill_color({1.0f, 1.0f, 1.0f, 0.08f});
    canvas.fill_rect(0, zero_y - 0.5f, w, 1.0f);

    // Analyzer overlay — STFT magnitude from the bridge.
    const auto& spec = plugin_.read_spectrum();
    if (spec.num_bins > 1) {
        canvas.set_fill_color({0.45f, 0.70f, 0.90f, 0.18f});
        const float bin_w = w / static_cast<float>(spec.num_bins - 1);
        for (int k = 0; k < spec.num_bins - 1; ++k) {
            const float db = std::clamp(spec.magnitude_db[k], -80.0f, 0.0f);
            const float t  = (db + 80.0f) / 80.0f;
            const float bh = t * (h * 0.4f);
            canvas.fill_rect(k * bin_w, h - bh, bin_w, bh);
        }
    }

    // Bands.
    const auto& field  = plugin_.field();
    const auto layout  = plugin_.layout();
    const auto n       = visible_count(layout);
    const float col_w  = w / static_cast<float>(n);

    for (std::size_t i = 0; i < n; ++i) {
        const auto& band = field.bands[i];
        const auto color = band_color(static_cast<int>(i), n, band.muted);
        const float x = i * col_w;
        const float bar_y = band.muted ? zero_y : db_to_y_(band.gain_db, h);
        const float bar_h = band.muted ? 0.0f : std::fabs(zero_y - bar_y);
        canvas.set_fill_color(color);
        if (band.muted) {
            canvas.fill_rect(x + 1, h - 3, col_w - 2, 2);  // red footer hint
        } else if (band.gain_db >= 0.0f) {
            canvas.fill_rect(x + 1, bar_y, col_w - 2, bar_h);
        } else {
            canvas.fill_rect(x + 1, zero_y, col_w - 2, bar_h);
        }
    }

    // Column separators.
    canvas.set_fill_color({1.0f, 1.0f, 1.0f, 0.04f});
    for (std::size_t i = 1; i < n; ++i) {
        canvas.fill_rect(i * col_w, 0, 0.5f, h);
    }
}

void BandFieldView::on_mouse_down(pulp::view::Point pos) {
    const auto layout = plugin_.layout();
    const auto n      = visible_count(layout);
    active_band_      = band_for_x_(pos.x, bounds().width, n);
    if (active_band_ < 0) return;
    auto& band = plugin_.field().bands[static_cast<std::size_t>(active_band_)];
    drag_start_y_       = pos.y;
    drag_start_gain_db_ = band.gain_db;
    // Initial click paints the band to the clicked Y.
    band.muted   = false;
    band.gain_db = std::clamp(y_to_db_(pos.y, bounds().height), kDbMin, kDbMax);
}

void BandFieldView::on_mouse_drag(pulp::view::Point pos) {
    if (active_band_ < 0) return;
    const auto layout = plugin_.layout();
    const auto n      = visible_count(layout);
    // Allow painting across bands during the drag (prototype behaviour).
    const int cur = band_for_x_(pos.x, bounds().width, n);
    if (cur < 0) return;
    auto& band = plugin_.field().bands[static_cast<std::size_t>(cur)];
    band.muted   = false;
    band.gain_db = std::clamp(y_to_db_(pos.y, bounds().height), kDbMin, kDbMax);
}

void BandFieldView::on_mouse_up(pulp::view::Point /*pos*/) {
    active_band_ = -1;
}

} // namespace spectr
