#include <Messages/AssignCharacterRequest.h>

void AssignCharacterRequest::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Cookie);
    ReferenceId.Serialize(aWriter);
    FormId.Serialize(aWriter);
    CellId.Serialize(aWriter);
    WorldSpaceId.Serialize(aWriter);
    Position.Serialize(aWriter);
    Rotation.Serialize(aWriter);
    aWriter.WriteBits(ChangeFlags, 32);
    Serialization::WriteString(aWriter, AppearanceBuffer);
    FactionsContent.Serialize(aWriter);
    LatestAction.GenerateDifferential(ActionEvent{}, aWriter);
    FaceTints.Serialize(aWriter);
    Serialization::WriteBool(aWriter, IsDragon);
    Serialization::WriteBool(aWriter, IsMount);
    Serialization::WriteBool(aWriter, IsPlayerSummon);
    CurrentActorData.Serialize(aWriter);
    Serialization::WriteString(aWriter, GraphState);
}

void AssignCharacterRequest::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    RequestMessage::DeserializeRaw(aReader);

    Cookie = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
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

    FactionsContent = {};
    FactionsContent.Deserialize(aReader);

    LatestAction = ActionEvent{};
    LatestAction.ApplyDifferential(aReader);

    FaceTints.Deserialize(aReader);

    IsDragon = Serialization::ReadBool(aReader);
    IsMount = Serialization::ReadBool(aReader);
    IsPlayerSummon = Serialization::ReadBool(aReader);

    CurrentActorData.Deserialize(aReader);
    GraphState = Serialization::ReadString(aReader);
}
