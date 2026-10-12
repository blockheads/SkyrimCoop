#include <Messages/EnterCellRequest.h>

void EnterCellRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    CellId.Serialize(aWriter);
    WorldSpaceId.Serialize(aWriter);
    Position.Serialize(aWriter);
    LatestAction.GenerateDifferential(ActionEvent{}, aWriter);
    Serialization::WriteString(aWriter, GraphState);
}

void EnterCellRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    CellId.Deserialize(aReader);
    WorldSpaceId.Deserialize(aReader);
    Position.Deserialize(aReader);
    LatestAction = ActionEvent{};
    LatestAction.ApplyDifferential(aReader);
    GraphState = Serialization::ReadString(aReader);
}
