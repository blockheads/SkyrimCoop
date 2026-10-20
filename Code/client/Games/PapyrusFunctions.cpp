#include "PapyrusFunctions.h"

#include <Actor.h>
#include <Games/ActorExtension.h>

namespace PapyrusFunctions
{

bool IsRemotePlayer(Actor* apActor)
{
    spdlog::info("Calling IsRemotePlayer");

    auto* pExtension = apActor->GetExtension();
    if (!pExtension)
        return false;

    return pExtension->IsOtherPlayer();
}

bool IsPlayer(Actor* apActor)
{
    spdlog::info("Calling IsPlayer");

    auto* pExtension = apActor->GetExtension();
    if (!pExtension)
        return false;

    return pExtension->IsPlayer();
}

} // namespace PapyrusFunctions
