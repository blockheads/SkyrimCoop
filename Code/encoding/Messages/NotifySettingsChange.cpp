#include <Messages/NotifySettingsChange.h>
#include <TiltedCore/Serialization.hpp>

void NotifySettingsChange::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Settings.Serialize(aWriter);
}

void NotifySettingsChange::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    NotifyMessage::DeserializeRaw(aReader);

    Settings.Deserialize(aReader);
}
