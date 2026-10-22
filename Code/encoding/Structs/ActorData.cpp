#include <Structs/ActorData.h>
#include <Wire/Serialization.h>

using Wire::Serialization;

void ActorData::Serialize(Wire::Buffer::Writer& aWriter) const noexcept
{
    InitialActorValues.Serialize(aWriter);
    InitialInventory.Serialize(aWriter);
    Serialization::WriteBool(aWriter, IsDead);
    Serialization::WriteBool(aWriter, IsWeaponDrawn);
}

void ActorData::Deserialize(Wire::Buffer::Reader& aReader) noexcept
{
    InitialActorValues.Deserialize(aReader);
    InitialInventory.Deserialize(aReader);
    IsDead = Serialization::ReadBool(aReader);
    IsWeaponDrawn = Serialization::ReadBool(aReader);
}
