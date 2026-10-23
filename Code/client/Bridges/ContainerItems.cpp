#include <Bridges/ContainerItems.h>

#include <FormIds.h>

#include <Actor.h>
#include <TESObjectREFR.h>

#include <algorithm>
#include <RTTI.h>

std::optional<ObjectChange> ReadContainerItems(uint32_t aObjectFormId, uint32_t aItemFormId) noexcept
{
    if ((aObjectFormId >> 24) == 0xFF || (aItemFormId >> 24) == 0xFF)
        return std::nullopt;
    auto* pObject = Cast<TESObjectREFR>(TESForm::GetById(aObjectFormId));
    if (!pObject || Cast<Actor>(pObject))
        return std::nullopt;
    const auto cCount = pObject->GetContainerCount(Cast<TESBoundObject>(TESForm::GetById(aItemFormId)));
    const auto cObject = ToSession(aObjectFormId);
    const auto cItem = ToSession(aItemFormId);
    if (!cCount || !cObject || !cItem)
        return std::nullopt;
    ObjectChange change{ObjectChange::Kind::kItems, *cObject};
    change.Item = *cItem;
    change.Value = static_cast<uint32_t>(*cCount);
    return change;
}

std::optional<ObjectChange> ReadContainerContents(const TESObjectREFR& acObject) noexcept
{
    const auto cObject = ToSessionOrCreated(acObject.formID);
    if (acObject.formType != FormType::Reference || !acObject.GetContainer() || !cObject)
        return std::nullopt;
    ObjectChange change{ObjectChange::Kind::kContents, *cObject};
    std::vector<ObjectChange::ItemCount> items;
    for (const auto& [pItem, cCount] : acObject.GetContainerItems())
    {
        if (const auto cItem = ToSession(pItem->formID); cCount && cItem)
            items.push_back({*cItem, cCount});
    }
    change.Data = ObjectChange::PackContents(items);
    return change;
}

bool ApplyContainerContents(TESObjectREFR& aObject, const ObjectChange& acChange) noexcept
{
    if (aObject.formType != FormType::Reference || !aObject.GetContainer())
        return false;
    std::vector<std::pair<TESBoundObject*, int32_t>> wanted;
    for (const auto& cItem : ObjectChange::UnpackContents(acChange.Data))
        if (auto* pItem = Cast<TESBoundObject>(TESForm::GetById(ToGame(cItem.Item))))
            wanted.emplace_back(pItem, cItem.Count);
    for (const auto& [pItem, cCount] : aObject.GetContainerItems())
        if (std::ranges::none_of(wanted, [pItem](const auto& acWanted) { return acWanted.first == pItem; }))
            aObject.SetContainerCount(pItem, 0);
    for (const auto& [pItem, cCount] : wanted)
        aObject.SetContainerCount(pItem, cCount);
    return true;
}
