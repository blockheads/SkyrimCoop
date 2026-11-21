#include <Structs/CastPart.h>
#include <Wire/Serialization.h>

using Wire::Serialization;

void CastPart::Serialize(Wire::Buffer::Writer& aWriter) const noexcept
{
    for (const auto& cCast : Sources)
    {
        Serialization::WriteBool(aWriter, cCast.has_value());
        if (!cCast)
            continue;
        cCast->Spell.Serialize(aWriter);
        Serialization::WriteBool(aWriter, cCast->Dual);
        Serialization::WriteVarInt(aWriter, cCast->Target);
    }
}

void CastPart::Deserialize(Wire::Buffer::Reader& aReader) noexcept
{
    for (auto& cast : Sources)
    {
        cast.reset();
        if (!Serialization::ReadBool(aReader))
            continue;
        auto& held = cast.emplace();
        held.Spell.Deserialize(aReader);
        held.Dual = Serialization::ReadBool(aReader);
        held.Target = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    }
}
