#pragma once

#include "Message.h"
#include <Structs/GameVersions.h>
#include <Structs/Mods.h>

struct AuthenticationResponse final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kAuthenticationResponse;

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

    AuthenticationResponse()
        : NotifyMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const AuthenticationResponse& achRhs) const noexcept { return GetOpcode() == achRhs.GetOpcode() && Type == achRhs.Type && Version == achRhs.Version && Game == achRhs.Game && UserMods == achRhs.UserMods && Difficulty == achRhs.Difficulty && PlayerId == achRhs.PlayerId && CharacterId == achRhs.CharacterId; }

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
