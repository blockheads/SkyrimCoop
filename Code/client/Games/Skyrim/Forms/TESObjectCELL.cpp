#include <Forms/TESObjectCELL.h>
#include <TESObjectREFR.h>
#include <VersionDb.h>

std::vector<TESObjectREFR*> TESObjectCELL::GetRefsByFormTypes(const std::vector<FormType>& aFormTypes) const noexcept
{
    std::vector<TESObjectREFR*> references{};

    if (!refData.refArray)
        return references;

    for (uint32_t i = 0; i < refData.capacity; i++)
    {
        TESObjectREFR* pRef = refData.refArray[i].Get();
        if (!pRef)
            continue;

        for (FormType formType : aFormTypes)
        {
            if (pRef->baseForm->formType == formType)
                references.push_back(pRef);
        }
    }

    return references;
}

bool TESObjectCELL::GetGrid(int32_t& aX, int32_t& aY) const noexcept
{
    struct ExteriorData
    {
        int32_t cellX;
        int32_t cellY;
    };
    const auto* pExterior = reinterpret_cast<const ExteriorData*>(cellData);
    if (IsInterior() || !pExterior)
        return false;
    aX = pExterior->cellX;
    aY = pExterior->cellY;
    return true;
}

std::vector<TESObjectREFR*> TESObjectCELL::GetReferences() noexcept
{
    std::vector<TESObjectREFR*> references;
    lock.Lock();
    if (refData.refArray)
        for (uint32_t i = 0; i < refData.capacity; ++i)
            if (TESObjectREFR* pRef = refData.refArray[i].Get())
                references.push_back(pRef);
    lock.Unlock();
    return references;
}

void TESObjectCELL::GetCOCPlacementInfo(NiPoint3* aOutPos, NiPoint3* aOutRot, bool aAllowCellLoad) noexcept
{
    using TGetCOCPlacementInfo = void(TESObjectCELL*, NiPoint3*, NiPoint3*, bool);
    POINTER_SKYRIMSE(TGetCOCPlacementInfo, s_getCOCPlacementInfo, 19075);
    s_getCOCPlacementInfo(this, aOutPos, aOutRot, aAllowCellLoad);
}
