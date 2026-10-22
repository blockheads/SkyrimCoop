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

void SerializeDialogueOptions(Wire::Buffer::Writer& aWriter, const DialogueOptions& acOptions) noexcept;
void DeserializeDialogueOptions(Wire::Buffer::Reader& aReader, DialogueOptions& aOptions) noexcept;
