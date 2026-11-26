#pragma once

namespace TestHarness
{
// Each key or button press and release as the game dispatched it, one log line each (into the log ring, so a failed
// scenario's digest shows it): the user event it mapped to, what each input sink did with it (continue or stop),
// the menus open, and an item menu's list state (its lists drop input while they slide in).
struct InputTrace
{
    // Wraps the input sinks once the game has them (and new ones later); logs last frame's presses. Once a frame.
    void Poll() noexcept;
    // Puts the sinks' own functions back (before this DLL goes).
    void Stop() noexcept;
};
} // namespace TestHarness
