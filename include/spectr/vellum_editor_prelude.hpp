#pragma once

// The installed Pulp package's generated editor hook calls Processor's
// descriptor before including the processor definition. Keep that generated
// source usable with SDKs that forward-declare Processor in editor_host.hpp.
#include <pulp/format/processor.hpp>
