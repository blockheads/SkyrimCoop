#pragma once

// A player going down and coming back (docs/design/death.md): a player's game and the host's HostDeath.
#include "Message.h"
#include <PartySize.h>
#include <Structs/GameId.h>
#include <Structs/Vector3_NetQuantize.h>
#include <Wire/Buffer.h>

#include <string>
#include <vector>

// A player's game: its character went down (a new fall, Generation one up) or stood up again after the host's respawn.
struct PlayerDeathRequest final : RequestMessage
{
    static constexpr RequestOpcode Opcode = kPlayerDeathRequest;

    PlayerDeathRequest()
        : RequestMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const PlayerDeathRequest& acRhs) const noexcept
    {
        return SessionId == acRhs.SessionId && Generation == acRhs.Generation && Down == acRhs.Down && KillerId == acRhs.KillerId;
    }

    uint32_t SessionId{};
    uint32_t Generation{};
    bool Down{};
    uint32_t KillerId{}; // the engine's killer of a fall, by session id (0: none, or not a character)
};

// A player's character in the party's death state: down or not, in which fall.
struct PartyDeathPlayer
{
    bool operator==(const PartyDeathPlayer&) const noexcept = default;

    uint32_t SessionId{};
    uint32_t Generation{};
    bool Down{};
};

// The host to every player on each change: who is down, whether a standing player fights, the recovery countdown,
// a wipe, and why its reload failed.
struct NotifyPartyDeath final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifyPartyDeath;
    // At most a party's players on the wire.
    static constexpr size_t kMaxPlayers = kMaxPartyPlayers;

    NotifyPartyDeath()
        : NotifyMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyPartyDeath& acRhs) const noexcept
    {
        return Players == acRhs.Players && Combat == acRhs.Combat && Wiped == acRhs.Wiped && RemainingMs == acRhs.RemainingMs &&
               Error == acRhs.Error;
    }

    std::vector<PartyDeathPlayer> Players;
    bool Combat{};
    bool Wiped{};
    uint32_t RemainingMs{};
    std::string Error;
};

// The host to a downed player's game: respawn for that fall beside the standing character AnchorId, there.
struct NotifyPlayerRespawn final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifyPlayerRespawn;

    NotifyPlayerRespawn()
        : NotifyMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyPlayerRespawn& acRhs) const noexcept
    {
        return SessionId == acRhs.SessionId && Generation == acRhs.Generation && AnchorId == acRhs.AnchorId &&
               CellId == acRhs.CellId && WorldSpaceId == acRhs.WorldSpaceId && Position == acRhs.Position &&
               Heading == acRhs.Heading;
    }

    uint32_t SessionId{};
    uint32_t Generation{};
    uint32_t AnchorId{};
    GameId CellId{};
    GameId WorldSpaceId{};
    Vector3_NetQuantize Position{};
    float Heading{};
};

// The host to every guest: the party wiped, the host's previous save comes next (as a join's does).
struct NotifyPartyReload final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifyPartyReload;

    NotifyPartyReload()
        : NotifyMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyPartyReload& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode(); }
};

// A player went down: to every game, the player's own too, for its chat.
struct NotifyPlayerPerished final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifyPlayerPerished;

    NotifyPlayerPerished()
        : NotifyMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyPlayerPerished& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && PlayerId == acRhs.PlayerId && Username == acRhs.Username &&
               KillerName == acRhs.KillerName;
    }

    uint32_t PlayerId{};
    std::string Username;
    std::string KillerName; // another player who killed it (friendly fire), else empty
};
