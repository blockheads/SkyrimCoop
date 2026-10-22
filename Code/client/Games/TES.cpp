#include <Games/TES.h>
#include <Forms/TESForm.h>
#include <Forms/TESObjectCELL.h>
#include <Effects/ReferenceEffect.h>

#include <algorithm>
#include <RTTI.h>
#include <VersionDb.h>

TES* TES::Get() noexcept
{
    POINTER_SKYRIMSE(TES*, tes, 400441);

    return *tes.Get();
}

std::vector<TESObjectCELL*> TES::AttachedCells() const noexcept
{
    std::vector<TESObjectCELL*> attached;
    if (interiorCell)
        attached.push_back(interiorCell);
    else if (cells && cells->arr)
        for (uint32_t i = 0; i < cells->dimension * cells->dimension; ++i)
            if (TESObjectCELL* pCell = cells->arr[i]; pCell && pCell->IsValid())
                attached.push_back(pCell);
    return attached;
}

ProcessLists* ProcessLists::Get() noexcept
{
    POINTER_SKYRIMSE(ProcessLists*, processLists, 400315);

    return *processLists.Get();
}

std::vector<uint32_t> ProcessLists::GetPlayingVisualFormIds(uint32_t aRefHandle) noexcept
{
    std::vector<uint32_t> result;
    {
        BSScopedLock<BSRecursiveLock> _(magicEffectsLock);
        for (uint32_t i = 0; magicEffects.data && i < magicEffects.length; ++i)
        {
            auto* pEffect = Cast<ReferenceEffect>(magicEffects[i]);
            if (!pEffect || pEffect->hTarget != aRefHandle || pEffect->bFinished)
                continue;
            const TESForm* pForm = nullptr;
            if (const auto* pShader = Cast<ShaderReferenceEffect>(magicEffects[i]))
                pForm = pShader->pEffectShader;
            else if (const auto* pModel = Cast<ModelReferenceEffect>(magicEffects[i]))
                pForm = pModel->pArtObject;
            if (pForm)
                result.push_back(pForm->formID);
        }
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

void ProcessLists::StopEffectShader(TESObjectREFR* apRef, TESForm* apEffectShader) noexcept
{
    using TStop = void(ProcessLists*, TESObjectREFR*, TESForm*);
    POINTER_SKYRIMSE(TStop, s_stopEffectShader, 41395);
    s_stopEffectShader(this, apRef, apEffectShader);
}

void ProcessLists::StopArtObject(TESObjectREFR* apRef, TESForm* apArtObject) noexcept
{
    using TStop = void(ProcessLists*, TESObjectREFR*, TESForm*);
    POINTER_SKYRIMSE(TStop, s_stopArtObject, 41396);
    s_stopArtObject(this, apRef, apArtObject);
}
