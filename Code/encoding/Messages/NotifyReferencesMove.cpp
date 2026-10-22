#include <Messages/NotifyReferencesMove.h>
#include <TiltedCore/Serialization.hpp>

void NotifyReferencesMove::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Tick);
    Serialization::WriteVarInt(aWriter, Updates.size());

    for (const auto& kvp : Updates)
    {
        Serialization::WriteVarInt(aWriter, kvp.first);
        kvp.second.Serialize(aWriter);
    }
}

void NotifyReferencesMove::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    NotifyMessage::DeserializeRaw(aReader);

    Tick = Serialization::ReadVarInt(aReader);
    const auto count = Serialization::ReadVarInt(aReader);

    for (auto i = 0u; i < count; ++i)
    {
        const uint32_t cSessionId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
        Updates[cSessionId].Deserialize(aReader);
    }
}
