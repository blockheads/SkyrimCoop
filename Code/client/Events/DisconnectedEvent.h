#pragma once

/**
 * @brief Dispatched when this game's connection to the session ends.
 */
struct DisconnectedEvent
{
    enum class Cause
    {
        kLeft,   // this game closed it (leaving, a join's load, the host stopping its own route)
        kHostLeft, // the host intentionally ended the session
        kKicked, // the host removed this game
        kLost,   // it dropped: a timeout, the host's game going away
    };
    Cause Why{Cause::kLeft};
    // This game was in the session as a guest (on the network, not the host's own route).
    bool WasGuest{};
};
