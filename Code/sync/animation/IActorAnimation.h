#pragma once

#include <Structs/ActionEvent.h>
#include <Structs/AnimationVariables.h>
#include <string>
#include <cstdint>

// An actor's graph: its variables, state and last action. One part of IActorBridge (ports/IActorBridge.h).
struct IActorAnimation
{
    virtual ~IActorAnimation() = default;

    virtual uint64_t GetAnimationGraphDescriptorHash(uint32_t aFormId) const noexcept = 0;
    virtual void SetAnimationGraphDescriptorHash(uint32_t aFormId, uint64_t aHash) noexcept = 0;
    // Whether the actor's animation graph can play actions (a copy's replay waits for this).
    virtual bool IsAnimationGraphReady(uint32_t aFormId) const noexcept = 0;
    virtual void ActorLoadAnimationVariables(uint32_t aFormId, const AnimationVariables& acVars) noexcept = 0;
    virtual void ActorSaveAnimationVariables(uint32_t aFormId, AnimationVariables& aVars) const noexcept = 0;
    // The actor's behaviour graph state as a savegame keeps it, and loading it into a copy.
    virtual std::string SaveActorGraphState(uint32_t aFormId) const noexcept = 0;
    virtual bool LoadActorGraphState(uint32_t aFormId, const std::string& acState) noexcept = 0;
    virtual ActionEvent GetActorLatestAnimation(uint32_t aFormId) const noexcept = 0;
    virtual void SetActorLatestAnimation(uint32_t aFormId, const ActionEvent& acEvent) noexcept = 0;
};
