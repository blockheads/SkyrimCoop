#include <Messages/SummonRequest.h>

void SummonRequest::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    SpellId.Serialize(aWriter);
    EffectId.Serialize(aWriter);
    Serialization::WriteFloat(aWriter, Magnitude);
    for (const float cValue : Location)
        Serialization::WriteFloat(aWriter, cValue);
    Serialization::WriteBool(aWriter, DualCast);
}

void SummonRequest::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    RequestMessage::DeserializeRaw(aReader);
    SpellId.Deserialize(aReader);
    EffectId.Deserialize(aReader);
    Magnitude = Serialization::ReadFloat(aReader);
    for (float& value : Location)
        value = Serialization::ReadFloat(aReader);
    DualCast = Serialization::ReadBool(aReader);
}
