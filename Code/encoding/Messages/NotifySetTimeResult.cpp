#include <Messages/NotifySetTimeResult.h>

void NotifySetTimeResult::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(static_cast<uint64_t>(Result), 8);
}

void NotifySetTimeResult::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    NotifyMessage::DeserializeRaw(aReader);

    uint64_t dest = 0;
    aReader.ReadBits(dest, 8);
    Result = static_cast<SetTimeResult>(dest & 0xFF);
}
