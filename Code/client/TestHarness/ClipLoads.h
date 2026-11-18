#pragma once

#include "Commands.h"

#include <span>

// The animation file manager's queued clip loads and a hold on its job (ClipLoads.cpp). Game thread, except the hook.
namespace TestHarness::ClipLoads
{
bool Watching() noexcept;
// Once a frame once used: which actor's process each animation lock belongs to.
void Poll();
std::span<const Command> Commands();
} // namespace TestHarness::ClipLoads
