#pragma once

// Hook trampoline management for the relay DLL.
// Installs MinHook trampolines on game functions identified by Address Library IDs.
// Each trampoline forwards raw arguments over TCP -- no game struct headers included (D-14).

/// Install hook trampolines. If aEssentialOnly is true, only installs the
/// minimal set needed for actor sync (lifecycle + position). Default: true.
/// Loads Address Library .bin file to resolve hook target addresses.
/// Returns true if hooks were installed successfully.
bool InstallAllHooks(bool aEssentialOnly = true);

/// Remove all hooks and uninitialize MinHook.
void RemoveAllHooks();
