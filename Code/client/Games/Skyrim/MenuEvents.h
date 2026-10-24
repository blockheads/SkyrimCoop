#pragma once

// Raises MenuEvent when one of the engine's menus opens or closes (UI's MenuOpenCloseEvent), for the plugin's lifetime.
namespace MenuEvents
{
void Start() noexcept;
void Stop() noexcept;
} // namespace MenuEvents
