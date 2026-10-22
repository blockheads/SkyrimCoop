#include <Bridges/ContainerItems.h>

#include <Systems/ModSystem.h>

#include <Actor.h>
#include <TESObjectREFR.h>

#include <algorithm>
#include <RTTI.h>

std::optional<ObjectChange> ReadContainerItems(const ModSystem& acMods, uint32_t aObjectFormId, uint32_t aItemFormId) noexcept
{
    if ((aObjectFormId >> 24) == 0xFF || (aItemFormId >> 24) == 0xFF)
        return std::nullopt;
    auto* pObject = Cast<TESObjectREFR>(TESForm::GetById(aObjectFormId));
    if (!pObject || Cast<Actor>(pObject))
        return std::nullopt;
    const auto cCount = pObject->GetContainerCount(Cast<TESBoundObject>(TESForm::GetById(aItemFormId)));
    ObjectChange change{ObjectChange::Kind::kItems};
    if (!cCount || !acMods.GetServerModId(aObjectFormId, change.Object) || !acMods.GetServerModId(aItemFormId, change.Item))
        return std::nullopt;
    change.Value = static_cast<uint32_t>(*cCount);
    return change;
}

std::optional<ObjectChange> ReadContainerContents(const ModSystem& acMods, const TESObjectREFR& acObject) noexcept
{
    ObjectChange change{ObjectChange::Kind::kContents};
    if (acObject.formType != FormType::Reference || !acObject.GetContainer() || !acMods.GetServerModId(acObject.formID, change.Object))
        return std::nullopt;
    std::vector<ObjectChange::ItemCount> items;
    for (const auto& [pItem, cCount] : acObject.GetContainerItems())
    {
        ObjectChange::ItemCount item{{}, cCount};
        if (cCount && acMods.GetServerModId(pItem->formID, item.Item))
            items.push_back(item);
    }
    change.Data = ObjectChange::PackContents(items);
    return change;
}

bool ApplyContainerContents(const ModSystem& acMods, TESObjectREFR& aObject, const ObjectChange& acChange) noexcept
{
    if (aObject.formType != FormType::Reference || !aObject.GetContainer())
        return false;
    std::vector<std::pair<TESBoundObject*, int32_t>> wanted;
    for (const auto& cItem : ObjectChange::UnpackContents(acChange.Data))
        if (auto* pItem = Cast<TESBoundObject>(TESForm::GetById(acMods.GetGameId(cItem.Item))))
            wanted.emplace_back(pItem, cItem.Count);
    for (const auto& [pItem, cCount] : aObject.GetContainerItems())
        if (std::ranges::none_of(wanted, [pItem](const auto& acWanted) { return acWanted.first == pItem; }))
            aObject.SetContainerCount(pItem, 0);
    for (const auto& [pItem, cCount] : wanted)
        aObject.SetContainerCount(pItem, cCount);
    return true;
}
