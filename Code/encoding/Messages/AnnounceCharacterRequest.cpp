#include <Messages/AnnounceCharacterRequest.h>

void AnnounceCharacterRequest::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, SessionId);
    ReferenceId.Serialize(aWriter);
    FormId.Serialize(aWriter);
    CellId.Serialize(aWriter);
    WorldSpaceId.Serialize(aWriter);
    Position.Serialize(aWriter);
    Rotation.Serialize(aWriter);
    aWriter.WriteBits(ChangeFlags, 32);
    Serialization::WriteString(aWriter, AppearanceBuffer);
    LatestAction.GenerateDifferential(ActionEvent{}, aWriter);
    FaceTints.Serialize(aWriter);
    Serialization::WriteFloat(aWriter, Weight);
    Serialization::WriteBool(aWriter, IsDragon);
    Serialization::WriteBool(aWriter, IsMount);
    Serialization::WriteVarInt(aWriter, CommanderId);
    CurrentActorData.Serialize(aWriter);
    Serialization::WriteString(aWriter, GraphState);
    Record.Serialize(aWriter);
    PerksContent.Serialize(aWriter);
    Payloads::ActiveEffects::WriteIds(aWriter, EffectIds);
    Payloads::ActiveEffects::WriteHeld(aWriter, HeldEffects);
}

void AnnounceCharacterRequest::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    RequestMessage::DeserializeRaw(aReader);

    SessionId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    ReferenceId.Deserialize(aReader);
    FormId.Deserialize(aReader);
    CellId.Deserialize(aReader);
    WorldSpaceId.Deserialize(aReader);
    Position.Deserialize(aReader);
    Rotation.Deserialize(aReader);

    uint64_t dest = 0;
    aReader.ReadBits(dest, 32);
    ChangeFlags = dest & 0xFFFFFFFF;

    AppearanceBuffer = Serialization::ReadString(aReader);


    LatestAction = ActionEvent{};
    LatestAction.ApplyDifferential(aReader);

    FaceTints.Deserialize(aReader);
    Weight = Serialization::ReadFloat(aReader);

    IsDragon = Serialization::ReadBool(aReader);
    IsMount = Serialization::ReadBool(aReader);
    CommanderId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;

    CurrentActorData.Deserialize(aReader);
    GraphState = Serialization::ReadString(aReader);
    Record.Deserialize(aReader);
    PerksContent.Deserialize(aReader);
    Payloads::ActiveEffects::ReadIds(aReader, EffectIds);
    Payloads::ActiveEffects::ReadHeld(aReader, HeldEffects);
}
