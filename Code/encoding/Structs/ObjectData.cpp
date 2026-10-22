#include <Structs/ObjectData.h>
#include <TiltedCore/Serialization.hpp>

using TiltedPhoques::Serialization;

bool ObjectData::operator==(const ObjectData& acRhs) const noexcept
{
    return SessionId == acRhs.SessionId && Id == acRhs.Id && CellId == acRhs.CellId && WorldSpaceId == acRhs.WorldSpaceId && CurrentCoords == acRhs.CurrentCoords && CurrentLockData == acRhs.CurrentLockData && CurrentInventory == acRhs.CurrentInventory && IsSenderFirst == acRhs.IsSenderFirst;
}

bool ObjectData::operator!=(const ObjectData& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void ObjectData::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, SessionId);
    Id.Serialize(aWriter);
    CellId.Serialize(aWriter);
    WorldSpaceId.Serialize(aWriter);
    CurrentCoords.Serialize(aWriter);
    CurrentLockData.Serialize(aWriter);
    CurrentInventory.Serialize(aWriter);
    Serialization::WriteBool(aWriter, IsSenderFirst);
}

void ObjectData::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    SessionId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Id.Deserialize(aReader);
    CellId.Deserialize(aReader);
    WorldSpaceId.Deserialize(aReader);
    CurrentCoords.Deserialize(aReader);
    CurrentLockData.Deserialize(aReader);
    CurrentInventory.Deserialize(aReader);
    IsSenderFirst = Serialization::ReadBool(aReader);
}
