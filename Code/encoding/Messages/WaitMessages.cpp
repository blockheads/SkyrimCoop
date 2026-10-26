#include <Messages/WaitMessages.h>
#include <Wire/Serialization.h>

using Wire::Serialization;

void WaitRequest::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(static_cast<uint8_t>(Action), 8);
    Serialization::WriteVarInt(aWriter, Id);
    Serialization::WriteVarInt(aWriter, Hours);
    Serialization::WriteVarInt(aWriter, RemainingSeconds);
    Serialization::WriteBool(aWriter, Sleeping);
}

void WaitRequest::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    RequestMessage::DeserializeRaw(aReader);
    uint64_t action{};
    aReader.ReadBits(action, 8);
    Action = static_cast<WaitAction>(action);
    Id = static_cast<uint32_t>(Serialization::ReadVarInt(aReader));
    Hours = static_cast<uint32_t>(Serialization::ReadVarInt(aReader));
    RemainingSeconds = static_cast<uint32_t>(Serialization::ReadVarInt(aReader));
    Sleeping = Serialization::ReadBool(aReader);
}

void NotifyWaitState::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(static_cast<uint8_t>(Phase), 8);
    Serialization::WriteVarInt(aWriter, Id);
    Serialization::WriteVarInt(aWriter, Hours);
    Serialization::WriteVarInt(aWriter, RemainingSeconds);
    Serialization::WriteBool(aWriter, Sleeping);
    Serialization::WriteBool(aWriter, Driver);
    Calendar.Serialize(aWriter);
}

void NotifyWaitState::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    NotifyMessage::DeserializeRaw(aReader);
    uint64_t phase{};
    aReader.ReadBits(phase, 8);
    Phase = static_cast<WaitPhase>(phase);
    Id = static_cast<uint32_t>(Serialization::ReadVarInt(aReader));
    Hours = static_cast<uint32_t>(Serialization::ReadVarInt(aReader));
    RemainingSeconds = static_cast<uint32_t>(Serialization::ReadVarInt(aReader));
    Sleeping = Serialization::ReadBool(aReader);
    Driver = Serialization::ReadBool(aReader);
    Calendar.Deserialize(aReader);
}

void WaitWorldRequest::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Id);
    State.SerializeRaw(aWriter);
}

void WaitWorldRequest::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    Id = static_cast<uint32_t>(Serialization::ReadVarInt(aReader));
    State.DeserializeRaw(aReader);
}

void NotifyWaitWorld::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Id);
    Serialization::WriteBool(aWriter, Last);
    State.SerializeRaw(aWriter);
}

void NotifyWaitWorld::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    Id = static_cast<uint32_t>(Serialization::ReadVarInt(aReader));
    Last = Serialization::ReadBool(aReader);
    State.DeserializeRaw(aReader);
}
