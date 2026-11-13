#pragma once

#include "Commands.h"

#include <span>

// Who wakes watched references' bodies (ActivateWatch.cpp). Game thread, except the hooks.
namespace TestHarness::ActivateWatch
{
bool Watching() noexcept;
// Once a frame while watching: the bodies' and islands' addresses, and awake changes.
void Poll();
std::span<const Command> Commands();
} // namespace TestHarness::ActivateWatch
