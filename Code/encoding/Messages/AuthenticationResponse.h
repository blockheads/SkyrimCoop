#pragma once

#include "Message.h"
#include <Structs/GameVersions.h>
#include <Structs/Mods.h>
#include <Structs/ServerSettings.h>

struct AuthenticationResponse final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kAuthenticationResponse;

    enum class ResponseType : uint8_t
    {
        kAccepted,
        kWrongVersion,
        kModsMismatch,
        kClientModsDisallowed,
        kWrongPassword,
        kServerFull,
        kWrongGame,
    };

    AuthenticationResponse()
        : NotifyMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const AuthenticationResponse& achRhs) const noexcept { return GetOpcode() == achRhs.GetOpcode() && Type == achRhs.Type && Version == achRhs.Version && Game == achRhs.Game && UserMods == achRhs.UserMods && Settings == achRhs.Settings && PlayerId == achRhs.PlayerId; }

    ResponseType Type;
    // The host's build and game: what a refused game differs from.
    String Version;
    GameVersions Game{};
    Mods UserMods{};
    ServerSettings Settings{};
    uint32_t PlayerId{};
};
