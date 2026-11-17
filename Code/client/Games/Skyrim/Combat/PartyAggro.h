#pragma once
namespace PartyThreat
{
struct State;
}
struct Actor;
namespace PartyAggroNative
{
void Publish(const PartyThreat::State& acState) noexcept;
// The host's NPC fighting one player sees every player's body as an enemy (37537, 37666, 37534, 37667 hooks).
bool IsPartyFoe(Actor* apActor, Actor* apTarget) noexcept;
}
