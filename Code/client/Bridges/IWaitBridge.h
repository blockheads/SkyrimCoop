#pragma once

#include <cstdint>
#include <functional>

// Each game's native Sleep/Wait menu. Solo play uses the engine unchanged.
class IWaitBridge
{
  public:
    struct Callbacks
    {
        // True: the service handles the player's confirmation or cancellation.
        std::function<bool(uint32_t aHours, bool aSleeping)> Request;
        std::function<bool()> Cancel;
        std::function<void(uint32_t aRemainingSeconds)> Stepped;
    };

    virtual ~IWaitBridge() = default;
    virtual void SetCallbacks(Callbacks aCallbacks) noexcept = 0;
    // Opens the menu for a shared rest; the driver (the host) runs the countdown, the others follow it.
    virtual bool Prepare(uint32_t aHours, bool aSleeping, bool aDriver) noexcept = 0;
    // The menu is up and pausing the game.
    virtual bool IsReady() const noexcept = 0;
    // Starts the native wait or sleep (the menu's OK).
    virtual void Begin() noexcept = 0;
    // A follower's native step down to the host's remaining seconds.
    virtual void Step(uint32_t aRemainingSeconds) noexcept = 0;
    // Closes the menu if the shared rest opened or took it.
    virtual void End() noexcept = 0;
};
