// SkyrimBridge: voice lines and subtitles (IActorSpeech).
#include "SkyrimBridgeInternal.h"

void SkyrimBridge::ActorSpeakSound(uint32_t aFormId, const std::string& acFilename) noexcept
{
    Breadcrumbs::Record("ActorSpeakSound", aFormId);
    Actor* pActor = GetActor(aFormId);
    if (pActor)
        pActor->SpeakSound(acFilename.c_str());
}

void SkyrimBridge::ActorStopCurrentDialogue(uint32_t aFormId) noexcept
{
    Breadcrumbs::Record("ActorStopCurrentDialogue", aFormId);
    Actor* pActor = GetActor(aFormId);
    if (pActor)
        pActor->StopCurrentDialogue(true);
}

void SkyrimBridge::ActorShowSubtitle(uint32_t aFormId, const std::string& acText, uint32_t aTopicInfoFormId, bool aForce) noexcept
{
    Breadcrumbs::Record("ActorShowSubtitle", aFormId);
    Actor* pActor = GetActor(aFormId);
    if (pActor)
        SubtitleManager::Get()->ShowSubtitle(pActor, acText.c_str(), Cast<TESTopicInfo>(TESForm::GetById(aTopicInfoFormId)), aForce);
}
