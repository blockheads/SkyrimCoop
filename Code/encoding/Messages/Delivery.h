#pragma once

#include <Messages/ReferencesMoveRequest.h>
#include <Messages/RelayedMessages.h>
#include <Opcodes.h>

#include <algorithm>

// Movement that must arrive: actions, or a teleport (said in one snapshot). The rest is followed by the next one.
template <class TUpdates> [[nodiscard]] bool MustArrive(const TUpdates& acUpdates) noexcept
{
    return std::ranges::any_of(acUpdates, [](const auto& acUpdate) {
        return !acUpdate.second.ActionEvents.empty() || acUpdate.second.UpdatedMovement.Teleported;
    });
}

// How a guest sends a request: movement unreliably, as the host relays it; the rest reliably, body frames too (the
// host follows the last one of a body this game moves: a lost one leaves it off, red-flags.md 5).
enum class Delivery
{
    kReliable,
    kReliableNow, // no Nagle delay
    kUnreliable,
};

[[nodiscard]] inline Delivery DeliveryOf(const RequestMessage& acMessage) noexcept
{
    switch (acMessage.GetOpcode())
    {
    case kPartyTravelRequest:
    case kPartySummonRequest: return Delivery::kReliableNow;
    case kReferencesMoveRequest:
        return MustArrive(static_cast<const ReferencesMoveRequest&>(acMessage).Updates) ? Delivery::kReliable : Delivery::kUnreliable;
    default: return Delivery::kReliable;
    }
}
