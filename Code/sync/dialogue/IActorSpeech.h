#pragma once

#include <string>
#include <cstdint>

// What an actor says. One part of IActorBridge (ports/IActorBridge.h).
struct IActorSpeech
{
    virtual ~IActorSpeech() = default;

    virtual void ActorSpeakSound(uint32_t aFormId, const std::string& acFilename) noexcept = 0;
    virtual void ActorStopCurrentDialogue(uint32_t aFormId) noexcept = 0;
    // The owner's subtitle over the copy (aTopicInfoFormId 0 if this game lacks it); aForce: at any distance.
    virtual void ActorShowSubtitle(uint32_t aFormId, const std::string& acText, uint32_t aTopicInfoFormId, bool aForce) noexcept = 0;
};
