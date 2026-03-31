#pragma once

#include <cstdint>
#include <string>
#include <vector>

/// TESForm lookups and typed Cast<> operations.
/// No SKSE types in any signature — all IDs use uint32_t.
class IFormBridge
{
public:
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

    // --- Mod registry ---
    virtual uint32_t GetModCount() const noexcept = 0;
    virtual std::string GetModNameByIndex(uint32_t aIndex) const noexcept = 0;
    virtual uint32_t GetModStandardId(const std::string& aModName) const noexcept = 0;
    virtual std::vector<std::string> GetAllModNames() const noexcept = 0;
    virtual uint32_t GetModFormIdBySpellId(uint32_t aModStandardId, uint32_t aSpellLocalId) const noexcept = 0;

    // --- Typed lookup helpers ---
    virtual bool IsSpellItem(uint32_t aFormId) const noexcept = 0;
    virtual bool IsConcentrationSpell(uint32_t aFormId) const noexcept = 0;
    virtual bool IsHealingSpell(uint32_t aFormId) const noexcept = 0;
    virtual bool IsWardSpell(uint32_t aFormId) const noexcept = 0;
    virtual bool IsInvisibilitySpell(uint32_t aFormId) const noexcept = 0;
    virtual bool IsMagicItem(uint32_t aFormId) const noexcept = 0;
};
