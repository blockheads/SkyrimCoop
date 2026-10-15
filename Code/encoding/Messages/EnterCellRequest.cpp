#include <Messages/EnterCellRequest.h>

void EnterCellRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    CellId.Serialize(aWriter);
    WorldSpaceId.Serialize(aWriter);
    Position.Serialize(aWriter);
    Serialization::WriteFloat(aWriter, Heading);
    aWriter.WriteBits(static_cast<uint8_t>(How), 8);
    LatestAction.GenerateDifferential(ActionEvent{}, aWriter);
    Serialization::WriteString(aWriter, GraphState);
    Data.Serialize(aWriter);
}

void EnterCellRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    CellId.Deserialize(aReader);
    WorldSpaceId.Deserialize(aReader);
    Position.Deserialize(aReader);
    Heading = Serialization::ReadFloat(aReader);
    uint64_t how = 0;
    aReader.ReadBits(how, 8);
    How = static_cast<Travel>(how);
    LatestAction = ActionEvent{};
    LatestAction.ApplyDifferential(aReader);
    GraphState = Serialization::ReadString(aReader);
    Data.Deserialize(aReader);
}
