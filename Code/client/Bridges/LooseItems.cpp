#include <Bridges/LooseItems.h>

#include <FormIds.h>

#include <ExtraData/ExtraCount.h>
#include <ExtraData/ExtraDataList.h>
#include <Forms/TESObjectCELL.h>
#include <Forms/TESWorldSpace.h>
#include <Games/TES.h>
#include <Actor.h>
#include <TESObjectREFR.h>

#include <cmath>
#include <RTTI.h>

std::optional<Inventory::Entry> ReadLooseItem(TESObjectREFR& aObject) noexcept
{
    if (aObject.formType != FormType::Reference || !aObject.baseForm || !Cast<TESBoundObject>(aObject.baseForm))
        return std::nullopt;
    const auto cBase = ToSession(aObject.baseForm->formID);
    if (!cBase)
        return std::nullopt;
    Inventory::Entry item;
    item.BaseId = *cBase;
    item.Count = 1;
    TESObjectREFR::GetItemFromExtraData(item, aObject.GetExtraDataList());
    item.ExtraWorn = item.ExtraWornLeft = item.IsQuestItem = false;
    return item;
}

std::optional<ObjectChange::Placement> ReadPlacement(const TESObjectREFR& acObject) noexcept
{
    const TESObjectCELL* pCell = acObject.GetParentCellEx();
    if (!pCell)
        return std::nullopt;
    ObjectChange::Placement placement;
    if (pCell->worldspace)
        placement.WorldSpace = ToSession(pCell->worldspace->formID).value_or(GameId{});
    else
        placement.Cell = ToSession(pCell->formID).value_or(GameId{});
    for (int axis = 0; axis < 3; ++axis)
    {
        placement.Position[axis] = acObject.position[axis];
        placement.Rotation[axis] = acObject.rotation[axis];
    }
    return placement;
}

TESObjectREFR* MakeLooseItem(const GameId& acBase, const Inventory::Entry* apItem, const ObjectChange::Placement& acPlacement) noexcept
{
    auto* pBase = Cast<TESBoundObject>(TESForm::GetById(ToGame(acBase)));
    auto* pCell = acPlacement.Cell ? Cast<TESObjectCELL>(TESForm::GetById(ToGame(acPlacement.Cell))) : nullptr;
    auto* pWorldSpace = acPlacement.WorldSpace ? Cast<TESWorldSpace>(TESForm::GetById(ToGame(acPlacement.WorldSpace))) : nullptr;
    ModManager* pManager = ModManager::Get();
    if (!pBase || (!pCell && !pWorldSpace) || !pManager)
        return nullptr;
    const NiPoint3 cPosition(glm::vec3(acPlacement.Position[0], acPlacement.Position[1], acPlacement.Position[2]));
    const NiPoint3 cRotation(glm::vec3(acPlacement.Rotation[0], acPlacement.Rotation[1], acPlacement.Rotation[2]));
    TESObjectREFR* pMade = TESObjectREFR::GetByHandle(pManager->CreateReference(pBase, cPosition, cRotation, pCell, pWorldSpace));
    if (!pMade || !apItem)
        return pMade;
    if (apItem->Count > 1)
        pMade->GetExtraDataList()->SetCount(static_cast<uint16_t>(std::min(apItem->Count, 0xFFFF)));
    TESObjectREFR::SetItemExtraData(*pMade->GetExtraDataList(), *apItem);
    return pMade;
}

bool IsPlacementLoaded(const ObjectChange::Placement& acPlacement) noexcept
{
    const TES* pTes = TES::Get();
    if (!pTes)
        return false;
    const uint32_t cCell = ToGame(acPlacement.Cell);
    const uint32_t cWorldSpace = ToGame(acPlacement.WorldSpace);
    // An exterior cell is 4096 units square.
    const int32_t cX = static_cast<int32_t>(std::floor(acPlacement.Position[0] / 4096.f));
    const int32_t cY = static_cast<int32_t>(std::floor(acPlacement.Position[1] / 4096.f));
    for (const TESObjectCELL* pCell : pTes->AttachedCells())
    {
        if (!pCell->IsFullyLoaded())
            continue;
        if (cCell && pCell->formID == cCell)
            return true;
        int32_t x = 0;
        int32_t y = 0;
        if (cWorldSpace && pCell->worldspace && pCell->worldspace->formID == cWorldSpace && pCell->GetGrid(x, y) && x == cX && y == cY)
            return true;
    }
    return false;
}
