#include <Messages/NotifyTimeSettings.h>

void NotifyTimeSettings::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    timeModel.Serialize(aWriter);
}

void NotifyTimeSettings::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    timeModel.Deserialize(aReader);
}
