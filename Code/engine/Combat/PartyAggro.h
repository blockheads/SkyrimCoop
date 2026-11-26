#pragma once
namespace PartyThreat
{
struct State;
}
struct Actor;
namespace PartyAggroNative
{
void Publish(const PartyThreat::State& acState) noexcept;
// Every player's body is the enemy of an NPC angry with one player or fighting one (37537, 37666, 37534, 37667 hooks).
bool IsPartyEnemy(Actor* apActor, Actor* apTarget) noexcept;
}
