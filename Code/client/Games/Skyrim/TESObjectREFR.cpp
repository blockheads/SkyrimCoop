#include <Games/Memory.h>
#include <Games/Animation/GraphReference.h>
#include <Games/Animation/VariableSync.h>
#include <Games/Animation/ReplayState.h>
#include <Havok/AnimationGraphState.h>
#include <atomic>
#include <cstring>
#include <SaveLoad.h>
#include <HookSwitch.h>

#include <Games/References.h>
#include <Games/Overrides.h>
#include <Games/ItemChanges.h>
#include <ExtraData/ExtraOwnership.h>

#include <World.h>
#include <Services/PapyrusService.h>
#include <Events/ActivateEvent.h>
#include <Games/Skyrim/Papyrus/GuestScripts.h>
#include <Events/InventoryChangeEvent.h>
#include <Events/ScriptAnimationEvent.h>
#include <Events/LockChangeEvent.h>
#include <Events/ReferenceJumpEvent.h>

#include <ExtraData/ExtraContainerChanges.h>
#include <Services/ContainerTransfers.h>
#include <Interface/UI.h>
#include <ExtraData/ExtraDataList.h>
#include <ExtraData/ExtraCharge.h>
#include <ExtraData/ExtraCount.h>
#include <ExtraData/ExtraEnchantment.h>
#include <ExtraData/ExtraHealth.h>
#include <ExtraData/ExtraPoison.h>
#include <ExtraData/ExtraSoul.h>
#include <ExtraData/ExtraTextDisplayData.h>
#include <ExtraData/ExtraWorn.h>
#include <ExtraData/ExtraWornLeft.h>
#include <Forms/EnchantmentItem.h>
#include <Forms/AlchemyItem.h>
#include <ItemIds.h>
#include <EquipManager.h>
#include <DefaultObjectManager.h>
#include <BSAnimationGraphManager.h>
#include <Havok/BShkbAnimationGraph.h>
#include <Havok/hkbBehaviorGraph.h>
#include <Havok/hkbVariableValueSet.h>
#include <Havok/hkbStateMachine.h>
#include <Forms/TESObjectCELL.h>
#include <Forms/TESWorldSpace.h>

#include <Structs/AnimationGraphDescriptorManager.h>
#include <Structs/AnimationVariables.h>

extern const AnimationGraphDescriptor* BehaviorVarPatch(BSAnimationGraphManager* pManager, Actor* pActor);

using TActivate = bool(TESObjectREFR*, TESObjectREFR* apActivator, uint8_t aUnk1, TESBoundObject* apObjectToGet, int32_t aCount, char aDefaultProcessing);
using TAddInventoryItem = void(TESObjectREFR*, TESBoundObject* apItem, ExtraDataList* apExtraData, int32_t aCount, TESObjectREFR* apOldOwner);
using TRemoveInventoryItem = BSPointerHandle<TESObjectREFR>*(TESObjectREFR*, BSPointerHandle<TESObjectREFR>* apResult, TESBoundObject* apItem, int32_t aCount, ITEM_REMOVE_REASON aReason, ExtraDataList* apExtraList, TESObjectREFR* apMoveToRef, const NiPoint3* apDropLoc, const NiPoint3* apRotate);
// InventoryChanges::RemoveItem (16059, CommonLibSSE-NG): apRef is the inventory's owner, apDropLoc a reference.
using TRemoveFromInventory = BSPointerHandle<TESObjectREFR>*(ExtraContainerChanges::Data*, BSPointerHandle<TESObjectREFR>* apResult, TESObjectREFR* apRef, TESBoundObject* apItem, int32_t aCount,
                                                            ITEM_REMOVE_REASON aReason, ExtraDataList* apExtraList, TESObjectREFR* apMoveToRef, const NiPoint3* apDropLoc, TESObjectREFR* apDropRef);
// TESObjectREFR::MoveTo_Impl (56626: a script's MoveTo, SetPosition and SetAngle), the console's setpos and setangle
// (22053, 22054). Their callers read no result.
using TMoveTo = void(TESObjectREFR*, void* apTargetHandle, TESObjectCELL* apCell, TESWorldSpace* apWorldSpace, const NiPoint3& acPosition, const NiPoint3& acRotation);
using TSetOnAxis = void(TESObjectREFR*, char aAxis, float aValue);
using TPlayAnimationAndWait = bool(void*, uint32_t auiStackID, TESObjectREFR* apSelf, BSFixedString* apAnimation, BSFixedString* apEventName);
using TPlayAnimation = bool(void*, uint32_t auiStackID, TESObjectREFR* apSelf, BSFixedString* apEventName);
using TRotate = void(TESObjectREFR*, float aAngle);
using TLockChange = void(TESObjectREFR*);

static TActivate* RealActivate = nullptr;
static TAddInventoryItem* RealAddInventoryItem = nullptr;
static TRemoveInventoryItem* RealRemoveInventoryItem = nullptr;
static TRemoveFromInventory* RealRemoveFromInventory = nullptr;
static TMoveTo* RealMoveTo = nullptr;
static TSetOnAxis* RealSetPositionOnAxis = nullptr;
static TSetOnAxis* RealSetAngleOnAxis = nullptr;
static TPlayAnimationAndWait* RealPlayAnimationAndWait = nullptr;
static TPlayAnimation* RealPlayAnimation = nullptr;
static TRotate* RealRotateX = nullptr;
static TRotate* RealRotateY = nullptr;
static TRotate* RealRotateZ = nullptr;
static TLockChange* RealLockChange = nullptr;

#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <RTTI.h>
#include <Utils.h>
#include <FormIds.h>
#include <Wire/Buffer.h>
#include <VersionDb.h>

TESObjectREFR* TESObjectREFR::GetByHandle(uint32_t aHandle) noexcept
{
    TESObjectREFR* pResult = nullptr;

    using TGetRefrByHandle = void(uint32_t & aHandle, TESObjectREFR * &apResult);

    POINTER_SKYRIMSE(TGetRefrByHandle, s_getRefrByHandle, 17201);

    s_getRefrByHandle.Get()(aHandle, pResult);

    if (pResult)
        pResult->handleRefObject.DecRefHandle();

    return pResult;
}

BSPointerHandle<TESObjectREFR> TESObjectREFR::GetHandle() const noexcept
{
    using TGetHandle = BSPointerHandle<TESObjectREFR>(const TESObjectREFR*, BSPointerHandle<TESObjectREFR>* apResult);
    POINTER_SKYRIMSE(TGetHandle, s_getHandle, 19846);

    BSPointerHandle<TESObjectREFR> result{};
    s_getHandle(this, &result);

    return result;
}

uint32_t* TESObjectREFR::GetNullHandle() noexcept
{
    POINTER_SKYRIMSE(uint32_t, s_nullHandle, 400312);

    return s_nullHandle.Get();
}

