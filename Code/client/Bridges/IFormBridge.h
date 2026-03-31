#pragma once

#include <cstdint>
#include <string>
#include <vector>

/// TESForm lookups and typed Cast<> operations.
/// No SKSE types in any signature — all IDs use uint32_t.
class IFormBridge
{
public:
    /// Flat mod descriptor returned by GetLoadedMods().
    struct ModEntry
    {
        uint16_t    Id{};       ///< Effective mod ID (lite or standard)
        bool        IsLite{};   ///< True if the mod is an ESL/lite plugin
        std::string Filename;   ///< Plugin filename (e.g. "Skyrim.esm")
    };

    virtual ~IFormBridge() = default;

    // --- Generic form queries ---
    virtual bool FormExists(uint32_t aFormId) const noexcept = 0;
    virtual uint8_t GetFormType(uint32_t aFormId) const noexcept = 0;
    virtual std::string GetFormEditorId(uint32_t aFormId) const noexcept = 0;
    virtual std::string GetFormName(uint32_t aFormId) const noexcept = 0;

    // --- TESGlobal ---
    virtual float GetGlobalValue(uint32_t aFormId) const noexcept = 0;
    virtual void SetGlobalValue(uint32_t aFormId, float aValue) noexcept = 0;

    // --- Cell / world ---
    virtual uint32_t LookupCellByCoords(uint32_t aWorldSpaceFormId, int32_t aX, int32_t aY) const noexcept = 0;
    virtual uint32_t LookupWorldSpaceByFormId(uint32_t aFormId) const noexcept = 0;
    virtual uint32_t GetParentCellFormId(uint32_t aObjectFormId) const noexcept = 0;
    virtual uint32_t GetParentWorldSpaceFormId(uint32_t aObjectFormId) const noexcept = 0;

    // --- Quest ---
    virtual bool QuestExists(uint32_t aFormId) const noexcept = 0;
    virtual uint32_t GetQuestStage(uint32_t aFormId) const noexcept = 0;
    virtual bool IsQuestStopped(uint32_t aFormId) const noexcept = 0;
    virtual uint8_t GetQuestType(uint32_t aFormId) const noexcept = 0;
    virtual std::string GetQuestName(uint32_t aFormId) const noexcept = 0;
    virtual void SetQuestActive(uint32_t aFormId, bool aActive) noexcept = 0;
    virtual uint32_t FindQuestByNameId(const std::string& aNameId) const noexcept = 0;
    /// Programmatically set quest stage (equivalent to TESQuest::ScriptSetStage).
    virtual void ScriptSetQuestStage(uint32_t aFormId, uint32_t aStage) noexcept = 0;
    /// Force-stop a quest (equivalent to TESQuest::SetStopped).
    virtual void SetQuestStopped(uint32_t aFormId) noexcept = 0;
    /// Returns true if the quest has no stage entries (non-syncable quest check).
    virtual bool IsQuestStagesEmpty(uint32_t aFormId) const noexcept = 0;
    /// Returns true if a ScopedQuestOverride is currently active.
    /// Used by QuestService to suppress duplicate quest events during remote updates.
    virtual bool IsQuestOverrideActive() const noexcept = 0;

    // --- Mod registry ---
    virtual uint32_t GetModCount() const noexcept = 0;
    virtual std::string GetModNameByIndex(uint32_t aIndex) const noexcept = 0;
    virtual uint32_t GetModStandardId(const std::string& aModName) const noexcept = 0;
    virtual std::vector<std::string> GetAllModNames() const noexcept = 0;
    virtual uint32_t GetModFormIdBySpellId(uint32_t aModStandardId, uint32_t aSpellLocalId) const noexcept = 0;
    /// Returns a flat list of all currently loaded mods (IsLoaded() == true).
    /// Used by TransportService to build the authentication mod list without touching ModManager directly.
    virtual std::vector<ModEntry> GetLoadedMods() const noexcept = 0;

    // --- Typed lookup helpers ---
    virtual bool IsSpellItem(uint32_t aFormId) const noexcept = 0;
    virtual bool IsConcentrationSpell(uint32_t aFormId) const noexcept = 0;
    virtual bool IsHealingSpell(uint32_t aFormId) const noexcept = 0;
    virtual bool IsWardSpell(uint32_t aFormId) const noexcept = 0;
    virtual bool IsInvisibilitySpell(uint32_t aFormId) const noexcept = 0;
    virtual bool IsMagicItem(uint32_t aFormId) const noexcept = 0;
};
