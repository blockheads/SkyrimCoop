#pragma once

#include <cstdint>

// This game's held copies hidden until their owner spawns them (docs/re/systems/copy-lifecycle.md): HookEnable and
// HookSetDisabled refuse to enable them. By form id from any thread: the engine sets the flag while loading too.
namespace HiddenCopies
{
void Set(uint32_t aFormId, bool aHidden) noexcept;
[[nodiscard]] bool Contains(uint32_t aFormId) noexcept;
} // namespace HiddenCopies
