#include <Messages/TimeScaleRequest.h>

#include <Wire/Serialization.h>

void TimeScaleRequest::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Wire::Serialization::WriteFloat(aWriter, Multiplier);
}

void TimeScaleRequest::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    RequestMessage::DeserializeRaw(aReader);

    Multiplier = Wire::Serialization::ReadFloat(aReader);
}
