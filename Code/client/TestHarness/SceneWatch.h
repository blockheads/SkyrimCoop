#pragma once

#include "Commands.h"

#include <span>

// Who stops scenes (SceneWatch.cpp): each call of BGSScene::SetPlaying (24140) with its call stack.
namespace TestHarness::SceneWatch
{
std::span<const Command> Commands();
} // namespace TestHarness::SceneWatch
