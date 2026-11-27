#pragma once

#include "Message.h"
#include <Structs/GameVersions.h>
#include <Structs/Mods.h>

struct AuthenticationResponse final : MessageOf<AuthenticationResponse, kAuthenticationResponse>
{
    enum class ResponseType : uint8_t
    {
        kAccepted,
        kWrongVersion,
        kModsMismatch,
        // Keep active refusal codes stable after the unused upstream reasons.
        kSessionFull = 5,
        kWrongGame,
        // A main menu join passed the gate: the host's save follows (NotifySaveChunk).
        kWorldFollows,
        // The host's game could not save for it.
        kTransferFailed,
        // Neither the host's save nor the joining game has a character for it.
        kNoCharacter,
    };


    template <class TArchive> void Fields(TArchive& aArchive) noexcept
    {
        aArchive(Type, Version, Game, UserMods, Difficulty, PlayerId, CharacterId);
    }
    bool operator==(const AuthenticationResponse&) const noexcept = default;

    ResponseType Type;
    // The host's build and game: what a refused game differs from.
    std::string Version;
    GameVersions Game{};
    Mods UserMods{};
    uint32_t Difficulty{};
    uint32_t PlayerId{};
    // The session id of the player's character, which its game announces under it.
    uint32_t CharacterId{};
};
