#pragma once

#include <Structs/GameId.h>

// One option of the host's list for a step (docs/design/shared-dialogue.md).
struct DialogueOption
{
    bool operator==(const DialogueOption&) const noexcept = default;

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    GameId Topic{};
    GameId Info{}; // the info said when picked
    GameId Quest{};
    TiltedPhoques::String Text{};
};

using DialogueOptions = TiltedPhoques::Vector<DialogueOption>;

void SerializeDialogueOptions(TiltedPhoques::Buffer::Writer& aWriter, const DialogueOptions& acOptions) noexcept;
void DeserializeDialogueOptions(TiltedPhoques::Buffer::Reader& aReader, DialogueOptions& aOptions) noexcept;
