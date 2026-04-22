#pragma once

// The main Spectr canvas: vertical bars per visible band, analyzer overlay
// behind, viewport min/max driving log-Hz band placement.
//
// Milestone 5 skeleton: paint() draws bars from the BandField and overlays
// the latest VisualizationBridge spectrum snapshot. Click/drag edits a
// band's gain. Scroll zooms the viewport in log space. Alt-drag pans.
//
// Not yet: pattern/edit mode selection (M6), A/B compare UI (M8), pattern
// manager modal (M7), hover readout (M11).

#include <pulp/view/view.hpp>

namespace spectr {

class Spectr;

class BandFieldView : public pulp::view::View {
public:
    explicit BandFieldView(Spectr& plugin) : plugin_(plugin) {}

    void paint(pulp::canvas::Canvas& canvas) override;

    void on_mouse_down(pulp::view::Point pos) override;
    void on_mouse_drag(pulp::view::Point pos) override;
    void on_mouse_up(pulp::view::Point pos) override;

private:
    Spectr& plugin_;
    int     active_band_ = -1;  // -1 = no drag
    float   drag_start_y_ = 0.0f;
    float   drag_start_gain_db_ = 0.0f;

    // Map a local Y coordinate to dB in the visible dB range [-60, +12].
    float y_to_db_(float y, float height) const;
    float db_to_y_(float db, float height) const;

    // Visible band index for a local X coordinate.
    int   band_for_x_(float x, float width, std::size_t n_visible) const;
};

} // namespace spectr
