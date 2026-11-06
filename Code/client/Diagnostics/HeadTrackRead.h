#pragma once

#include <array>
#include <cstdint>
#include <vector>

// What the engine reads of an actor's headtrack slots: docs/re/systems/actor-ai-and-copies.md, "When the slots are read".
namespace HeadTrackRead
{
// Actor::UpdateAnimation (37361) and Character::ProcessTrackingIfAllowed (37363) ask these before ProcessTracking
// (38009), which then asks TrackingOn (ActorState flags2 bit 3) and, on a target, the life state again.
struct Gate
{
    uint8_t Life = 0; // ActorState::LifeStateOf
    bool Dead = false;
    uint8_t Knock = 0;
    uint8_t SitSleep = 0;
    int8_t ProcessLevel = 0; // 0 high
    bool HasController = true;
    float LodDistance = 0.f;  // bhkCharacterController::lodDistance
    bool ExtraLarge = false;  // TESRace size 3
    bool TrackingOn = true;
    // This game's player has no range: its own update (40447) tracks only in the camera states from 9, third person.
    bool Player = false;
    bool ThirdPerson = false;
};

// Why the engine reads no slot now, nullptr if it does.
[[nodiscard]] constexpr const char* ShutBy(const Gate& acGate) noexcept
{
    if (acGate.Dead || !(acGate.Life == 0 || acGate.Life == 6 || acGate.Life == 7))
        return "life state";
    if (acGate.Knock)
        return "knocked";
    if (acGate.SitSleep == 7)
        return "asleep";
    if (acGate.ProcessLevel != 0)
        return "not high process";
    if (acGate.Player)
    {
        if (!acGate.ThirdPerson)
            return "first person";
    }
    else if (!acGate.HasController || acGate.LodDistance < 0.f || acGate.LodDistance > (acGate.ExtraLarge ? 12000.f : 2000.f))
        return "out of range";
    if (!acGate.TrackingOn)
        return "tracking off";
    return nullptr;
}

// The targets the engine reads, by form id: the highest set slot (AIProcess::GetHeadtrackTarget 39484), and slot 0 beside
// slot 2 in the conversation ProcessTracking alternates them in. A target that is the actor itself is no target.
[[nodiscard]] inline std::vector<uint32_t> Targets(const std::array<uint32_t, 6>& acSlots, uint32_t aSelf, bool aAlternates = false)
{
    std::vector<uint32_t> targets;
    for (size_t i = acSlots.size(); i-- > 0;)
    {
        if (!acSlots[i])
            continue;
        if (acSlots[i] != aSelf)
            targets.push_back(acSlots[i]);
        if (i == 2 && aAlternates && acSlots[0] && acSlots[0] != aSelf)
            targets.push_back(acSlots[0]);
        break;
    }
    return targets;
}
} // namespace HeadTrackRead
