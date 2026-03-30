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

// ---------------------------------------------------------------------------
// Game function resolvers for TESNPC::Create and Actor::Create pipelines.
// These return resolved addresses from the Address Library.
// Must be called AFTER InstallAllHooks() (which loads the Address Library).
// Returns nullptr if the address ID was not found.
// ---------------------------------------------------------------------------

/// TESForm::GetById (addr 14617) -- void* __cdecl GetFormById(uint32_t aFormId)
void* GetFormById();

/// GameHeap::Allocate (addr 68115) -- void* __fastcall Allocate(GameHeap*, size_t, size_t, bool)
void* GetGameHeapAllocate();

/// GameHeap singleton (addr 400188) -- pointer to GameHeap instance
void* GetGameHeapInstance();

/// Character constructor (addr 40245) -- Character* __fastcall CharacterCtor(Character*)
void* GetCharacterCtor();

/// ModManager::SpawnNewREFR (addr 13723) -- spawn actor into world
void* GetSpawnNewREFR();

/// ModManager singleton (addr 400269) -- pointer to ModManager instance
void* GetModManagerInstance();

/// Actor::SetLevelMod (addr 11806) -- void __fastcall SetLevelMod(ExtraDataList*, uint32_t)
void* GetSetLevelMod();

/// IFormFactory table (addr 400508) -- returns uintptr_t to factory pointer array
/// Usage: factories[43] gives IFormFactory* for TESNPC (FormType::NPC)
void* GetIFormFactoryTable();
