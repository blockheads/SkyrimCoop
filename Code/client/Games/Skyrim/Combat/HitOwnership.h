#pragma once

#include <Actor.h>
#include <Games/ActorExtension.h>
#include <HitRules.h>

namespace HitRules
{
inline Party PartyOf(Actor* apActor) noexcept
{
    if (!apActor)
        return Party::None;
    const auto* pExtension = apActor->GetExtension();
    if (pExtension->IsLocalPlayer())
        return Party::LocalPlayer;
    if (pExtension->IsOtherPlayer())
        return Party::OtherPlayer;
    return pExtension->IsLocal() ? Party::Local : Party::Copy;
}
} // namespace HitRules
