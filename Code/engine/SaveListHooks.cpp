// The Journal's Load lists only the saves SaveList allows (docs/re/systems/save-game.md, "Only this playthrough's saves").
#include <SaveLoadManager.h>

#include <HookSwitch.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <Interface/GFxArray.h>
#include <VersionDb.h>

#include <atomic>

namespace
{
std::atomic s_limit{SaveList::Limit::kAll};

// The Journal's list holder (IDA 1.7.104: 52890 and 52894 fill this entryList).
struct UISaveLoadManager
{
    uint8_t pad00[0x30];
    GFxValue entryList;
};
static_assert(offsetof(UISaveLoadManager, entryList) == 0x30);

// The functor FillCharacterList (52890) hands PushCharacterRows (52917).
struct CharacterRows
{
    void* movie;
    GFxValue* row;
    GFxValue* id;
    GFxValue* flags;
    UISaveLoadManager* manager;
};

// BuildSaveList (35743): saveGameList for a filter {id, flags}: flags bit 1 filters by id, bit 2 is the modded flag
// (35924), bit 0 picks the folder.
using TBuildSaveList = uint64_t(BGSSaveLoadManager*, const uint32_t*);
TBuildSaveList* RealBuildSaveList = nullptr;

uint64_t HookBuildSaveList(BGSSaveLoadManager* apManager, const uint32_t* apFilter)
{
    const auto cLimit = s_limit.load();
    if (cLimit == SaveList::Limit::kAll || !apFilter)
        return RealBuildSaveList(apManager, apFilter);
    const uint32_t cFilter[2]{apManager->currentCharacterId, (apFilter[1] & 1u) | 2u | (apManager->currentCharacterModded ? 4u : 0u)};
    const uint64_t cResult = RealBuildSaveList(apManager, cFilter);
    if (cLimit == SaveList::Limit::kNothing)
        apManager->ClearSaveList();
    return cResult;
}

// PushCharacterRows (52917): the Journal's character rows; the "Show all saves" row is already in.
using TPushRows = bool(void*, CharacterRows*);
TPushRows* RealPushRows = nullptr;

bool HookPushRows(void* apMap, CharacterRows* apRows)
{
    const bool cResult = RealPushRows(apMap, apRows);
    const auto cLimit = s_limit.load();
    const auto* pManager = BGSSaveLoadManager::Get();
    if (cLimit == SaveList::Limit::kAll || !apRows || !apRows->manager || !pManager)
        return cResult;
    const GFxValue& cList = apRows->manager->entryList;
    for (uint32_t i = GFxArray::Size(cList); i-- > 0;)
    {
        // Ids are numbers: a character's as a signed int, "Show all saves" as 4294967295.
        const auto cId = GFxArray::NumberOf(cList, i, "id");
        const uint32_t cCharacter = !cId ? 0 : *cId < 0 ? static_cast<uint32_t>(static_cast<int32_t>(*cId)) : static_cast<uint32_t>(*cId);
        if (!cId || !SaveList::Shows(cLimit, pManager->currentCharacterId, cCharacter))
            GFxArray::Remove(cList, i);
    }
    return cResult;
}

Hooks::Initializer s_hooks([] {
    const HookGate cGate(__FILE__);
    HOOK_ID(35743, &RealBuildSaveList, HookBuildSaveList);
    HOOK_ID(52917, &RealPushRows, HookPushRows);
});
} // namespace

void BGSSaveLoadManager::LimitSaveList(SaveList::Limit aLimit) noexcept
{
    s_limit = aLimit;
}

void BGSSaveLoadManager::ClearSaveList() noexcept
{
    // 35746: frees saveGameList's entries and marks the list unbuilt (+0x118).
    using TClear = void(BGSSaveLoadManager*, GameArray<GameplaySaveEntry*>*);
    POINTER_SKYRIMSE(TClear, s_clear, 35746);
    s_clear(this, &saveGameList);
}
