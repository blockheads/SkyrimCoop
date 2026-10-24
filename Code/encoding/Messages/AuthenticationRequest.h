#pragma once

#include "Message.h"
#include <Structs/Mods.h>
#include <Wire/Buffer.h>
#include <Structs/GameId.h>
#include <Structs/GameVersions.h>
#include <Structs/TimeModel.h>

struct AuthenticationRequest final : RequestMessage
{
    static constexpr RequestOpcode Opcode = kAuthenticationRequest;

    AuthenticationRequest()
        : RequestMessage(Opcode)
    {
    }

    virtual ~AuthenticationRequest() = default;

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const AuthenticationRequest& achRhs) const noexcept
    {
        return GetOpcode() == achRhs.GetOpcode() && Game == achRhs.Game && Version == achRhs.Version && UserMods == achRhs.UserMods && Username == achRhs.Username &&
               WorldSpaceId == achRhs.WorldSpaceId && CellId == achRhs.CellId && Level == achRhs.Level
            && PlayerTime == achRhs.PlayerTime && PlayerKey == achRhs.PlayerKey && WantsWorld == achRhs.WantsWorld &&
               HasCharacter == achRhs.HasCharacter;
    }

    GameVersions Game{};
    std::string Version{};
    Mods UserMods{};
    std::string Username{};
    GameId WorldSpaceId{};
    GameId CellId{};
    uint16_t Level{};
    TimeModel PlayerTime{};
    // The player's stable id, its character's key in the host's save (PlayerRecord::Key).
    std::string PlayerKey{};
    // The host's gate, then its save (JoinService): no player yet.
    bool WantsWorld{};
    // Its game brings a character of its own (joined from a game it plays), for a host's save that has none.
    bool HasCharacter{};
};
