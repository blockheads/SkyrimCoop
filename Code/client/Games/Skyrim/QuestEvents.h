#pragma once

#include <Games/QuestChanges.h>

namespace QuestEvents
{
void Start() noexcept;
void Stop() noexcept;
QuestChanges& Changes() noexcept;
} // namespace QuestEvents
