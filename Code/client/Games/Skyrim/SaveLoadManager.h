#pragma once

#include <filesystem>

// The engine's saves and loads (CommonLibSSE-NG BGSSaveLoadManager; docs/re/systems/save-game.md).
struct BGSSaveLoadManager
{
    static BGSSaveLoadManager* Get() noexcept;

    // Save_Impl(2, 0, name) (35727): a named save, written before it returns; false when the engine refused.
    bool Save(const char* apName) noexcept;
    // Load_Impl(name, -1, 0, true) (35728): starts loading the save (TESLoadGameEvent follows).
    bool Load(const char* apName) noexcept;

    // Where a save of that name is written (.ess): BSWin32SaveDataSystemUtility::PrepareFileSavePath (109357).
    [[nodiscard]] static std::filesystem::path SaveFilePath(const char* apName) noexcept;
};
