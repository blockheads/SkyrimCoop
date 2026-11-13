#pragma once

#include <filesystem>
#include <Games/Primitives.h>
#include <Misc/BSFixedString.h>
#include <optional>
#include <string>

// The engine's saves and loads (CommonLibSSE-NG BGSSaveLoadManager; docs/re/systems/save-game.md).
struct GameplaySaveEntry
{
    BSFixedString fileName;
    uint8_t pad08[0x48];
    uint64_t fileTime;
    uint64_t saveTime;
    uint8_t pad60[0xC];
    uint32_t characterId;
    uint32_t saveType;
    uint32_t pad74;
};
static_assert(offsetof(GameplaySaveEntry, fileTime) == 0x50);
static_assert(offsetof(GameplaySaveEntry, characterId) == 0x6C);
static_assert(sizeof(GameplaySaveEntry) == 0x78);

struct BGSSaveLoadManager
{
    uint8_t pad00[0xD0];
    // Set by a load from the save's file name (0 for a name without the engine's fields), saves filed under it.
    uint32_t currentCharacterId;
    uint32_t currentCharacterModded;
    uint8_t padD8[0x28];
    GameArray<GameplaySaveEntry*> saveGameList;
    // CommonLibSSE-NG SaveEntry: per-character catalogue, including the exact save quickload selects.
    struct SaveEntry
    {
        uint8_t pad00[0x40];
        BSFixedString lastQuicksave;
    };
    static_assert(offsetof(SaveEntry, lastQuicksave) == 0x40);
    static_assert(sizeof(SaveEntry) == 0x48);
    [[nodiscard]] std::string QuickSaveName() noexcept;
    [[nodiscard]] std::optional<std::string> PreviousGameplaySave() noexcept;
    // SetCurrentCharacter (35751): current and display character, as a load sets them; the modded flag kept.
    void SetCurrentCharacter(uint32_t aCharacterId) noexcept;
    static BGSSaveLoadManager* Get() noexcept;

    // Save_Impl(2, 0, name) (35727): a named save, written before it returns; false when the engine refused.
    bool Save(const char* apName) noexcept;
    // Internal party load through Load_Impl (35728), bypassing the player-load gate; TESLoadGameEvent follows.
    bool Load(const char* apName) noexcept;

    // Where a save of that name is written (.ess): BSWin32SaveDataSystemUtility::PrepareFileSavePath (109357).
    [[nodiscard]] static std::filesystem::path SaveFilePath(const char* apName) noexcept;
};

static_assert(offsetof(BGSSaveLoadManager, currentCharacterId) == 0xD0);
static_assert(offsetof(BGSSaveLoadManager, currentCharacterModded) == 0xD4);
static_assert(offsetof(BGSSaveLoadManager, saveGameList) == 0x100);
