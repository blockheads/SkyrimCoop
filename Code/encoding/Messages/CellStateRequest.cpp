#include <Messages/CellStateRequest.h>

#include <TiltedCore/Serialization.hpp>

namespace
{
// A cell holds a few thousand references at most; more is a corrupt count.
constexpr uint64_t kMaxChanged = 0x4000;
} // namespace

void CellStateRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Cell.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, Changed.size());
    for (const auto& cRef : Changed)
    {
        cRef.Id.Serialize(aWriter);
        Serialization::WriteVarInt(aWriter, cRef.Flags);
    }
}

void CellStateRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    RequestMessage::DeserializeRaw(aReader);

    Cell.Deserialize(aReader);
    const uint64_t cCount = Serialization::ReadVarInt(aReader);
    if (cCount > kMaxChanged)
        return;
    Changed.resize(cCount);
    for (auto& ref : Changed)
    {
        ref.Id.Deserialize(aReader);
        ref.Flags = static_cast<uint32_t>(Serialization::ReadVarInt(aReader));
    }
}
