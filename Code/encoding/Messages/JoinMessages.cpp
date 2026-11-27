#include <Messages/JoinMessages.h>

void PlayerRecordRequest::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Record.Serialize(aWriter);
}

void PlayerRecordRequest::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    RequestMessage::DeserializeRaw(aReader);
    Record = {};
    Record.Deserialize(aReader);
}

void SaveHeldRequest::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Saves.size());
    for (const auto& save : Saves)
        Serialization::WriteString(aWriter, save);
}

void SaveHeldRequest::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    RequestMessage::DeserializeRaw(aReader);
    Saves.clear();
    const uint64_t cCount = Serialization::ReadCount(aReader, kMaxSaves);
    for (uint64_t i = 0; i < cCount; ++i)
        Saves.push_back(Serialization::ReadString(aReader));
}

void NotifySaveChunk::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteString(aWriter, Save);
    Serialization::WriteString(aWriter, Base);
    Serialization::WriteVarInt(aWriter, Size);
    Serialization::WriteVarInt(aWriter, Total);
    Serialization::WriteVarInt(aWriter, Offset);
    Serialization::WriteBool(aWriter, Ahead);
    Serialization::WriteString(aWriter, Data);
}

void NotifySaveChunk::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    Save = Serialization::ReadString(aReader);
    Base = Serialization::ReadString(aReader);
    Size = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Total = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Offset = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Ahead = Serialization::ReadBool(aReader);
    Data = Serialization::ReadString(aReader);
}
