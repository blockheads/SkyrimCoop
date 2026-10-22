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
}

void ActorAiState::Deserialize(Wire::Buffer::Reader& aReader) noexcept
{
    Package.Deserialize(aReader);
    Scene.Deserialize(aReader);
    uint64_t combat = 0;
    aReader.ReadBits(combat, 8);
    Combat = combat & 0xFF;
    CombatTarget.Deserialize(aReader);
}
