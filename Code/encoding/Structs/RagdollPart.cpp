#include <Structs/RagdollPart.h>
#include <Wire/Serialization.h>

using Wire::Serialization;

void RagdollPart::Serialize(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Tick);
    Serialization::WriteVarInt(aWriter, Rests.size());
    for (const auto& cRest : Rests)
        cRest.Serialize(aWriter);
}

void RagdollPart::Deserialize(Wire::Buffer::Reader& aReader) noexcept
{
    Tick = Serialization::ReadVarInt(aReader);
    const uint64_t cCount = Serialization::ReadVarInt(aReader);
    Rests.clear();
    if (cCount > kMaxBodies)
        return;
    Rests.resize(cCount);
    for (auto& rest : Rests)
        rest.Deserialize(aReader);
}
