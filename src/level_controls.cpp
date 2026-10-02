// Spectr's level controls on the processor: the editor Range (editor state),
// and the editor allow-list entries for Intensity and Auto Gain live in
// param_surface.cpp beside Mix and Output. See include/spectr/level_controls.hpp.

#include "spectr/spectr.hpp"

#include <mutex>

namespace spectr {

int Spectr::editor_range_db() const noexcept {
    std::lock_guard<std::mutex> lock(processing_state_mutex_);
    return editor_range_db_;
}

bool Spectr::set_editor_range_db(int range_db) noexcept {
    if (!valid_editor_range_db(range_db)) return false;
    std::lock_guard<std::mutex> lock(processing_state_mutex_);
    editor_range_db_ = range_db;
    return true;
}

} // namespace spectr
