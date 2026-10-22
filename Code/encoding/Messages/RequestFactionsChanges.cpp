#include <Messages/RequestFactionsChanges.h>
#include <Wire/Serialization.h>
#include <cassert>

void RequestFactionsChanges::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    assert(Changes.size() < 0x100);

    aWriter.WriteBits(Changes.size() & 0xFF, 8);

    for (auto& change : Changes)
    {
        Serialization::WriteVarInt(aWriter, change.first);
        change.second.Serialize(aWriter);
    }
}

void RequestFactionsChanges::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    RequestMessage::DeserializeRaw(aReader);

    uint64_t count = 0;
    aReader.ReadBits(count, 8);

    for (auto i = 0u; i < count; ++i)
    {
        auto& change = Changes[Serialization::ReadVarInt(aReader) & 0xFFFFFFFF];
        change.Deserialize(aReader);
    }
}
