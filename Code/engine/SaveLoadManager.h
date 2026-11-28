#pragma once

#include <filesystem>
#include <Primitives.h>
#include <Misc/BSFixedString.h>
#include <join/SaveList.h>
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

// A save's file while the engine writes or reads it (IDA 1.7.104, no CommonLibSSE-NG type;
// docs/re/systems/save-game.md, "The file's compression").
struct BGSSaveLoadFile
{
    uint8_t pad000[0xBB0];
    const char* fileName; // the save's name: no folder, no extension
    uint8_t padBB8[0x8];
    uint32_t uncompressedLength;
    uint32_t bodyOffset; // where the body starts: after the header and the screenshot
    uint16_t compressionType; // 0 none, 1 zlib, 2 LZ4
    bool bodyDecoded;
};
static_assert(offsetof(BGSSaveLoadFile, fileName) == 0xBB0);
static_assert(offsetof(BGSSaveLoadFile, uncompressedLength) == 0xBC0);
static_assert(offsetof(BGSSaveLoadFile, bodyOffset) == 0xBC4);
static_assert(offsetof(BGSSaveLoadFile, compressionType) == 0xBC8);
static_assert(offsetof(BGSSaveLoadFile, bodyDecoded) == 0xBCA);

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
    // SetCurrentCharacter (35751): current and display character {id, modded}, as a load sets them from the save's
    // name. Both count: a modded flag other than the game's makes the next save add a catalogue entry (35749).
    void SetCurrentCharacter(uint32_t aCharacterId, bool aModded) noexcept;
    static BGSSaveLoadManager* Get() noexcept;
    // The Journal's save lists from now on (SaveListHooks.cpp: BuildSaveList 35743, the character rows 52917).
    static void LimitSaveList(SaveList::Limit aLimit) noexcept;
    // ClearSaveList (35746): saveGameList emptied, to be built again.
    void ClearSaveList() noexcept;

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
