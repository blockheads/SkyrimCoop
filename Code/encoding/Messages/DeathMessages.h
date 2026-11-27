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
struct PlayerDeathRequest final : MessageOf<PlayerDeathRequest, kPlayerDeathRequest>
{
    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    template <class TArchive> void Fields(TArchive& aArchive) noexcept
    {
        aArchive(SessionId, Generation, Down, KillerId);
    }
    bool operator==(const PlayerDeathRequest&) const noexcept = default;

    uint32_t SessionId{};
    uint32_t Generation{};
    bool Down{};
    uint32_t KillerId{}; // the engine's killer of a fall, by session id (0: none, or not a character)
};

// A player's character in the party's death state: down or not, in which fall.
struct PartyDeathPlayer
{
    bool operator==(const PartyDeathPlayer&) const noexcept = default;
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(SessionId, Generation, Down); }

    uint32_t SessionId{};
    uint32_t Generation{};
    bool Down{};
};

// The host to every player on each change: who is down, whether a standing player fights, the recovery countdown,
// a wipe, and why its reload failed.
struct NotifyPartyDeath final : MessageOf<NotifyPartyDeath, kNotifyPartyDeath>
{
    // At most a party's players on the wire.
    static constexpr size_t kMaxPlayers = kMaxPartyPlayers;

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    template <class TArchive> void Fields(TArchive& aArchive) noexcept
    {
        aArchive(Wire::List(Players, kMaxPlayers), Combat, Wiped, RemainingMs, Error);
    }
    bool operator==(const NotifyPartyDeath&) const noexcept = default;

    std::vector<PartyDeathPlayer> Players;
    bool Combat{};
    bool Wiped{};
    uint32_t RemainingMs{};
    std::string Error;
};

// The host to a downed player's game: respawn for that fall beside the standing character AnchorId, there.
struct NotifyPlayerRespawn final : MessageOf<NotifyPlayerRespawn, kNotifyPlayerRespawn>
{
    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    template <class TArchive> void Fields(TArchive& aArchive) noexcept
    {
        aArchive(SessionId, Generation, AnchorId, CellId, WorldSpaceId, Position, Heading);
    }
    bool operator==(const NotifyPlayerRespawn&) const noexcept = default;

    uint32_t SessionId{};
    uint32_t Generation{};
    uint32_t AnchorId{};
    GameId CellId{};
    GameId WorldSpaceId{};
    Vector3_NetQuantize Position{};
    float Heading{};
};

// The host to every guest: the party wiped or the host loads a save. The save comes next (as a join's does), unless
// the guest's store holds Save already.
struct NotifyPartyReload final : MessageOf<NotifyPartyReload, kNotifyPartyReload>
{
    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(Save); }
    bool operator==(const NotifyPartyReload&) const noexcept = default;

    std::string Save; // the image's SHA-256 id
};

// A player went down: to every game, the player's own too, for its chat.
struct NotifyPlayerPerished final : MessageOf<NotifyPlayerPerished, kNotifyPlayerPerished>
{
    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(PlayerId, Username, KillerName); }
    bool operator==(const NotifyPlayerPerished&) const noexcept = default;

    uint32_t PlayerId{};
    std::string Username;
    std::string KillerName; // another player who killed it (friendly fire), else empty
};
