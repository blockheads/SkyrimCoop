#include <TiltedOnlinePCH.h>
#include <HookSwitch.h>

#include <Games/TES.h>

#include <Actor.h>
#include <SaveLoad.h>
#include <TESObjectREFR.h>

#include <atomic>

ModManager* ModManager::Get() noexcept
{
    POINTER_SKYRIMSE(ModManager*, modManager, 400269);

    return *modManager.Get();
}

uint32_t Mod::GetFormId(uint32_t aBaseId) const noexcept
{
    if (IsLite())
    {
        aBaseId &= 0xFFF;
        aBaseId |= 0xFE000000;
        aBaseId |= uint32_t(liteId) << 12;
    }
    else
    {
        aBaseId &= 0xFFFFFF;
        aBaseId |= uint32_t(standardId) << 24;
    }

    return aBaseId;
}

using TSpawnNewREFR = uint32_t&(ModManager*, uint32_t& aRefHandleOut, TESForm* apBaseForm, NiPoint3* apPosition, NiPoint3* apRotation, TESObjectCELL* apParentCell, TESWorldSpace* apWorldSpace, Actor* apActor, uintptr_t a9, uintptr_t a10, char aForcePersist, char a12);
TSpawnNewREFR* RealSpawnNewREFR;

static std::atomic<void (*)(uint32_t)> s_createdWatcher{nullptr};

uint32_t& SpawnNewREFR(ModManager* apThis, uint32_t& aRefHandleOut, TESForm* apBaseForm, NiPoint3* apPosition, NiPoint3* apRotation, TESObjectCELL* apParentCell, TESWorldSpace* apWorldSpace, Actor* apActor, uintptr_t a9, uintptr_t a10, char aForcePersist, char a12)
{
    uint32_t& handle = RealSpawnNewREFR(apThis, aRefHandleOut, apBaseForm, apPosition, apRotation, apParentCell, apWorldSpace, apActor, a9, a10, aForcePersist, a12);
    const BGSSaveLoadGame* pSaveLoad = BGSSaveLoadGame::Get();
    if (const auto cWatcher = s_createdWatcher.load(std::memory_order_acquire); cWatcher && pSaveLoad && !pSaveLoad->IsSavingOrLoading())
        if (const TESObjectREFR* pRef = TESObjectREFR::GetByHandle(handle))
            cWatcher(pRef->formID);
    return handle;
}

uint32_t ModManager::CreateReference(TESBoundObject* apBase, const NiPoint3& acPosition, const NiPoint3& acRotation, TESObjectCELL* apCell,
                                     TESWorldSpace* apWorldSpace) noexcept
{
    uint32_t handle = 0;
    NiPoint3 position = acPosition;
    NiPoint3 rotation = acRotation;
    uint32_t noRoom = 0;
    RealSpawnNewREFR(this, handle, apBase, &position, &rotation, apCell, apWorldSpace, nullptr, 0, reinterpret_cast<uintptr_t>(&noRoom), static_cast<char>(0), static_cast<char>(1));
    return handle;
}

void ModManager::WatchCreated(void (*apWatcher)(uint32_t aFormId)) noexcept
{
    s_createdWatcher.store(apWatcher, std::memory_order_release);
}

uint32_t ModManager::Spawn(NiPoint3& aPosition, NiPoint3& aRotation, TESObjectCELL* apParentCell, TESWorldSpace* apWorldSpace, Actor* apCharacter) noexcept
{
    uint32_t refrHandle = 0;

    RealSpawnNewREFR(this, refrHandle, apCharacter->baseForm, &aPosition, &aRotation, apParentCell, apWorldSpace, apCharacter, 0, 0, static_cast<char>(0), static_cast<char>(1));

    return refrHandle;
}

Mod* ModManager::GetByName(const char* acpName) const noexcept
{
    auto pEntry = &mods.entry;

    while (pEntry && pEntry->data)
    {
        if (_stricmp(acpName, pEntry->data->filename) == 0)
            return pEntry->data;

        pEntry = pEntry->next;
    }

    return nullptr;
}

TESObjectCELL* ModManager::GetCellFromCoordinates(int32_t aX, int32_t aY, TESWorldSpace* aWorldSpace, bool aSpawnCell) noexcept
{
    using TModManager = TESObjectCELL*(ModManager*, int32_t, int32_t, TESWorldSpace*, bool);
    POINTER_SKYRIMSE(TModManager, getCell, 13718);

    return getCell(this, aX, aY, aWorldSpace, aSpawnCell);
}

static Hooks::Initializer s_tesHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        POINTER_SKYRIMSE(TSpawnNewREFR, s_realSpawnNewREFR, 13723);

        RealSpawnNewREFR = s_realSpawnNewREFR.Get();

        HOOK(&RealSpawnNewREFR, SpawnNewREFR);
    });
