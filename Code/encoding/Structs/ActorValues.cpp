#include <Structs/ActorValues.h>
#include <Wire/Serialization.h>

using Wire::Serialization;

namespace
{
// Keyed by actor value index: the engine has 164.
constexpr uint64_t kMaxActorValues = 256;
} // namespace

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
    const uint64_t cCount = Serialization::ReadCount(aReader, kMaxActorValues);
    for (uint64_t i = 0; i < cCount; ++i)
    {
        auto key = Serialization::ReadVarInt(aReader);
        auto value = Serialization::ReadFloat(aReader);
        ActorValuesList.insert({key, value});
    }

    const uint64_t cMaxCount = Serialization::ReadCount(aReader, kMaxActorValues);
    for (uint64_t i = 0; i < cMaxCount; ++i)
    {
        auto key = Serialization::ReadVarInt(aReader);
        auto value = Serialization::ReadFloat(aReader);
        ActorMaxValuesList.insert({key, value});
    }
    const uint64_t cTemporaryCount = Serialization::ReadCount(aReader, kMaxActorValues);
    for (uint64_t i = 0; i < cTemporaryCount; ++i)
    {
        const auto cIndex = Serialization::ReadVarInt(aReader);
        ActorTemporaryValuesList[cIndex] = Serialization::ReadFloat(aReader);
    }
}
