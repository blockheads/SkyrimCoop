#include <Structs/DialogueOption.h>
#include <PartySize.h>
#include <Wire/Serialization.h>

using Wire::Serialization;

namespace
{
// A conversation never shows more; anything past it is a corrupt message.
constexpr uint64_t kMaxOptions = 64;
constexpr uint64_t kMaxPlayers = kMaxPartyPlayers;
} // namespace

void DialogueOption::Serialize(Wire::Buffer::Writer& aWriter) const noexcept
{
    Topic.Serialize(aWriter);
    Info.Serialize(aWriter);
    Quest.Serialize(aWriter);
    Serialization::WriteString(aWriter, Text);
    Serialization::WriteVarInt(aWriter, Players.size());
    for (const uint32_t cPlayer : Players)
        Serialization::WriteVarInt(aWriter, cPlayer);
}

void DialogueOption::Deserialize(Wire::Buffer::Reader& aReader) noexcept
{
    Topic.Deserialize(aReader);
    Info.Deserialize(aReader);
    Quest.Deserialize(aReader);
    Text = Serialization::ReadString(aReader);
    const uint64_t cCount = Serialization::ReadVarInt(aReader);
    Players.clear();
    for (uint64_t i = 0; i < cCount && i < kMaxPlayers; ++i)
        Players.push_back(static_cast<uint32_t>(Serialization::ReadVarInt(aReader)));
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

void SerializePlayerOptions(Wire::Buffer::Writer& aWriter, const std::vector<PlayerDialogueOptions>& acPlayers) noexcept
{
    Serialization::WriteVarInt(aWriter, acPlayers.size());
    for (const auto& cPlayer : acPlayers)
    {
        Serialization::WriteVarInt(aWriter, cPlayer.PlayerId);
        SerializeDialogueOptions(aWriter, cPlayer.Options);
    }
}

void DeserializePlayerOptions(Wire::Buffer::Reader& aReader, std::vector<PlayerDialogueOptions>& aPlayers) noexcept
{
    const uint64_t cCount = Serialization::ReadVarInt(aReader);
    aPlayers.clear();
    for (uint64_t i = 0; i < cCount && i < kMaxPlayers; ++i)
    {
        auto& player = aPlayers.emplace_back();
        player.PlayerId = static_cast<uint32_t>(Serialization::ReadVarInt(aReader));
        DeserializeDialogueOptions(aReader, player.Options);
    }
}
