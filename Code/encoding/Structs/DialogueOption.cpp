#include <Structs/DialogueOption.h>
#include <Wire/Serialization.h>

using Wire::Serialization;

namespace
{
// A conversation never shows more; anything past it is a corrupt message.
constexpr uint64_t kMaxOptions = 64;
} // namespace

void DialogueOption::Serialize(Wire::Buffer::Writer& aWriter) const noexcept
{
    Topic.Serialize(aWriter);
    Info.Serialize(aWriter);
    Quest.Serialize(aWriter);
    Serialization::WriteString(aWriter, Text);
}

void DialogueOption::Deserialize(Wire::Buffer::Reader& aReader) noexcept
{
    Topic.Deserialize(aReader);
    Info.Deserialize(aReader);
    Quest.Deserialize(aReader);
    Text = Serialization::ReadString(aReader);
}

void SerializeDialogueOptions(Wire::Buffer::Writer& aWriter, const DialogueOptions& acOptions) noexcept
{
    Serialization::WriteVarInt(aWriter, acOptions.size());
    for (const auto& cOption : acOptions)
        cOption.Serialize(aWriter);
}

void DeserializeDialogueOptions(Wire::Buffer::Reader& aReader, DialogueOptions& aOptions) noexcept
{
    const uint64_t cCount = Serialization::ReadVarInt(aReader);
    aOptions.clear();
    for (uint64_t i = 0; i < cCount && i < kMaxOptions; ++i)
        aOptions.emplace_back().Deserialize(aReader);
}
