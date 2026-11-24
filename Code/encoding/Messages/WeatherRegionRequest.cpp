#include <Messages/WeatherRegionRequest.h>

void WeatherRegionRequest::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Region.Serialize(aWriter);
}

void WeatherRegionRequest::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    RequestMessage::DeserializeRaw(aReader);

    Region.Deserialize(aReader);
}
