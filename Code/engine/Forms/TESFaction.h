#pragma once

#include <Forms/TESForm.h>
#include <Components/TESFullName.h>

struct TESFaction : TESForm
{
    // TESFaction::SetPlayerEnemy (24505): kPlayerIsEnemy set or cleared, the player's knowledge reset.
    void SetPlayerEnemy(bool aEnemy) noexcept;

    TESFullName fullname;
};
