#include <Messages/ReferencesMoveRequest.h>
#include <Wire/Serialization.h>

void ReferencesMoveRequest::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Tick);
    Serialization::WriteVarInt(aWriter, Updates.size());

    for (const auto& kvp : Updates)
    {
        Serialization::WriteVarInt(aWriter, kvp.first);
        kvp.second.Serialize(aWriter);
    }
}

void ReferencesMoveRequest::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    RequestMessage::DeserializeRaw(aReader);

    Tick = Serialization::ReadVarInt(aReader);
    const auto count = Serialization::ReadVarInt(aReader);

    for (auto i = 0u; i < count; ++i)
    {
        uint32_t sessionId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
        Updates[sessionId].Deserialize(aReader);
    }
}
