#pragma once

#include <Games/ActorExtension.h>
#include <PlayerCharacter.h>

namespace GameplayFormulas
{
float CalculateRealDamage(Actor* apHittee, float aDamage, bool aKillMove) noexcept;
}

namespace Settings
{
int32_t* GetDifficulty() noexcept;
float* GetGreetDistance() noexcept;
float* GetVATSSelectTargetTimeMultiplier() noexcept;
} // namespace Settings

struct ExActor : Actor, ActorExtension
{
};

struct ExPlayerCharacter : PlayerCharacter, ActorExtension
{
};
