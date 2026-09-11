#pragma once

// Appearance detectors that assert what a person actually sees, rather than a
// proxy for it. A value reaching the runtime is not a user seeing correct text,
// so these read the laid-out View tree: absolute boxes for collisions, and
// measured-vs-laid-out width for text that cannot fit the box it was given.

#include <pulp/canvas/recording_canvas.hpp>
#include <pulp/view/geometry.hpp>
#include <pulp/view/inspector.hpp>
#include <pulp/view/view.hpp>
#include <pulp/view/widgets.hpp>
#include <pulp/view/virtual_grid.hpp>
#include <pulp/view/virtual_list.hpp>

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

namespace spectr::appearance {

/// One Label's text, in the two rects that answer two different questions.
///
/// `box` is the slot layout gave the text — the right thing to ask "did this fit
/// where it was put?". `ink` is the span the glyphs actually cover, which is
/// narrower whenever the slot is padded or shared with a sibling element, and is
/// the right thing to ask "do these two collide?". Conflating them makes the fit
/// check compare a measurement against itself and always pass.
struct TextBox {
    const pulp::view::Label* label = nullptr;
    pulp::view::Rect box{};
    pulp::view::Rect ink{};
    std::string text;
};

/// True when the view or any ancestor is hidden or fully transparent. A hidden
/// subtree is not on screen, so it can neither collide nor clip.
inline bool hidden_in_tree(const pulp::view::View& view) {
    for (const auto* node = &view; node != nullptr; node = node->parent()) {
        if (!node->visible()) return true;
        if (node->opacity() <= 0.01f) return true;
    }
    return false;
}

inline pulp::view::Rect intersect(const pulp::view::Rect& a,
                                  const pulp::view::Rect& b) {
    const float x = std::max(a.x, b.x);
    const float y = std::max(a.y, b.y);
    const float right = std::min(a.right(), b.right());
    const float bottom = std::min(a.bottom(), b.bottom());
    if (right <= x || bottom <= y) return pulp::view::Rect{0.0f, 0.0f, 0.0f, 0.0f};
    return pulp::view::Rect{x, y, right - x, bottom - y};
}

/// The part of `rect` that survives every clipping ancestor of `view`.
///
/// `absolute_bounds` walks the parent chain and reports where a node WOULD sit,
/// ignoring that an `overflow: hidden`/`scroll` ancestor crops it. Inside a
/// scrolling panel that is the difference between a box on screen and a box
/// scrolled far out of it: the settings body is 1246px tall inside a 679px
/// panel, so most of its rows report positions over unrelated chrome. Without
/// this, every scrolling surface manufactures collisions that no one can see.
inline pulp::view::Rect visible_rect(const pulp::view::View& view,
                                     pulp::view::Rect rect) {
    for (const auto* node = view.parent(); node != nullptr;
         node = node->parent()) {
        if (node->overflow() == pulp::view::View::Overflow::visible) continue;
        rect = intersect(rect, pulp::view::ViewInspector::absolute_bounds(*node));
        if (rect.width <= 0.0f || rect.height <= 0.0f) return rect;
    }
    return rect;
}

inline bool is_ancestor_of(const pulp::view::View& maybe_ancestor,
                           const pulp::view::View& node) {
    for (const auto* cur = node.parent(); cur != nullptr; cur = cur->parent())
        if (cur == &maybe_ancestor) return true;
    return false;
}

/// The absolute rect this Label's glyphs actually occupy.
///
/// A Label that also has element children wraps its bare text in an anonymous
/// inline box (`own_text_box()`, in the Label's LOCAL coordinates) rather than
/// using its whole content area. Measuring the Label's full bounds in that case
/// compares the text against a box it never had, which both hides real clipping
/// and invents overlaps between a container and its own children.
inline pulp::view::Rect text_rect(const pulp::view::Label& label) {
    const auto absolute = pulp::view::ViewInspector::absolute_bounds(label);
    if (!label.has_own_text_box()) return absolute;
    const auto own = label.own_text_box();
    return pulp::view::Rect{absolute.x + own.x, absolute.y + own.y, own.width,
                            own.height};
}

/// Narrow a text box to the horizontal extent the glyphs actually occupy.
///
/// A Label's box is a layout slot, not ink. A one-glyph button label can own a
/// 35px box and paint its "A" at the right-hand end of it, so two such boxes
/// touch while nothing a person sees does. Comparing boxes reports that as a
/// collision; comparing ink does not. `paint()` records the draw origin, and
/// `intrinsic_width()` is documented to measure with the same text-transform
/// paint uses, so together they give the real span. Vertical extent stays the
/// box: labels that collide do so on a shared row, and the box is already right
/// there.
inline pulp::view::Rect ink_rect(const pulp::view::Label& label,
                                 const pulp::view::Rect& box) {
    pulp::canvas::RecordingCanvas canvas;
    const_cast<pulp::view::Label&>(label).paint(canvas);
    const pulp::canvas::DrawCommand* only_text = nullptr;
    for (const auto& command : canvas.commands()) {
        const bool is_text
            = command.type == pulp::canvas::DrawCommand::Type::fill_text
              || command.type == pulp::canvas::DrawCommand::Type::stroke_text;
        if (!is_text) continue;
        // More than one run (wrapped or per-range styled) — the single-advance
        // model does not describe it, so keep the box and stay conservative.
        if (only_text != nullptr) return box;
        only_text = &command;
    }
    if (only_text == nullptr) return pulp::view::Rect{0.0f, 0.0f, 0.0f, 0.0f};
    const float width = label.intrinsic_width();
    if (width <= 0.0f) return box;
    const float left = box.x + only_text->f[0];
    return pulp::view::Rect{left, box.y, width, box.height};
}

inline void collect_text_boxes(const pulp::view::View& view,
                               std::vector<TextBox>& out) {
    if (const auto* label = dynamic_cast<const pulp::view::Label*>(&view)) {
        if (!label->text().empty() && !hidden_in_tree(*label)) {
            const auto box = visible_rect(*label, text_rect(*label));
            if (box.width > 0.0f && box.height > 0.0f) {
                const auto ink = ink_rect(*label, box);
                if (ink.width > 0.0f && ink.height > 0.0f)
                    out.push_back(TextBox{label, box, ink, label->text()});
            }
        }
    }
    for (std::size_t i = 0; i < view.child_count(); ++i)
        collect_text_boxes(*view.child_at(i), out);
}

inline std::vector<TextBox> text_boxes(const pulp::view::View& root) {
    std::vector<TextBox> boxes;
    collect_text_boxes(root, boxes);
    return boxes;
}

/// Detector 1 — two text boxes must not overlap.
///
/// Reports every pair of visible, non-nested text boxes whose intersection is
/// larger than `min_overlap_px` in BOTH axes, so edge-touching and sub-pixel
/// rounding do not register as collisions.
inline std::vector<std::string> detect_overlapping_text(
    const pulp::view::View& root, float min_overlap_px = 0.5f) {
    const auto boxes = text_boxes(root);
    std::vector<std::string> findings;
    for (std::size_t i = 0; i < boxes.size(); ++i) {
        for (std::size_t j = i + 1; j < boxes.size(); ++j) {
            const auto& a = boxes[i];
            const auto& b = boxes[j];
            if (is_ancestor_of(*a.label, *b.label)
                || is_ancestor_of(*b.label, *a.label))
                continue;
            const float dx = std::min(a.ink.right(), b.ink.right())
                             - std::max(a.ink.x, b.ink.x);
            const float dy = std::min(a.ink.bottom(), b.ink.bottom())
                             - std::max(a.ink.y, b.ink.y);
            if (dx <= min_overlap_px || dy <= min_overlap_px) continue;
            std::ostringstream message;
            message << "text overlaps by " << dx << "x" << dy << "px: \""
                    << a.text << "\" at (" << a.ink.x << "," << a.ink.y << " "
                    << a.ink.width << "x" << a.ink.height << ") vs \"" << b.text
                    << "\" at (" << b.ink.x << "," << b.ink.y << " "
                    << b.ink.width << "x" << b.ink.height << ")";
            findings.push_back(message.str());
        }
    }
    return findings;
}

/// Detector 2 — painted (measured) width must fit the laid-out box.
///
/// `Label::intrinsic_width()` is the width the text actually measures;
/// `bounds().width` is the width layout gave it. When the measurement exceeds
/// the box the glyphs cannot all be drawn, which is the visible defect: a label
/// clipped mid-word, or one whose captured line layout is discarded because the
/// text no longer matches the width it was measured at.
inline std::vector<std::string> detect_clipped_text(
    const pulp::view::View& root, float slack_px = 0.5f) {
    const auto boxes = text_boxes(root);
    std::vector<std::string> findings;
    for (const auto& entry : boxes) {
        const float measured = entry.label->intrinsic_width();
        if (measured <= entry.box.width + slack_px) continue;
        std::ostringstream message;
        message << "text does not fit its box: \"" << entry.text
                << "\" measures " << measured << "px but was laid out at "
                << entry.box.width << "px (overflow "
                << (measured - entry.box.width) << "px) at (" << entry.box.x
                << "," << entry.box.y << ")";
        findings.push_back(message.str());
    }
    return findings;
}

/// One Label's text against the slot layout gave it, measured the way paint
/// measures.
///
/// `box` is the ABSOLUTE laid-out slot and is deliberately NOT cropped by any
/// clipping ancestor. Whether the text fits its slot is a property of the
/// layout, not of where the panel happens to be scrolled: a settings row parked
/// below the fold is still truncated the moment the user scrolls to it. The
/// collision detectors crop, and must, because two boxes can only collide where
/// both are on screen; a fit check that crops instead measures the clip.
struct SlotFit {
    const pulp::view::Label* label = nullptr;
    pulp::view::Rect box{};
    std::string text;
    bool measured = false;
    float painted_width = 0.0f;
    float painted_height = 0.0f;
    int line_count = 0;
};

/// Every text-bearing Label in the tree, measured against its own slot.
///
/// Uses `painted_text_extents()`, not `intrinsic_width()`. The two answer
/// different questions and the difference is the whole point: `intrinsic_width()`
/// is a Yoga layout hint that returns 0 for a wrapped Label ON PURPOSE, so the
/// parent's available width drives wrapping. Any fit check built on it reads a
/// wrapped label as zero-width and passes it silently — agreement, not evidence.
/// `painted_text_extents()` runs the same shaper, text-transform, break mode and
/// line clamp that `paint()` runs, so it reports where the ink actually lands.
inline void collect_slot_fits(const pulp::view::View& view,
                              std::vector<SlotFit>& out) {
    if (const auto* label = dynamic_cast<const pulp::view::Label*>(&view)) {
        if (!label->text().empty() && !hidden_in_tree(*label)) {
            const auto box = text_rect(*label);
            if (box.width > 0.0f) {
                const auto extents = label->painted_text_extents(box.width);
                out.push_back(SlotFit{label, box, label->text(),
                                      extents.measured, extents.width,
                                      extents.height, extents.line_count});
            }
        }
    }
    for (std::size_t i = 0; i < view.child_count(); ++i)
        collect_slot_fits(*view.child_at(i), out);
}

inline std::vector<SlotFit> slot_fits(const pulp::view::View& root) {
    std::vector<SlotFit> fits;
    collect_slot_fits(root, fits);
    return fits;
}

/// Labels whose extents the shaper could not report.
///
/// `measured == false` means unknown, which is NOT the same as "fits". Counting
/// these is what keeps a green from this detector honest: a run of zeros because
/// nothing could be measured looks identical to a run of zeros because
/// everything fits.
inline std::vector<std::string> unmeasurable_slots(
    const pulp::view::View& root) {
    std::vector<std::string> findings;
    for (const auto& fit : slot_fits(root))
        if (!fit.measured)
            findings.push_back("extents unknown for \"" + fit.text + "\"");
    return findings;
}

/// Detector 3 — painted ink must fit the WIDTH of the slot layout gave the Label.
///
/// Fires when the shaped glyphs run past the slot, which is the visible defect
/// "this text is truncated".
///
/// Worth knowing where this can and cannot fire. A Label that Yoga sized FROM
/// its own text measure has a slot equal to its measurement by construction, so
/// the check compares a number against itself and can never fail there. It bites
/// exactly where the parent pins the slot — a fixed `width`, a `max_width`, a
/// `flex_shrink: 0` column — which is where truncation actually comes from.
///
/// Deliberately width-only. See `describe_tall_slots()` for why the height axis
/// is reported but not gated.
inline std::vector<std::string> detect_text_overflowing_slot(
    const pulp::view::View& root, float slack_px = 0.5f) {
    std::vector<std::string> findings;
    for (const auto& fit : slot_fits(root)) {
        if (!fit.measured) continue;
        if (fit.painted_width <= fit.box.width + slack_px) continue;
        std::ostringstream message;
        message << "text is cut off horizontally: \"" << fit.text
                << "\" paints " << fit.painted_width
                << "px of ink in a " << fit.box.width << "px slot (overflow "
                << (fit.painted_width - fit.box.width) << "px) at ("
                << fit.box.x << "," << fit.box.y << ")";
        findings.push_back(message.str());
    }
    return findings;
}

/// Diagnostic, NOT a gate — Labels whose ink is taller than their slot.
///
/// The height axis cannot be asserted the way width can. `painted_text_extents`
/// reports the font's full line height, which includes half-leading above the
/// ascent and below the descent, while the slot is the layout line box Yoga
/// gave the Label. Ink height exceeding box height is therefore the ORDINARY
/// case for a single-line label — on this surface roughly a quarter of all
/// labels report it — and a gate on it would fire on text that is not clipped
/// at all. What is worth a human's eye is a slot holding more than one shaped
/// line in the room for one, so the line count is reported alongside.
inline std::vector<std::string> describe_tall_slots(
    const pulp::view::View& root, float slack_px = 0.5f) {
    std::vector<std::string> notes;
    for (const auto& fit : slot_fits(root)) {
        if (!fit.measured) continue;
        if (fit.painted_height <= fit.box.height + slack_px) continue;
        std::ostringstream message;
        message << "\"" << fit.text << "\" paints " << fit.painted_height
                << "px over " << fit.line_count << " line(s) in a "
                << fit.box.height << "px slot at (" << fit.box.x << ","
                << fit.box.y << ")";
        notes.push_back(message.str());
    }
    return notes;
}

/// Detector 4 — no widget in the tree may paint a scrollbar.
///
/// Pulp's plain `View` paints no scrollbar chrome: `overflow: scroll` is
/// forwarded to Yoga for descendant measurement and otherwise clips like
/// `hidden` (`pulp/view/view.hpp`, Overflow). The widgets that DO paint one are
/// the virtualised containers, so a scrollbar can only reach this surface by one
/// of them being in the tree. Their `scrollbar_visible()` is private, so this
/// reports the reachable fact — the painter is present — rather than claiming to
/// read a bar that cannot be queried from outside.
inline std::vector<std::string> detect_scrollbar_painters(
    const pulp::view::View& root) {
    std::vector<std::string> findings;
    const auto walk = [&findings](const pulp::view::View& view,
                                  auto&& self) -> void {
        const bool paints_scrollbar
            = dynamic_cast<const pulp::view::VirtualList*>(&view) != nullptr
              || dynamic_cast<const pulp::view::VirtualGrid*>(&view) != nullptr;
        if (paints_scrollbar && !hidden_in_tree(view)) {
            const auto bounds = pulp::view::ViewInspector::absolute_bounds(view);
            std::ostringstream message;
            message << "a scrollbar-painting widget is in the tree at ("
                    << bounds.x << "," << bounds.y << " " << bounds.width << "x"
                    << bounds.height << ")";
            findings.push_back(message.str());
        }
        for (std::size_t i = 0; i < view.child_count(); ++i)
            self(*view.child_at(i), self);
    };
    walk(root, walk);
    return findings;
}

inline std::string join_findings(const std::vector<std::string>& findings) {
    std::ostringstream out;
    for (std::size_t i = 0; i < findings.size(); ++i) {
        if (i > 0) out << "\n";
        out << "  - " << findings[i];
    }
    return out.str();
}

} // namespace spectr::appearance
