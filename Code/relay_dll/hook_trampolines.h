#pragma once

// Hook trampoline management for the relay DLL.
// Installs MinHook trampolines on game functions identified by Address Library IDs.
// Each trampoline forwards raw arguments over TCP -- no game struct headers included (D-14).

/// Install all MinHook trampolines. Call after TCP server is started.
/// Loads Address Library .bin file to resolve hook target addresses.
/// Returns true if all hooks were installed successfully.
bool InstallAllHooks();

/// Remove all hooks and uninitialize MinHook.
void RemoveAllHooks();
