#pragma once

/**
 * @brief Dispatched when the client initially connects to the server.
 */
struct ConnectedEvent
{
    ConnectedEvent(uint32_t aPlayerId, uint32_t aCharacterId)
        : PlayerId(aPlayerId)
        , CharacterId(aCharacterId)
    {
    }

    uint32_t PlayerId{};
    // The session id this game's player is announced under (AuthenticationResponse::CharacterId).
    uint32_t CharacterId{};
};