TESObjectREFR* TESObjectREFR::LoadFromPlugin(uint32_t aRefId, uint32_t aCellId, int16_t aGridX, int16_t aGridY, TESForm* apOwner) noexcept
{
    // An entry of the promote list (BGSLocation 18359's unique references): the grid packs x high, y low.
    struct Entry
    {
        uint32_t RefId;
        uint32_t CellId;
        uint32_t Grid;
    };
    static_assert(sizeof(Entry) == 12);
    using TPromoteList = void(void*, GameArray<Entry>*, TESForm*, GameArray<uint32_t>*);
    POINTER_SKYRIMSE(TPromoteList, promoteList, 15328);
    POINTER_SKYRIMSE(void*, s_persistence, 400326);

    if (auto* pLoaded = Cast<TESObjectREFR>(TESForm::GetById(aRefId)))
        return pLoaded;
    void* pManager = *s_persistence.Get();
    if (!pManager || !aCellId)
        return nullptr;
    Entry entry{aRefId, aCellId, (static_cast<uint32_t>(static_cast<uint16_t>(aGridX)) << 16) | static_cast<uint16_t>(aGridY)};
    GameArray<Entry> entries;
    entries.data = &entry;
    entries.capacity = entries.length = 1;
    GameArray<uint32_t> handles; // grown by the engine
    promoteList(pManager, &entries, apOwner, &handles);
    if (handles.data)
        Memory::Free(handles.data);
    return Cast<TESObjectREFR>(TESForm::GetById(aRefId));
}

void TESObjectREFR::SetRotation(float aX, float aY, float aZ) noexcept
{
    RealRotateX(this, aX);
    RealRotateY(this, aY);
    RealRotateZ(this, aZ);
}

using Wire::Serialization;

// aCall with the reference's graph manager, held for it; false if it has none.
template <class T> static bool WithGraphs(const TESObjectREFR& acRef, T&& aCall) noexcept
{
    return WithGraphReference<BSAnimationGraphManager>(
        [&](BSAnimationGraphManager** apManager) { return EngineVirtual(acRef.animationGraphHolder).GetBSAnimationGraph(apManager); },
        std::forward<T>(aCall));
}

bool TESObjectREFR::SaveGraphs(std::string& aGraphs) const noexcept
{
    aGraphs.clear();
    return WithGraphs(*this, [&aGraphs](BSAnimationGraphManager* apManager) { return AnimationGraphState::Save(apManager, aGraphs); });
}

bool TESObjectREFR::LoadGraphs(const std::string& acGraphs) const noexcept
{
    return WithGraphs(*this, [&acGraphs](BSAnimationGraphManager* apManager) { return AnimationGraphState::Load(apManager, acGraphs); });
}

const char* TESObjectREFR::GetDisplayFullName() noexcept
{
    POINTER_SKYRIMSE(const char*(TESObjectREFR*), s_getDisplayFullName, 19781);
    const char* cpName = s_getDisplayFullName(this);
    return cpName ? cpName : "";
}

// The actor's state flags (weapon, sitting, life state) go first: the graph and they must agree.
bool TESObjectREFR::SaveAnimationGraphState(std::string& aState) const noexcept
{
    aState.clear();
    const auto* pActor = Cast<Actor>(this);
    std::string graphs;
    if (!pActor || !SaveGraphs(graphs))
        return false;
    const uint32_t cFlags[2]{pActor->actorState.flags1, pActor->actorState.flags2};
    aState.append(reinterpret_cast<const char*>(cFlags), sizeof(cFlags));
    aState += graphs;
    return true;
}

bool TESObjectREFR::LoadAnimationGraphState(const std::string& acState) const noexcept
{
    auto* pActor = Cast<Actor>(const_cast<TESObjectREFR*>(this));
    uint32_t flags[2]{};
    if (!pActor || acState.size() < sizeof(flags))
        return false;
    std::memcpy(flags, acState.data(), sizeof(flags));
    const uint32_t cPreviousFlags2 = pActor->actorState.flags2;
    const bool cLoaded = LoadGraphs(acState.substr(sizeof(flags)));
    if (cLoaded)
    {
        pActor->actorState.flags1 = flags[0];
        // Weapon choice has its live owner messages; a graph snapshot's transition is historical.
        pActor->actorState.flags2 = pActor->GetExtension()->IsCopy() ? ReplayState::Flags2(flags[1], cPreviousFlags2) : flags[1];
    }
    return cLoaded;
}

// Called with the manager locked: its selected graph and descriptor must describe the same project.
static const AnimationGraphDescriptor* DescriptorFor(BSAnimationGraphManager& aManager, Actor& aActor, int aIndex)
{
    auto* pExtension = aActor.GetExtension();
    if (!pExtension)
        return nullptr;
    const auto cProject = aManager.GetProjectKey(aIndex);
    if (VariableSync::NeedsDescriptor(pExtension->GraphDescriptorHash, pExtension->GraphDescriptorProjectKey, cProject))
    {
        pExtension->GraphDescriptorHash = aManager.GetDescriptorKey(aIndex);
        pExtension->GraphDescriptorProjectKey = cProject;
    }
    auto* pDescriptor = AnimationGraphDescriptorManager::Get().GetDescriptor(pExtension->GraphDescriptorHash);
    return pDescriptor ? pDescriptor : BehaviorVarPatch(&aManager, &aActor);
}

static std::span<const hkbVariableInfo> VariableInfos(const hkbBehaviorGraph& acGraph)
{
    const auto* pData = acGraph.data;
    return pData && pData->variableInfos.data && pData->variableInfos.size > 0
               ? std::span<const hkbVariableInfo>(pData->variableInfos.data, pData->variableInfos.size)
               : std::span<const hkbVariableInfo>{};
}

// An actor's graph aIndex (-1: the selected one) with its descriptor, its variable words and their types, the manager
// locked while aWork runs.
template <class T> static bool WithVariables(const TESObjectREFR& acRef, int aIndex, T&& aWork) noexcept
{
    return WithGraphs(acRef, [&](BSAnimationGraphManager* apManager) {
        BSScopedLock<BSRecursiveLock> lock{apManager->lock};
        auto* pActor = Cast<Actor>(const_cast<TESObjectREFR*>(&acRef));
        const auto* pGraph = apManager->GetGraph(aIndex);
        if (!pActor || !pGraph || !pGraph->behaviorGraph)
            return false;
        const auto* pBehavior = pGraph->behaviorGraph;
        auto* pValues = pBehavior->animationVariables;
        const auto* pDescriptor = DescriptorFor(*apManager, *pActor, aIndex);
        if (!pDescriptor || !pValues || !pValues->data)
            return false;
        aWork(*pDescriptor, std::span<uint32_t>(pValues->data, pValues->size), VariableInfos(*pBehavior));
        return true;
    });
}

void TESObjectREFR::SaveAnimationVariables(AnimationVariables& aVariables) const noexcept
{
    // The player's third-person graph (0), whichever is selected.
    WithVariables(*this, formID == 0x14 ? 0 : -1, [&](const AnimationGraphDescriptor& acDescriptor, std::span<uint32_t> aWords, auto acInfos) {
        aVariables = VariableSync::Read(acDescriptor, aWords, acInfos);
    });
}

void TESObjectREFR::LoadAnimationVariables(const AnimationVariables& aVariables) const noexcept
{
    WithVariables(*this, -1, [&](const AnimationGraphDescriptor& acDescriptor, std::span<uint32_t> aWords, auto acInfos) {
        VariableSync::Write(acDescriptor, aWords, acInfos, aVariables);
    });
}

uint32_t TESObjectREFR::GetCellId() const noexcept
{
    if (!parentCell)
        return 0;

    const auto* pWorldSpace = parentCell->worldspace;

    return pWorldSpace != nullptr ? pWorldSpace->formID : parentCell->formID;
}

TESWorldSpace* TESObjectREFR::GetWorldSpace() const noexcept
{
    auto* pParentCell = parentCell ? parentCell : GetParentCell();
    if (pParentCell && !(pParentCell->cellFlags[0] & 1))
        return pParentCell->worldspace;

    return nullptr;
}

ExtraDataList* TESObjectREFR::GetExtraDataList() noexcept
{
    return &extraData;
}

