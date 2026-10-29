#pragma once

#include <Structs/GameId.h>
#include <Wire/Buffer.h>

// One option of the host's list for a step (docs/design/shared-dialogue.md).
struct DialogueOption
{
    bool operator==(const DialogueOption&) const noexcept = default;

    void Serialize(Wire::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(Wire::Buffer::Reader& aReader) noexcept;

    GameId Topic{};
    GameId Info{}; // the info said when picked
    GameId Quest{};
    std::string Text{};
};

using DialogueOptions = std::vector<DialogueOption>;

// A player's own list for a step: the host's engine judged its lines for that player's character.
struct PlayerDialogueOptions
{
    bool operator==(const PlayerDialogueOptions&) const noexcept = default;

    uint32_t PlayerId{};
    DialogueOptions Options{};
};

void SerializeDialogueOptions(Wire::Buffer::Writer& aWriter, const DialogueOptions& acOptions) noexcept;
void DeserializeDialogueOptions(Wire::Buffer::Reader& aReader, DialogueOptions& aOptions) noexcept;
void SerializePlayerOptions(Wire::Buffer::Writer& aWriter, const std::vector<PlayerDialogueOptions>& acPlayers) noexcept;
void DeserializePlayerOptions(Wire::Buffer::Reader& aReader, std::vector<PlayerDialogueOptions>& aPlayers) noexcept;
