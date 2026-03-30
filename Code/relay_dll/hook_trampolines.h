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

/// Returns the original (pre-hook) SetPosition function pointer.
/// Used by DrainCommandQueue to call the real game function.
void* GetRealSetPosition();

/// Returns resolved game function pointers for actor spawning (Phase 15).
/// Used by DrainCommandQueue CMD_SPAWN_ACTOR handler.
void* GetFormById();          ///< TESForm::GetById (addr 14617) -- signature: void*(uint32_t)
void* GetGameHeapAllocate();  ///< GameHeap::Allocate (addr 68115) -- signature: void*(void*, size_t, size_t, bool)
void* GetGameHeapInstance();  ///< GameHeap singleton (addr 400188) -- returns void** (dereference to get instance)
void* GetCharacterCtor();     ///< Character constructor (addr 40245) -- signature: void*(void*)
void* GetSpawnNewREFR();      ///< SpawnNewREFR (addr 13723) -- 12-param ModManager thiscall
void* GetModManagerInstance(); ///< ModManager singleton (addr 400269) -- returns void** (dereference to get instance)
void* GetSetLevelMod();       ///< ExtraDataList::SetLevelMod (addr 11806) -- signature: void(void*, uint32_t)

/// Drain and execute queued commands on the game thread.
/// Called from HookActorProcess when the player character (formId 0x14) processes.
void DrainCommandQueue();
