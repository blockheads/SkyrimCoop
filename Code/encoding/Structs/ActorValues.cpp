#include <Structs/ActorValues.h>
#include <Wire/Serialization.h>

using Wire::Serialization;

bool ActorValues::operator==(const ActorValues& acRhs) const noexcept
{
    return ActorValuesList == acRhs.ActorValuesList && ActorMaxValuesList == acRhs.ActorMaxValuesList &&
           ActorTemporaryValuesList == acRhs.ActorTemporaryValuesList;
}

bool ActorValues::operator!=(const ActorValues& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void ActorValues::Serialize(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, ActorValuesList.size());
    for (auto& value : ActorValuesList)
    {
        Serialization::WriteVarInt(aWriter, value.first);
        Serialization::WriteFloat(aWriter, value.second);
    }

    Serialization::WriteVarInt(aWriter, ActorMaxValuesList.size());
    for (auto& value : ActorMaxValuesList)
    {
        Serialization::WriteVarInt(aWriter, value.first);
        Serialization::WriteFloat(aWriter, value.second);
    }
    Serialization::WriteVarInt(aWriter, ActorTemporaryValuesList.size());
    for (const auto& [cIndex, cValue] : ActorTemporaryValuesList)
    {
        Serialization::WriteVarInt(aWriter, cIndex);
        Serialization::WriteFloat(aWriter, cValue);
    }
}

void ActorValues::Deserialize(Wire::Buffer::Reader& aReader) noexcept
{
    auto count = Serialization::ReadVarInt(aReader);
    for (int i = 0; i < count; i++)
    {
        auto key = Serialization::ReadVarInt(aReader);
        auto value = Serialization::ReadFloat(aReader);
        ActorValuesList.insert({key, value});
    }

    auto maxCount = Serialization::ReadVarInt(aReader);
    for (int i = 0; i < maxCount; i++)
    {
        auto key = Serialization::ReadVarInt(aReader);
        auto value = Serialization::ReadFloat(aReader);
        ActorMaxValuesList.insert({key, value});
    }
    const auto cTemporaryCount = Serialization::ReadVarInt(aReader);
    for (uint64_t i = 0; i < cTemporaryCount; ++i)
    {
        const auto cIndex = Serialization::ReadVarInt(aReader);
        ActorTemporaryValuesList[cIndex] = Serialization::ReadFloat(aReader);
    }
}