// Delete() should only be used on temporaries
void TESObjectREFR::Delete() const noexcept
{
    using ObjectReference = TESObjectREFR;

    PAPYRUS_FUNCTION(void, ObjectReference, Delete);

    s_pDelete(this);
}

void TESObjectREFR::Disable() const noexcept
{
    using ObjectReference = TESObjectREFR;

    PAPYRUS_FUNCTION(void, ObjectReference, Disable, bool);

    s_pDisable(this, true);
}

void TESObjectREFR::Enable() const noexcept
{
    using ObjectReference = TESObjectREFR;

    PAPYRUS_FUNCTION(void, ObjectReference, Enable, bool);

    s_pEnable(this, true);
}

// Skyrim: MoveTo() can fail, causing the object to be deleted
void TESObjectREFR::MoveTo(TESObjectCELL* apCell, const NiPoint3& acPosition) const noexcept
{
    ScopedReferencesOverride recursionGuard;

    using TInternalMoveTo = bool(const TESObjectREFR*, uint32_t*&, TESObjectCELL*, TESWorldSpace*, const NiPoint3&, const NiPoint3&);

    POINTER_SKYRIMSE(TInternalMoveTo, s_internalMoveTo, 56626);

    uint32_t* pHandle = GetNullHandle();
    s_internalMoveTo(this, pHandle, apCell, apCell->worldspace, acPosition, rotation);
}

void TESObjectREFR::SetPositionOnAxis(char aAxis, float aValue) noexcept
{
    using TSetOnAxis = void(TESObjectREFR*, char, float);
    POINTER_SKYRIMSE(TSetOnAxis, s_setPositionOnAxis, 22053);
    s_setPositionOnAxis(this, aAxis, aValue);
}

void TESObjectREFR::SetAngleOnAxis(char aAxis, float aDegrees) noexcept
{
    using TSetOnAxis = void(TESObjectREFR*, char, float);
    POINTER_SKYRIMSE(TSetOnAxis, s_setAngleOnAxis, 22054);
    s_setAngleOnAxis(this, aAxis, aDegrees);
    // It sets the 3D's local rotation only: its world transforms follow, as MoveTo does (NiAVObject::Update, 70251).
    if (NiAVObject* p3D = GetNiNode())
        p3D->Update();
}

void TESObjectREFR::SetScale(float aScale) noexcept
{
    using TSetScale = void(TESObjectREFR*, float);
    POINTER_SKYRIMSE(TSetScale, s_setScale, 19665);
    s_setScale(this, aScale);
}

void TESObjectREFR::SetOwner(TESForm* apOwner) noexcept
{
    using TSetOwner = void(TESObjectREFR*, TESForm*);
    POINTER_SKYRIMSE(TSetOwner, s_setOwner, 20198);
    s_setOwner(this, apOwner);
}

TESForm* TESObjectREFR::GetOwner() const noexcept
{
    const auto* pOwnership = static_cast<ExtraOwnership*>(extraData.GetByType(ExtraDataType::Ownership));
    return pOwnership ? pOwnership->owner : nullptr;
}

void TESObjectREFR::RecordMove(bool aMoved) noexcept
{
    if (aMoved && formType != FormType::Character)
        MarkChanged(CHANGE_REFR_MOVE);
}

Lock* TESObjectREFR::GetLock() const noexcept
{
    using TGetLock = Lock*(const TESObjectREFR*);
    POINTER_SKYRIMSE(TGetLock, realGetLock, 20223);

    return realGetLock(this);
}

Lock* TESObjectREFR::CreateLock() noexcept
{
    using TCreateLock = Lock*(TESObjectREFR*);
    POINTER_SKYRIMSE(TCreateLock, realCreateLock, 20221);

    return realCreateLock(this);
}

void TESObjectREFR::LockChange() noexcept
{
    RealLockChange(this);
}

const float TESObjectREFR::GetHeight() noexcept
{
    auto boundMax = GetBoundMax();
    return boundMax.z - GetBoundMin().z;
}

TESObjectREFR::OpenState TESObjectREFR::GetOpenState() noexcept
{
    using ObjectReference = TESObjectREFR;

    PAPYRUS_FUNCTION(TESObjectREFR::OpenState, ObjectReference, GetOpenState);

    return s_pGetOpenState(this);
}

ExtraContainerChanges::Data* TESObjectREFR::GetContainerChanges() const noexcept
{
    using TGetContainterChanges = ExtraContainerChanges::Data*(const TESObjectREFR*);

    POINTER_SKYRIMSE(TGetContainterChanges, s_getContainerChangs, 16040);

    return s_getContainerChangs(this);
}

void TESObjectREFR::RemoveAllItems() noexcept
{
    using ObjectReference = TESObjectREFR;

    PAPYRUS_FUNCTION(void, ObjectReference, RemoveAllItems, TESObjectREFR*, bool, bool);

    ScopedEquipOverride equipOverride;

    s_pRemoveAllItems(this, nullptr, false, true);
}

TESContainer* TESObjectREFR::GetContainer() const noexcept
{
    using TGetContainer = TESContainer*(const TESObjectREFR*);

    POINTER_SKYRIMSE(TGetContainer, s_getContainer, 19702);

    return s_getContainer(this);
}

int64_t TESObjectREFR::GetItemCountInInventory(TESForm* apItem) const noexcept
{
    int64_t count = GetContainer()->GetItemCount(apItem);

    auto* pContainerChanges = GetContainerChanges()->entries;
    for (auto pChange : *pContainerChanges)
    {
        if (pChange && pChange->form)
        {
            if (pChange->form->formID == apItem->formID)
            {
                count += pChange->count;
                break;
            }
        }
    }

    return count;
}

ExtraContainerChanges::Data* TESObjectREFR::FindContainerChanges() const noexcept
{
    using TGetInventoryChanges = ExtraContainerChanges::Data*(const TESObjectREFR*);
    POINTER_SKYRIMSE(TGetInventoryChanges, s_getInventoryChanges, 16039);
    return s_getInventoryChanges(this);
}

std::optional<int32_t> TESObjectREFR::GetContainerCount(TESBoundObject* apItem) const noexcept
{
    using TGetItemCount = int32_t(ExtraContainerChanges::Data*, TESBoundObject*);
    POINTER_SKYRIMSE(TGetItemCount, s_getItemCount, 16108); // InventoryChanges::GetItemCount

    const TESContainer* pContainer = GetContainer();
    if (!apItem || !pContainer)
        return std::nullopt;
    // No changes yet (nothing touched it): its base container's, leveled entries not rolled (14529 returns an int).
    auto* pChanges = FindContainerChanges();
    if (!pChanges)
        return std::abs(static_cast<int32_t>(pContainer->GetItemCount(apItem)));
    return std::abs(s_getItemCount(pChanges, apItem));
}

