#include <Messages/TimeScaleRequest.h>

#include <TiltedCore/Serialization.hpp>

void TimeScaleRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    TiltedPhoques::Serialization::WriteFloat(aWriter, Multiplier);
}

void TimeScaleRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    RequestMessage::DeserializeRaw(aReader);

    Multiplier = TiltedPhoques::Serialization::ReadFloat(aReader);
}
