#include <FormState.h>

#include <Forms/TESForm.h>
#include <items/Overrides.h>
#include <SaveLoad.h>

namespace FormState
{
namespace
{
// Set on a form while its changes load (35569), cleared after FinishLoadGame (35567).
constexpr uint32_t cLoadingFlag = 0x200000;
constexpr uint32_t cDeletedFlag = 0x20;
} // namespace

bool Save(const TESForm& acForm, uint32_t aChangeFlags, std::string& aData) noexcept
{
    const BGSSaveLoadGame* pSaveLoad = BGSSaveLoadGame::Get();
    if (!pSaveLoad || pSaveLoad->IsSavingOrLoading())
        return false;

    ScopedSaveLoadOverride networkIds;
    BGSSaveFormBuffer buffer;
    buffer.SetHeader(acForm.formID, aChangeFlags, static_cast<uint8_t>(acForm.formType), BGSSaveLoadGame::CurrentVersion());
    buffer.form = const_cast<TESForm*>(&acForm);
    acForm.Save(&buffer);
    aData.assign(buffer.buffer ? buffer.buffer : "", buffer.position);
    return true;
}

bool Load(TESForm& aForm, uint32_t aChangeFlags, const std::string& acData) noexcept
{
    BGSSaveLoadGame* pSaveLoad = BGSSaveLoadGame::Get();
    if (!pSaveLoad || pSaveLoad->IsSavingOrLoading())
        return false;

    ScopedSaveLoadOverride networkIds;
    BGSLoadFormBuffer buffer;
    buffer.SetData(acData.data(), static_cast<uint32_t>(acData.size()));
    buffer.formId = aForm.formID;
    buffer.form = &aForm;
    buffer.changeFlags = aChangeFlags;
    buffer.version = BGSSaveLoadGame::CurrentVersion();

    const bool cWasClear = pSaveLoad->BeginLoading();
    aForm.flags |= cLoadingFlag;
    aForm.Load(&buffer);
    // The engine's InitLoadGame and FinishLoadGame pass reads only the header (35567).
    buffer.SetData(nullptr, 0);
    aForm.InitLoadGame(&buffer);
    if ((aForm.flags & cDeletedFlag) == 0)
        aForm.FinishLoadGame(&buffer);
    aForm.flags &= ~cLoadingFlag;
    pSaveLoad->EndLoading(cWasClear);
    return true;
}

bool SaveGlobal(uint32_t aType, std::string& aData) noexcept
{
    const BGSSaveLoadGame* pSaveLoad = BGSSaveLoadGame::Get();
    if (!pSaveLoad || pSaveLoad->IsSavingOrLoading())
        return false;

    using TSaveGlobal = bool(uint32_t, BGSSaveFormBuffer*);
    POINTER_SKYRIMSE(TSaveGlobal, s_saveGlobal, 35639);
    ScopedSaveLoadOverride networkIds;
    BGSSaveFormBuffer buffer;
    if (!s_saveGlobal(aType, &buffer))
        return false;
    aData.assign(buffer.buffer ? buffer.buffer : "", buffer.position);
    return true;
}

bool LoadGlobal(uint32_t aType, const std::string& acData) noexcept
{
    BGSSaveLoadGame* pSaveLoad = BGSSaveLoadGame::Get();
    if (!pSaveLoad || pSaveLoad->IsSavingOrLoading())
        return false;

    using TLoadGlobal = bool(uint32_t, BGSLoadFormBuffer*);
    POINTER_SKYRIMSE(TLoadGlobal, s_loadGlobal, 35642);
    ScopedSaveLoadOverride networkIds;
    BGSLoadFormBuffer buffer;
    buffer.SetData(acData.data(), static_cast<uint32_t>(acData.size()));
    buffer.version = BGSSaveLoadGame::CurrentVersion();
    const bool cWasClear = pSaveLoad->BeginLoading();
    const bool cLoaded = s_loadGlobal(aType, &buffer);
    buffer.SetData(nullptr, 0);
    pSaveLoad->EndLoading(cWasClear);
    return cLoaded;
}
} // namespace FormState
