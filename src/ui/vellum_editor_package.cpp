#include "pulp-vellum/editor-packages/SpectrVellumEditor.hpp"

// Force the generated package header and the root view to compile together.
// The target is intentionally an object-only check until the model-backed
// session replaces the native scripted editor in a later port slice.
namespace {
[[maybe_unused]] auto* const kSpectrVellumFactory =
    pulp::vellum::generated::SpectrVellumEditor();
}
