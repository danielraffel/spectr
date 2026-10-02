#include "spectr/spectr.hpp"
#include <pulp/format/aax_entry.hpp>

// AAX is an opt-in local build (SPECTR_ENABLE_AAX); the Avid SDK is
// developer-supplied and never part of this repository.
//
// SPECTR_AAX_CUSTOM_EDITOR selects which UI Pro Tools shows:
//   1  Spectr's own editor, through Pulp's AAX_CEffectGUI shell.
//   0  Pro Tools' auto-generated parameter strip (no editor registered).
// The custom editor is Spectr's product surface, so it is the default when
// AAX is enabled; the strip stays one cache flip away
// (-DSPECTR_AAX_EDITOR=strip) for isolating a host problem to the editor.
#if SPECTR_AAX_CUSTOM_EDITOR
PULP_AAX_PLUGIN_WITH_GUI(spectr::create_spectr)
#else
PULP_AAX_PLUGIN(spectr::create_spectr)
#endif
