#include <Structs/ActorAiState.h>
#include <Wire/Serialization.h>

using Wire::Serialization;

void AiTarget::Serialize(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, SessionId);
    RefId.Serialize(aWriter);
}

void AiTarget::Deserialize(Wire::Buffer::Reader& aReader) noexcept
{
    SessionId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    RefId.Deserialize(aReader);
}

void ActorAiState::Serialize(Wire::Buffer::Writer& aWriter) const noexcept
{
    Package.Serialize(aWriter);
    Scene.Serialize(aWriter);
    aWriter.WriteBits(Combat, 8);
    CombatTarget.Serialize(aWriter);
    // The set slots after a mask: most actors look at nothing.
    uint64_t set = 0;
    for (size_t i = 0; i < HeadTrack.size(); ++i)
        set |= uint64_t(HeadTrack[i] != AiTarget{}) << i;
    aWriter.WriteBits(set, HeadTrack.size());
    for (size_t i = 0; i < HeadTrack.size(); ++i)
        if (set & (uint64_t{1} << i))
            HeadTrack[i].Serialize(aWriter);
}

void ActorAiState::Deserialize(Wire::Buffer::Reader& aReader) noexcept
{
    Package.Deserialize(aReader);
    Scene.Deserialize(aReader);
    uint64_t combat = 0;
    aReader.ReadBits(combat, 8);
    Combat = combat & 0xFF;
    CombatTarget.Deserialize(aReader);
    uint64_t set = 0;
    aReader.ReadBits(set, HeadTrack.size());
    for (size_t i = 0; i < HeadTrack.size(); ++i)
        HeadTrack[i] = AiTarget{};
    for (size_t i = 0; i < HeadTrack.size(); ++i)
        if (set & (uint64_t{1} << i))
            HeadTrack[i].Deserialize(aReader);
}
