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

void NotifySaveChunk::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Offset);
    Serialization::WriteVarInt(aWriter, Total);
    Serialization::WriteString(aWriter, Data);
}

void NotifySaveChunk::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    Offset = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Total = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Data = Serialization::ReadString(aReader);
}
