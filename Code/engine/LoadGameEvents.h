#pragma once

// Raises GameLoadedEvent when a save finishes loading (the engine's TESLoadGameEvent), for the plugin's lifetime, and
// NewGameEvent when the main menu's New starts a game (a hook on NewGame_Enter).
namespace LoadGameEvents
{
void Start() noexcept;
void Stop() noexcept;
} // namespace LoadGameEvents
