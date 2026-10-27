#include <Messages/DeathMessages.h>
#include <Wire/Serialization.h>

void PlayerDeathRequest::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, SessionId);
    Serialization::WriteVarInt(aWriter, Generation);
    Serialization::WriteBool(aWriter, Down);
    Serialization::WriteVarInt(aWriter, KillerId);
}

void PlayerDeathRequest::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    RequestMessage::DeserializeRaw(aReader);

    SessionId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Generation = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Down = Serialization::ReadBool(aReader);
    KillerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
}

void NotifyPartyDeath::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Players.size());
    for (const auto& cPlayer : Players)
    {
        Serialization::WriteVarInt(aWriter, cPlayer.SessionId);
        Serialization::WriteVarInt(aWriter, cPlayer.Generation);
        Serialization::WriteBool(aWriter, cPlayer.Down);
    }
    Serialization::WriteBool(aWriter, Combat);
    Serialization::WriteBool(aWriter, Wiped);
    Serialization::WriteVarInt(aWriter, RemainingMs);
    Serialization::WriteString(aWriter, Error);
}

void NotifyPartyDeath::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    NotifyMessage::DeserializeRaw(aReader);

    Players.clear();
    const auto cCount = Serialization::ReadVarInt(aReader);
    if (cCount > kMaxPlayers)
        return;
    for (size_t i = 0; i < cCount; ++i)
    {
        PartyDeathPlayer player;
        player.SessionId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
        player.Generation = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
        player.Down = Serialization::ReadBool(aReader);
        Players.push_back(player);
    }
    Combat = Serialization::ReadBool(aReader);
    Wiped = Serialization::ReadBool(aReader);
    RemainingMs = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Error = Serialization::ReadString(aReader);
}

void NotifyPlayerRespawn::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, SessionId);
    Serialization::WriteVarInt(aWriter, Generation);
    Serialization::WriteVarInt(aWriter, AnchorId);
    CellId.Serialize(aWriter);
    WorldSpaceId.Serialize(aWriter);
    Position.Serialize(aWriter);
    Serialization::WriteFloat(aWriter, Heading);
}

void NotifyPlayerRespawn::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    NotifyMessage::DeserializeRaw(aReader);

    SessionId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Generation = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    AnchorId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    CellId.Deserialize(aReader);
    WorldSpaceId.Deserialize(aReader);
    Position.Deserialize(aReader);
    Heading = Serialization::ReadFloat(aReader);
}

void NotifyPartyReload::SerializeRaw(Wire::Buffer::Writer&) const noexcept
{
}

void NotifyPartyReload::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    NotifyMessage::DeserializeRaw(aReader);
}

void NotifyPlayerPerished::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, PlayerId);
    Serialization::WriteString(aWriter, Username);
    Serialization::WriteString(aWriter, KillerName);
}

void NotifyPlayerPerished::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    NotifyMessage::DeserializeRaw(aReader);

    PlayerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Username = Serialization::ReadString(aReader);
    KillerName = Serialization::ReadString(aReader);
}
