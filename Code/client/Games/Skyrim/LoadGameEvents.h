#pragma once

// Raises GameLoadedEvent when a save finishes loading (the engine's TESLoadGameEvent), for the plugin's lifetime.
namespace LoadGameEvents
{
void Start() noexcept;
void Stop() noexcept;
} // namespace LoadGameEvents
