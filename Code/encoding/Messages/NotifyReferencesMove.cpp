#include <Messages/NotifyReferencesMove.h>
#include <Wire/Serialization.h>

void NotifyReferencesMove::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Tick);
    Serialization::WriteVarInt(aWriter, Updates.size());

    for (const auto& kvp : Updates)
    {
        Serialization::WriteVarInt(aWriter, kvp.first);
        kvp.second.Serialize(aWriter);
    }
}

void NotifyReferencesMove::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    NotifyMessage::DeserializeRaw(aReader);

    Tick = Serialization::ReadVarInt(aReader);
    const uint64_t cCount = Serialization::ReadCount(aReader, ReferenceUpdate::kMaxPerMessage);

    for (uint64_t i = 0; i < cCount; ++i)
    {
        const uint32_t cSessionId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
        Updates[cSessionId].Deserialize(aReader);
    }
}
