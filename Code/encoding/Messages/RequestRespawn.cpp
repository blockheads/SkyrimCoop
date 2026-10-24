#include <Messages/RequestRespawn.h>

void RequestRespawn::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, ActorId);
    Serialization::WriteString(aWriter, AppearanceBuffer);
    Serialization::WriteVarInt(aWriter, ChangeFlags);
    FaceTints.Serialize(aWriter);
    Serialization::WriteFloat(aWriter, Weight);
    Serialization::WriteBool(aWriter, HasState);
    if (!HasState)
        return;
    LatestAction.GenerateDifferential(ActionEvent{}, aWriter);
    Serialization::WriteString(aWriter, GraphState);
    Data.Serialize(aWriter);
}

void RequestRespawn::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    RequestMessage::DeserializeRaw(aReader);

    ActorId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    AppearanceBuffer = Serialization::ReadString(aReader);
    ChangeFlags = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    FaceTints = Tints{};
    FaceTints.Deserialize(aReader);
    Weight = Serialization::ReadFloat(aReader);
    HasState = Serialization::ReadBool(aReader);
    if (!HasState)
        return;
    LatestAction = ActionEvent{};
    LatestAction.ApplyDifferential(aReader);
    GraphState = Serialization::ReadString(aReader);
    Data.Deserialize(aReader);
}
