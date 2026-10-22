#include <Messages/CancelAssignmentRequest.h>

void CancelAssignmentRequest::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Cookie);
}

void CancelAssignmentRequest::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    RequestMessage::DeserializeRaw(aReader);

    Cookie = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
}
