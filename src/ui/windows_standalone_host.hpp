#pragma once

#if defined(_WIN32)
namespace spectr {

/// Install the Windows standalone window host used by Spectr.exe.
///
/// Pulp's Windows SDK currently provides the embedded PluginViewHost but does
/// not install a top-level WindowHost factory. The standalone app needs both:
/// this adapter owns the top-level HWND and embeds the same GPU plugin host
/// used by REAPER inside it.
void install_windows_standalone_window_host();

} // namespace spectr
#endif