bool TESObjectREFR::SetContainerCount(TESBoundObject* apItem, int32_t aCount) noexcept
{
    const auto cHeld = GetContainerCount(apItem);
    if (!cHeld)
        return false;
    ScopedInventoryOverride _;
    if (aCount > *cHeld)
        AddObjectToContainer(apItem, nullptr, aCount - *cHeld, nullptr);
    else if (aCount < *cHeld)
        RemoveItem(apItem, *cHeld - aCount, ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
    return true;
}

std::vector<std::pair<TESBoundObject*, int32_t>> TESObjectREFR::GetContainerItems() const noexcept
{
    std::vector<std::pair<TESBoundObject*, int32_t>> items;
    const TESContainer* pContainer = GetContainer();
    if (!pContainer)
        return items;
    std::vector<TESBoundObject*> forms;
    for (uint32_t i = 0; i < pContainer->count; ++i)
        if (pContainer->entries[i] && pContainer->entries[i]->form && pContainer->entries[i]->form->formType != FormType::LeveledItem)
            if (auto* pItem = Cast<TESBoundObject>(pContainer->entries[i]->form))
                forms.push_back(pItem);
    if (const auto* pChanges = FindContainerChanges(); pChanges && pChanges->entries)
        for (const auto* pEntry : *pChanges->entries)
            if (pEntry && pEntry->form)
                if (auto* pItem = Cast<TESBoundObject>(pEntry->form))
                    forms.push_back(pItem);
    for (TESBoundObject* pItem : forms)
    {
        if (std::ranges::any_of(items, [pItem](const auto& acItem) { return acItem.first == pItem; }))
            continue;
        if (const auto cCount = GetContainerCount(pItem))
            items.emplace_back(pItem, *cCount);
    }
    return items;
}

uint32_t TESObjectREFR::CountLeveledEntries() const noexcept
{
    const TESContainer* pContainer = GetContainer();
    uint32_t count = 0;
    for (uint32_t i = 0; pContainer && i < pContainer->count; ++i)
        if (pContainer->entries[i] && pContainer->entries[i]->form && pContainer->entries[i]->form->formType == FormType::LeveledItem)
            ++count;
    return count;
}

bool TESObjectREFR::InitInventoryIfRequired() noexcept
{
    using TInitInventoryIfRequired = bool(TESObjectREFR*, bool);
    POINTER_SKYRIMSE(TInitInventoryIfRequired, s_initInventoryIfRequired, 16038);
    return s_initInventoryIfRequired(this, false);
}

void TESObjectREFR::SetBaseObject(TESBoundObject* apBase) noexcept
{
    // Actor's override (37466) also sets its race from the new NPC base.
    SetBaseForm(apBase);
    MarkChanged(CHANGE_REFR_BASEOBJECT);
    if (!GetNiNode() || IsDisabled())
        return;
    // The 3D is the old base's: dropped and loaded again as Disable and Enable do, leaving no form-flags record.
    const BGSSaveLoadGame* pSaveLoad = BGSSaveLoadGame::Get();
    const bool cRecorded = pSaveLoad && pSaveLoad->HasChange(*this, CHANGE_FORM_FLAGS);
    // The reference's own Disable (19801): Actor's (37267) only queues it, which would land after the Enable.
    using TDisable = void(TESObjectREFR*);
    POINTER_SKYRIMSE(TDisable, s_disable, 19801);
    s_disable(this);
    EnableImpl();
    if (!cRecorded)
        UnsetChanged(CHANGE_FORM_FLAGS);
}

TESObjectCELL* TESObjectREFR::GetParentCellEx() const noexcept
{
    return parentCell ? parentCell : GetParentCell();
}

void TESObjectREFR::GetItemFromExtraData(Inventory::Entry& arEntry, ExtraDataList* apExtraDataList) noexcept
{

    if (ExtraCount* pExtraCount = Cast<ExtraCount>(apExtraDataList->GetByType(ExtraDataType::Count)))
    {
        arEntry.Count = pExtraCount->count;
    }

    if (ExtraCharge* pExtraCharge = Cast<ExtraCharge>(apExtraDataList->GetByType(ExtraDataType::Charge)))
    {
        arEntry.ExtraCharge = pExtraCharge->fCharge;
    }

    if (ExtraEnchantment* pExtraEnchantment = Cast<ExtraEnchantment>(apExtraDataList->GetByType(ExtraDataType::Enchantment)))
    {
        Utils::Assert(pExtraEnchantment->pEnchantment, "Null enchantment in ExtraEnchantment");

        arEntry.ExtraEnchantId = ToSessionOrCreated(pExtraEnchantment->pEnchantment->formID).value_or(GameId{});

        if (pExtraEnchantment->pEnchantment->formID & 0xFF000000)
        {
            arEntry.EnchantData.Effects = pExtraEnchantment->pEnchantment->SessionEffects().value_or(std::vector<Inventory::EffectItem>{});

            uint32_t objectId = ToGame(arEntry.BaseId);
            TESForm* pObject = TESForm::GetById(objectId);
            if (pObject)
                arEntry.EnchantData.IsWeapon = pObject->formType == FormType::Weapon;
        }

        arEntry.ExtraEnchantCharge = pExtraEnchantment->usCharge;
        arEntry.ExtraEnchantRemoveUnequip = pExtraEnchantment->bRemoveOnUnequip;
    }

    if (ExtraHealth* pExtraHealth = Cast<ExtraHealth>(apExtraDataList->GetByType(ExtraDataType::Health)))
    {
        arEntry.ExtraHealth = pExtraHealth->fHealth;
    }

    if (ExtraPoison* pExtraPoison = Cast<ExtraPoison>(apExtraDataList->GetByType(ExtraDataType::Poison)))
    {
        Utils::Assert(pExtraPoison->pPoison, "Null poison in ExtraPoison");
        if (pExtraPoison && pExtraPoison->pPoison)
        {
            arEntry.ExtraPoisonId = ItemId(pExtraPoison->pPoison->formID).value_or(GameId{});
            arEntry.ExtraPoisonCount = pExtraPoison->uiCount;
        }
    }

    if (ExtraSoul* pExtraSoul = Cast<ExtraSoul>(apExtraDataList->GetByType(ExtraDataType::Soul)))
    {
        arEntry.ExtraSoulLevel = (int32_t)pExtraSoul->cSoul;
    }

    // A player's name, without the temper suffix the engine appends.
    if (const auto* pText = static_cast<const ExtraTextDisplayData*>(apExtraDataList->GetByType(ExtraDataType::TextDisplayData));
        pText && pText->iOwnerInstance == ExtraTextDisplayData::kCustomName && pText->DisplayName.data)
    {
        const std::string_view cName(pText->DisplayName.data);
        arEntry.ExtraName = std::string(cName.substr(0, pText->usCustomNameLength ? pText->usCustomNameLength : cName.size()));
    }

    arEntry.ExtraWorn = apExtraDataList->Contains(ExtraDataType::Worn);
    arEntry.ExtraWornLeft = apExtraDataList->Contains(ExtraDataType::WornLeft);

    arEntry.IsQuestItem = apExtraDataList->HasQuestObjectAlias();
}

ExtraDataList* TESObjectREFR::FindCarriedStack(TESBoundObject& aObject, const Inventory::Entry& acEntry) noexcept
{
    if (!acEntry.ContainsExtraData())
        return nullptr;
    auto* pChanges = GetContainerChanges();
    if (!pChanges || !pChanges->entries)
        return nullptr;
    std::vector<ExtraDataList*> lists;
    std::vector<Inventory::Entry> stacks;
    for (auto* pEntry : *pChanges->entries)
        if (pEntry && pEntry->form == &aObject && pEntry->dataList)
            for (ExtraDataList* pList : *pEntry->dataList)
                if (pList)
                {
                    Inventory::Entry stack{};
                    stack.BaseId = acEntry.BaseId;
                    {
                        ScopedExtraDataOverride _;
                        GetItemFromExtraData(stack, pList);
                    }
                    lists.push_back(pList);
                    stacks.push_back(std::move(stack));
                }
    const auto cFound = ItemChanges::FindStack(stacks, acEntry);
    return cFound ? lists[*cFound] : nullptr;
}

ExtraDataList* TESObjectREFR::GetExtraDataFromItem(const Inventory::Entry& arEntry, bool aCounted) noexcept
{
    if (!arEntry.ContainsExtraData())
        return nullptr;

    ExtraDataList* pExtraDataList = ExtraDataList::New();
    SetItemExtraData(*pExtraDataList, arEntry);
    // A list stands for one item unless it holds a count: adding 2 with a list of none adds one plain.
    if (aCounted && arEntry.Count > 1)
        pExtraDataList->SetCount(static_cast<uint16_t>(std::min(arEntry.Count, 0xFFFF)));

    if (arEntry.ExtraWorn)
        pExtraDataList->SetWorn(false);
    if (arEntry.ExtraWornLeft)
        pExtraDataList->SetWorn(true);

    if (pExtraDataList->data == nullptr)
    {
        Memory::Delete(pExtraDataList->bitfield);
        Memory::Delete(pExtraDataList);
        pExtraDataList = nullptr;
    }

    return pExtraDataList;
}

void TESObjectREFR::SetItemExtraData(ExtraDataList& aList, const Inventory::Entry& acEntry) noexcept
{
    if (acEntry.ExtraCharge > 0.f)
        aList.SetChargeData(acEntry.ExtraCharge);

    if (acEntry.ExtraEnchantId != 0)
    {
        EnchantmentItem* pEnchantment = nullptr;
        if (acEntry.ExtraEnchantId.ModId == 0xFFFFFFFF)
            pEnchantment = EnchantmentItem::Create(acEntry.EnchantData);
        else
            pEnchantment = Cast<EnchantmentItem>(TESForm::GetById(ToGame(acEntry.ExtraEnchantId)));

        Utils::Assert(pEnchantment, "No Enchantment created or found.");
        if (pEnchantment)
            aList.SetEnchantmentData(pEnchantment, acEntry.ExtraEnchantCharge, acEntry.ExtraEnchantRemoveUnequip);
    }

    // A player-made poison by its recipe (ItemIds).
    if (acEntry.ExtraPoisonId != 0)
        if (AlchemyItem* pPoison = Cast<AlchemyItem>(TESForm::GetById(ItemForm(acEntry.ExtraPoisonId))))
            aList.SetPoison(pPoison, acEntry.ExtraPoisonCount);

    if (acEntry.ExtraHealth > 0.f)
        aList.SetHealth(acEntry.ExtraHealth);

    if (acEntry.ExtraSoulLevel > 0 && acEntry.ExtraSoulLevel <= 5)
        aList.SetSoulData(static_cast<SOUL_LEVEL>(acEntry.ExtraSoulLevel));

    if (!acEntry.ExtraName.empty())
        aList.SetCustomName(acEntry.ExtraName.c_str());
}

Inventory TESObjectREFR::GetInventory() const noexcept
{
    return GetInventory([](TESForm& aForm) { return true; });
}

Inventory TESObjectREFR::GetInventory(std::function<bool(TESForm&)> aFilter) const noexcept
{
    Inventory inventory{};

    if (TESContainer* pBaseContainer = GetContainer())
    {
        for (int i = 0; i < pBaseContainer->count; i++)
        {
            TESContainer::Entry* pGameEntry = pBaseContainer->entries[i];
            if (!pGameEntry || !pGameEntry->form)
            {
                spdlog::warn("Entry or form for inventory item is null.");
                continue;
            }

            if (!aFilter(*pGameEntry->form))
                continue;

            Inventory::Entry entry;
            entry.BaseId = ItemId(pGameEntry->form->formID).value_or(GameId{});
            entry.Count = pGameEntry->count;

            inventory.Entries.push_back(std::move(entry));
        }
    }

    Inventory extraInventory{};

    auto pExtraContChangesEntries = GetContainerChanges()->entries;
    for (auto pGameEntry : *pExtraContChangesEntries)
    {
        if (!pGameEntry)
            continue;

        if (!aFilter(*pGameEntry->form))
            continue;

        Inventory::Entry entry{};
        entry.BaseId = ItemId(pGameEntry->form->formID).value_or(GameId{});
        entry.Count = pGameEntry->count;

        for (ExtraDataList* pExtraDataList : *pGameEntry->dataList)
        {
            if (!pExtraDataList)
                continue;

            Inventory::Entry innerEntry;
            innerEntry.BaseId = entry.BaseId;
            innerEntry.Count = 1;

            GetItemFromExtraData(innerEntry, pExtraDataList);

            entry.Count -= innerEntry.Count;

            extraInventory.Entries.push_back(std::move(innerEntry));
        }

        if (entry.Count != 0)
            extraInventory.Entries.push_back(std::move(entry));
    }

    Inventory minimizedExtraInventory{};

    for (auto& entry : extraInventory.Entries)
    {
        auto duplicate = std::find_if(minimizedExtraInventory.Entries.begin(), minimizedExtraInventory.Entries.end(), [entry](const Inventory::Entry& newEntry) { return newEntry.CanBeMerged(entry); });

        if (duplicate == std::end(minimizedExtraInventory.Entries))
        {
            minimizedExtraInventory.Entries.push_back(entry);
            continue;
        }

        duplicate->Count += entry.Count;
    }

    for (auto& entry : minimizedExtraInventory.Entries)
    {
        if (entry.ContainsExtraData())
            continue;

        auto duplicate = std::find_if(inventory.Entries.begin(), inventory.Entries.end(), [entry](const Inventory::Entry& newEntry) { return newEntry.CanBeMerged(entry); });

        if (duplicate == std::end(inventory.Entries))
            continue;

        entry.Count += duplicate->Count;
        duplicate->Count = 0;
    }

    inventory.Entries.insert(inventory.Entries.end(), minimizedExtraInventory.Entries.begin(), minimizedExtraInventory.Entries.end());

    inventory.RemoveByFilter([](const auto& entry) { return entry.Count == 0; });

    return inventory;
}

Inventory TESObjectREFR::GetArmor() const noexcept
{
    return GetInventory([](TESForm& aForm) { return aForm.formType == FormType::Armor; });
}

Inventory TESObjectREFR::GetWornArmor() const noexcept
{
    Inventory wornArmor = GetArmor();
    wornArmor.RemoveByFilter([](const auto& entry) { return !entry.IsWorn(); });
    return wornArmor;
}

bool TESObjectREFR::IsItemInInventory(uint32_t aFormID) const noexcept
{
    Inventory inventory = GetInventory([aFormID](TESForm& aForm) { return aForm.formID == aFormID; });
    return !inventory.Entries.empty();
}

void TESObjectREFR::SetInventory(const Inventory& aInventory) noexcept
{
    spdlog::debug("Setting inventory for {:X}", formID);

    ScopedInventoryOverride _;

    RemoveAllItems();

    for (const Inventory::Entry& entry : aInventory.Entries)
    {
        if (entry.Count != 0)
            AddOrRemoveItem(entry, true);
    }
}

std::vector<uint32_t> TESObjectREFR::RemoveNonQuestItems(Inventory& aCurrentInventory) noexcept
{
    ScopedEquipOverride equipOverride_;

    std::vector<uint32_t> questEntries{};

    for (auto& entry : aCurrentInventory.Entries)
    {
        if (entry.IsQuestItem)
        {
            uint32_t gameId = ItemForm(entry.BaseId);
            questEntries.emplace_back(gameId);
            continue;
        }

        if (entry.Count <= 0)
            continue;

        entry.Count = -entry.Count;
        AddOrRemoveItem(entry, true);
    }

    return questEntries;
}

void TESObjectREFR::SetInventoryRetainingQuestItems(Inventory& aCurrentInventory, const Inventory& acSourceInventory) noexcept
{
    spdlog::debug("Setting inventory for {:X}", formID);

    ScopedInventoryOverride _;

    // Remove all non-quest items first
    std::vector<uint32_t> questItemIds = RemoveNonQuestItems(aCurrentInventory);

    for (const auto& entry : acSourceInventory.Entries)
    {
        uint32_t gameId = ItemForm(entry.BaseId);

        // If the item is not in the list of quest items, add it
        if (std::find(questItemIds.begin(), questItemIds.end(), gameId) == questItemIds.end())
        {
            if (entry.Count != 0)
                AddOrRemoveItem(entry, true);
        }
    }
}

void TESObjectREFR::AddOrRemoveItem(const Inventory::Entry& arEntry, bool aIsSettingInventory) noexcept
{

    uint32_t objectId = ItemForm(arEntry.BaseId);
    TESBoundObject* pObject = Cast<TESBoundObject>(TESForm::GetById(objectId));
    if (!pObject)
    {
        spdlog::warn("{}: Object to add not found, {:X}:{:X}.", __FUNCTION__, arEntry.BaseId.ModId, arEntry.BaseId.BaseId);
        return;
    }

    // A removal names the carried stack by its own list (16059 takes from no other); an add builds one from the entry.
    ExtraDataList* pExtraDataList = arEntry.Count > 0 ? GetExtraDataFromItem(arEntry, true) : FindCarriedStack(*pObject, arEntry);
    if (arEntry.Count < 0 && arEntry.ContainsExtraData() && !pExtraDataList)
    {
        spdlog::warn("{}: {:X} carries no stack of {:X} like the one to remove", __FUNCTION__, formID, pObject->formID);
        return;
    }

    if (arEntry.Count > 0)
    {
        bool isWorn = false;
        bool isWornLeft = false;
        if (pExtraDataList)
        {
            isWorn = pExtraDataList->Contains(ExtraDataType::Worn);
            isWornLeft = pExtraDataList->Contains(ExtraDataType::WornLeft);
        }

        spdlog::debug("Adding item {:X}, count {}", pObject->formID, arEntry.Count);
        AddObjectToContainer(pObject, pExtraDataList, arEntry.Count, nullptr);

        // TODO: check Actor cast first?
        if (isWorn)
            EquipManager::Get()->Equip(Cast<Actor>(this), pObject, nullptr, arEntry.Count, DefaultObjectManager::Get().rightEquipSlot, false, true, false, false);
        else if (isWornLeft)
            EquipManager::Get()->Equip(Cast<Actor>(this), pObject, nullptr, arEntry.Count, DefaultObjectManager::Get().leftEquipSlot, false, true, false, false);
    }
    else if (arEntry.Count < 0)
    {
        spdlog::debug("Removing item {:X}, count {}", pObject->formID, -arEntry.Count);
        RemoveItem(pObject, -arEntry.Count, ITEM_REMOVE_REASON::kRemove, pExtraDataList, nullptr);
    }

    UpdateItemList(nullptr);
}

void TESObjectREFR::UpdateItemList(TESForm* pUnkForm) noexcept
{
    using TUpdateItemList = void(TESObjectREFR*, TESForm*);
    POINTER_SKYRIMSE(TUpdateItemList, updateItemList, 52849);
    updateItemList(this, pUnkForm);
}

bool TESObjectREFR::Activate(TESObjectREFR* apActivator, uint8_t aUnk1, TESBoundObject* aObjectToGet, int32_t aCount, char aDefaultProcessing) noexcept
{
    ScopedActivateOverride _;

    return RealActivate(this, apActivator, aUnk1, aObjectToGet, aCount, aDefaultProcessing);
}

bool TESObjectREFR::SetLockState(bool aLocked, uint8_t aLockLevel) noexcept
{
    Lock* pLock = GetLock();
    if (!pLock && aLocked)
        pLock = CreateLock();
    if (!pLock)
        return !aLocked;
    pLock->lockLevel = aLockLevel;
    pLock->SetLock(aLocked);
    LockChange(); // AddLockChange (19512) past our hook
    return true;
}

void TESObjectREFR::EnableImpl() noexcept
{
    using TEnableImpl = void(TESObjectREFR*, bool aResetInventory);

    POINTER_SKYRIMSE(TEnableImpl, s_enable, 19800);

    s_enable(this, false);
}

uint32_t TESObjectREFR::GetAnimationVariableInt(BSFixedString* apVariableName) noexcept
{
    using ObjectReference = TESObjectREFR;

    PAPYRUS_FUNCTION(uint32_t, ObjectReference, GetAnimationVariableInt, BSFixedString*);

    return s_pGetAnimationVariableInt(this, apVariableName);
}

static thread_local bool s_cancelAnimationWaitEvent = false;

bool TESObjectREFR::PlayAnimationAndWait(BSFixedString* apAnimation, BSFixedString* apEventName) noexcept
{
    using ObjectReference = TESObjectREFR;

    PAPYRUS_FUNCTION(bool, ObjectReference, PlayAnimationAndWait, BSFixedString*, BSFixedString*);

    s_cancelAnimationWaitEvent = true;
    bool result = s_pPlayAnimationAndWait(this, apAnimation, apEventName);
    s_cancelAnimationWaitEvent = false;
    return result;
}

bool HookPlayAnimationAndWait(void* apThis, uint32_t auiStackID, TESObjectREFR* apSelf, BSFixedString* apAnimation, BSFixedString* apEventName)
{
    spdlog::debug("Animation: {}, EventName: {}", apAnimation->AsAscii(), apEventName->AsAscii());

    // Sent by every game, passed on from the host's only (HostAuthority::OnScriptAnimation).
    if (!s_cancelAnimationWaitEvent && !ScopedHostChangeOverride::IsOverriden() && (apSelf->formID < 0xFF000000))
        World::Get().GetRunner().Trigger(ScriptAnimationEvent(apSelf->formID, apAnimation->AsAscii(), apEventName->AsAscii()));

    return RealPlayAnimationAndWait(apThis, auiStackID, apSelf, apAnimation, apEventName);
}

static thread_local bool s_cancelAnimationEvent = false;

bool TESObjectREFR::PlayAnimation(BSFixedString* apEventName) noexcept
{
    using ObjectReference = TESObjectREFR;

    PAPYRUS_FUNCTION(bool, ObjectReference, PlayAnimation, BSFixedString*);

    s_cancelAnimationEvent = true;
    bool result = s_pPlayAnimation(this, apEventName);
    s_cancelAnimationEvent = false;
    return result;
}

bool TESObjectREFR::SendAnimationEvent(BSFixedString* apEventName) noexcept
{
    return EngineVirtual(animationGraphHolder).SendAnimationEvent(apEventName);
}

bool HookPlayAnimation(void* apThis, uint32_t auiStackID, TESObjectREFR* apSelf, BSFixedString* apEventName)
{
    spdlog::debug("EventName: {}", apEventName->AsAscii());

    if (!s_cancelAnimationEvent && !ScopedHostChangeOverride::IsOverriden() && (apSelf->formID < 0xFF000000))
        World::Get().GetRunner().Trigger(ScriptAnimationEvent(apSelf->formID, std::string{}, apEventName->AsAscii()));

    return RealPlayAnimation(apThis, auiStackID, apSelf, apEventName);
}

bool HookActivate(TESObjectREFR* apThis, TESObjectREFR* apActivator, uint8_t aUnk1, TESBoundObject* apObjectToGet, int32_t aCount, char aDefaultProcessing)
{
    Actor* pActivator = Cast<Actor>(apActivator);
    const bool cIsDoor = apThis->baseForm->formType == FormType::Door;

    // An actor's activation (not a book's: only reading one removes it from the cell), or a script's on a door
    // (by a reference: only the host's are passed on, HostAuthority::OnActivate).
    if (apActivator && !ScopedHostChangeOverride::IsOverriden() && (pActivator ? apThis->baseForm->formType != FormType::Book : cIsDoor))
    {
        const auto cOpenState = cIsDoor ? apThis->GetOpenState() : TESObjectREFR::kNone;
        ActivateEvent evt;
        evt.ObjectFormId = apThis->formID;
        evt.ActivatorFormId = apActivator->formID;
        evt.ByReference = !pActivator;
        TESObjectCELL* pCell = apThis->GetParentCellEx();
        evt.ObjectParentCellFormId = pCell ? pCell->formID : 0;
        evt.PreActivationOpenStateValue = static_cast<uint8_t>(cOpenState);
        World::Get().GetRunner().Trigger(evt);
    }

    // A door or container the player opens with its key unlocks inside (TESObjectDOOR/CONT::Activate).
    if (apActivator && apActivator == PlayerCharacter::Get())
    {
        ScopedPlayerLockOverride _;
        return RealActivate(apThis, apActivator, aUnk1, apObjectToGet, aCount, aDefaultProcessing);
    }
    return RealActivate(apThis, apActivator, aUnk1, apObjectToGet, aCount, aDefaultProcessing);
}

void HookAddInventoryItem(TESObjectREFR* apThis, TESBoundObject* apItem, ExtraDataList* apExtraData, int32_t aCount, TESObjectREFR* apOldOwner)
{
    if (!ScopedInventoryOverride::IsOverriden())
    {

        Inventory::Entry item{};
        item.BaseId = ItemId(apItem->formID).value_or(GameId{});
        item.Count = aCount;

        if (apExtraData)
            apThis->GetItemFromExtraData(item, apExtraData);

        InventoryChangeEvent event(apThis->formID, std::move(item));
        event.StoryGiven = GuestScripts::StoryGiving();
        World::Get().GetRunner().Trigger(std::move(event));
    }

    spdlog::debug("Adding inventory item {:X} to {:X}", apItem->formID, apThis->formID);

    RealAddInventoryItem(apThis, apItem, apExtraData, aCount, apOldOwner);
}

// An item leaving aRef's inventory, as InventoryService sends it.
void SendRemoval(TESObjectREFR* apRef, TESBoundObject* apItem, int32_t aCount, ExtraDataList* apExtraList) noexcept
{
    Inventory::Entry item{};
    item.BaseId = ItemId(apItem->formID).value_or(GameId{});
    if (apExtraList)
    {
        ScopedExtraDataOverride _;
        apRef->GetItemFromExtraData(item, apExtraList);
    }
    item.Count = -aCount;
    World::Get().GetRunner().Trigger(InventoryChangeEvent(apRef->formID, std::move(item)));
}

static std::atomic<bool> s_movesContainerItems{true};

bool ProposeContainerTransfer(TESObjectREFR* apSource, TESObjectREFR* apTarget, TESBoundObject* apItem,
                              int32_t aCount, ExtraDataList* apExtra, ITEM_REMOVE_REASON aReason)
{
    auto* pUI = UI::Get();
    if (s_movesContainerItems.load(std::memory_order_relaxed) || ScopedInventoryOverride::IsOverriden() || !apSource ||
        !apTarget || !apItem || !pUI || !pUI->GetMenuOpen("ContainerMenu"))
        return false;
    Inventory::Entry item;
    item.BaseId = ItemId(apItem->formID).value_or(GameId{});
    if (apExtra)
        TESObjectREFR::GetItemFromExtraData(item, apExtra);
    item.Count = aCount;
    return ContainerTransfers::Propose(World::Get(), apSource->formID, apTarget->formID, std::move(item),
                                      aReason == ITEM_REMOVE_REASON::kSteal);
}

BSPointerHandle<TESObjectREFR>*
HookRemoveInventoryItem(TESObjectREFR* apThis, BSPointerHandle<TESObjectREFR>* apResult, TESBoundObject* apItem, int32_t aCount, ITEM_REMOVE_REASON aReason, ExtraDataList* apExtraList, TESObjectREFR* apMoveToRef, const NiPoint3* apDropLoc, const NiPoint3* apRotate)
{
    if (ProposeContainerTransfer(apThis, apMoveToRef, apItem, aCount, apExtraList, aReason))
    {
        *apResult = {};
        return apResult;
    }
    if (!ScopedInventoryOverride::IsOverriden())
        SendRemoval(apThis, apItem, aCount, apExtraList);

    spdlog::debug("Removing inventory item {:X} from {:X}", apItem->formID, apThis->formID);

    ScopedItemRemovalOverride _;

    return RealRemoveInventoryItem(apThis, apResult, apItem, aCount, aReason, apExtraList, apMoveToRef, apDropLoc, apRotate);
}

// An actor put somewhere at once jumps there on the other games too (CharacterService, Movement::Teleported).
void RaiseJump(TESObjectREFR* apThis) noexcept
{
    if (apThis && Cast<Actor>(apThis))
        World::Get().GetRunner().Trigger(ReferenceJumpEvent{apThis->formID});
}

void HookMoveTo(TESObjectREFR* apThis, void* apTargetHandle, TESObjectCELL* apCell, TESWorldSpace* apWorldSpace, const NiPoint3& acPosition, const NiPoint3& acRotation)
{
    RealMoveTo(apThis, apTargetHandle, apCell, apWorldSpace, acPosition, acRotation);
    RaiseJump(apThis);
}

void HookSetPositionOnAxis(TESObjectREFR* apThis, char aAxis, float aValue)
{
    RealSetPositionOnAxis(apThis, aAxis, aValue);
    RaiseJump(apThis);
}

void HookSetAngleOnAxis(TESObjectREFR* apThis, char aAxis, float aValue)
{
    RealSetAngleOnAxis(apThis, aAxis, aValue);
    RaiseJump(apThis);
}

// A removal the engine makes on the inventory itself, not through TESObjectREFR::RemoveItem (the Training Menu's and a
// fine's gold, PayGoldToContainer 37511; a trapped soul's gem): sent as that one is.
BSPointerHandle<TESObjectREFR>* HookRemoveFromInventory(ExtraContainerChanges::Data* apThis, BSPointerHandle<TESObjectREFR>* apResult, TESObjectREFR* apRef, TESBoundObject* apItem,
                                                        int32_t aCount, ITEM_REMOVE_REASON aReason, ExtraDataList* apExtraList, TESObjectREFR* apMoveToRef, const NiPoint3* apDropLoc,
                                                        TESObjectREFR* apDropRef)
{
    TESObjectREFR* pOwner = apRef ? apRef : apThis ? apThis->parent : nullptr;
    if (!ScopedItemRemovalOverride::IsOverriden() &&
        ProposeContainerTransfer(pOwner, apMoveToRef, apItem, aCount, apExtraList, aReason))
    {
        *apResult = {};
        return apResult;
    }
    if (pOwner && apItem && aCount > 0 && !ScopedInventoryOverride::IsOverriden() && !ScopedItemRemovalOverride::IsOverriden())
        SendRemoval(pOwner, apItem, aCount, apExtraList);
    return RealRemoveFromInventory(apThis, apResult, apRef, apItem, aCount, aReason, apExtraList, apMoveToRef, apDropLoc, apDropRef);
}

void HookRotateX(TESObjectREFR* apThis, float aAngle)
{
    if (apThis->formType == Actor::Type)
    {
        const auto pActor = static_cast<Actor*>(apThis);
        // We don't allow remotes to move
        if (pActor->GetExtension()->IsCopy())
            return;
    }

    const float cBefore = apThis->rotation.x;
    RealRotateX(apThis, aAngle);
    apThis->RecordMove(apThis->rotation.x != cBefore);
}

void HookRotateY(TESObjectREFR* apThis, float aAngle)
{
    if (apThis->formType == Actor::Type)
    {
        const auto pActor = static_cast<Actor*>(apThis);
        // We don't allow remotes to move
        if (pActor->GetExtension()->IsCopy())
            return;
    }

    const float cBefore = apThis->rotation.y;
    RealRotateY(apThis, aAngle);
    apThis->RecordMove(apThis->rotation.y != cBefore);
}

void HookRotateZ(TESObjectREFR* apThis, float aAngle)
{
    if (apThis->formType == Actor::Type)
    {
        const auto pActor = static_cast<Actor*>(apThis);
        // We don't allow remotes to move, but where we let the engine (a copy lying in its ragdoll: HookTransformDelta)
        if (pActor->GetExtension()->IsCopy() && !ScopedReferencesOverride::IsOverriden())
            return;
    }

    const float cBefore = apThis->rotation.z;
    RealRotateZ(apThis, aAngle);
    apThis->RecordMove(apThis->rotation.z != cBefore);
}

void HookLockChange(TESObjectREFR* apThis)
{
    RealLockChange(apThis);
    if (ScopedHostChangeOverride::IsOverriden())
        return;
    const auto* pLock = apThis->GetLock();
    const bool cByPlayer = ScopedPlayerLockOverride::IsOverriden();
    if(pLock)
        World::Get().GetRunner().Trigger(LockChangeEvent(apThis->formID, pLock->IsLocked(), pLock->lockLevel, cByPlayer));
    else
        World::Get().GetRunner().Trigger(LockChangeEvent(apThis->formID, false, 0, cByPlayer));
}

// LockpickingMenu::Unlock (51968, `void (LockpickingMenu*)`): the player's lockpick opened it.
using TLockpickUnlock = void(void*);
static TLockpickUnlock* RealLockpickUnlock = nullptr;

void HookLockpickUnlock(void* apThis)
{
    ScopedPlayerLockOverride _;
    RealLockpickUnlock(apThis);
}

// TESObjectREFR::SetAngle (19786, `void (TESObjectREFR*, const NiPoint3&)`): its move reaches the funnel (RecordMove).
using TSetAngle = void(TESObjectREFR*, const NiPoint3&);
static TSetAngle* RealSetAngle = nullptr;

void HookSetAngle(TESObjectREFR* apThis, const NiPoint3& acAngle)
{
    const NiPoint3 cBefore = apThis->rotation;
    RealSetAngle(apThis, acAngle);
    apThis->RecordMove(apThis->rotation != cBefore);
}

// TESObjectREFR::PickLeveledBase (20232, our name): at its cell's load, a reference placed with ExtraLeveledItemBase rolls
// the list, takes the pick as its base (19785) and records it (0x80).
static std::atomic<bool> s_picksLeveledBases{true};
using TPickLeveledBase = void*(TESObjectREFR*);
static TPickLeveledBase* RealPickLeveledBase = nullptr;

void* HookPickLeveledBase(TESObjectREFR* apThis)
{
    if (!s_picksLeveledBases.load(std::memory_order_relaxed))
        return nullptr;
    return RealPickLeveledBase(apThis);
}

void TESObjectREFR::SetPicksLeveledBases(bool aPicks) noexcept
{
    s_picksLeveledBases.store(aPicks, std::memory_order_relaxed);
}

void TESObjectREFR::SetMovesContainerItems(bool aMoves) noexcept
{
    s_movesContainerItems.store(aMoves, std::memory_order_relaxed);
}

// Tempering (InventoryChanges 16145) and enchanting (EnchantObject 16146) change one of a carried stack in place, past
// our add/remove hooks: the owner's stacks of the item before and after, the difference raised as item changes
// (ItemChanges::Diff; party-items.md, re/systems/inventory-changes.md).
using TTemper = void*(ExtraContainerChanges::Data*, TESBoundObject*, ExtraDataList*, float);
using TEnchantObject = ExtraDataList*(ExtraContainerChanges::Data*, TESBoundObject*, ExtraDataList*, EnchantmentItem*, uint16_t);
static TTemper* RealTemper = nullptr;
static TEnchantObject* RealEnchantObject = nullptr;

template <class TCall> static auto ImprovesStack(ExtraContainerChanges::Data* apChanges, TESBoundObject* apObject, TCall&& aCall)
{
    TESObjectREFR* pOwner = apChanges && apObject ? apChanges->parent : nullptr;
    const auto cStacks = [&] { return pOwner->GetInventory([apObject](TESForm& aForm) { return &aForm == apObject; }).Entries; };
    const auto cBefore = pOwner ? cStacks() : std::vector<Inventory::Entry>{};
    auto result = aCall();
    if (pOwner && !ScopedInventoryOverride::IsOverriden())
        for (auto& change : ItemChanges::Diff(cBefore, cStacks()))
            World::Get().GetRunner().Trigger(InventoryChangeEvent(pOwner->formID, std::move(change)));
    return result;
}

static void* HookTemper(ExtraContainerChanges::Data* apChanges, TESBoundObject* apObject, ExtraDataList* apExtra, float aHealth)
{
    return ImprovesStack(apChanges, apObject, [&] { return RealTemper(apChanges, apObject, apExtra, aHealth); });
}

static ExtraDataList* HookEnchantObject(ExtraContainerChanges::Data* apChanges, TESBoundObject* apObject, ExtraDataList* apExtra,
                                        EnchantmentItem* apEnchantment, uint16_t aCharge)
{
    return ImprovesStack(apChanges, apObject, [&] { return RealEnchantObject(apChanges, apObject, apExtra, apEnchantment, aCharge); });
}

static Hooks::Initializer s_objectReferencesHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        HOOK_ID(19512, &RealLockChange, HookLockChange);
        HOOK_ID(19787, &RealRotateX, HookRotateX);
        HOOK_ID(19788, &RealRotateY, HookRotateY);
        HOOK_ID(19789, &RealRotateZ, HookRotateZ);
        HOOK_ID(19796, &RealActivate, HookActivate);
        HOOK_ID(19708, &RealAddInventoryItem, HookAddInventoryItem);
        HOOK_ID(16145, &RealTemper, HookTemper);
        HOOK_ID(16146, &RealEnchantObject, HookEnchantObject);
        HOOK_ID(19689, &RealRemoveInventoryItem, HookRemoveInventoryItem);
        HOOK_ID(16059, &RealRemoveFromInventory, HookRemoveFromInventory);
        HOOK_ID(56626, &RealMoveTo, HookMoveTo);
        HOOK_ID(22053, &RealSetPositionOnAxis, HookSetPositionOnAxis);
        HOOK_ID(22054, &RealSetAngleOnAxis, HookSetAngleOnAxis);
        HOOK_ID(56206, &RealPlayAnimationAndWait, HookPlayAnimationAndWait);
        HOOK_ID(56205, &RealPlayAnimation, HookPlayAnimation);
        HOOK_ID(51968, &RealLockpickUnlock, HookLockpickUnlock);
        HOOK_ID(19786, &RealSetAngle, HookSetAngle);
        HOOK_ID(20232, &RealPickLeveledBase, HookPickLeveledBase);
    });
