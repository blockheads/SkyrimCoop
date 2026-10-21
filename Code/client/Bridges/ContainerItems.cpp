#include <Bridges/ContainerItems.h>

#include <Systems/ModSystem.h>

#include <Actor.h>
#include <TESObjectREFR.h>

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
