#include <Structs/ActorData.h>
#include <Wire/Serialization.h>

using Wire::Serialization;

void ActorData::Serialize(Wire::Buffer::Writer& aWriter) const noexcept
{
    InitialActorValues.Serialize(aWriter);
    Serialization::WriteBool(aWriter, IsDead);
}

void ActorData::Deserialize(Wire::Buffer::Reader& aReader) noexcept
{
    InitialActorValues.Deserialize(aReader);
    IsDead = Serialization::ReadBool(aReader);
}
