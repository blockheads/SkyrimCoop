#include <Messages/RequestRespawn.h>

void RequestRespawn::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, ActorId);
    Serialization::WriteString(aWriter, AppearanceBuffer);
    Serialization::WriteVarInt(aWriter, ChangeFlags);
    Serialization::WriteBool(aWriter, HasState);
    if (!HasState)
        return;
    LatestAction.GenerateDifferential(ActionEvent{}, aWriter);
    Serialization::WriteString(aWriter, GraphState);
    Data.Serialize(aWriter);
}

void RequestRespawn::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    ActorId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    AppearanceBuffer = Serialization::ReadString(aReader);
    ChangeFlags = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    HasState = Serialization::ReadBool(aReader);
    if (!HasState)
        return;
    LatestAction = ActionEvent{};
    LatestAction.ApplyDifferential(aReader);
    GraphState = Serialization::ReadString(aReader);
    Data.Deserialize(aReader);
}
