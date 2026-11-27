#pragma once

#include <PartySize.h>
#include <Wire/Archive.h>

#include <Structs/GameId.h>
#include <Wire/Buffer.h>

// One option of a step's list (docs/design/shared-dialogue.md); in the party's merged list, who can say it.
struct DialogueOption
{
    // A conversation never shows more; anything past it is a corrupt message.
    static constexpr uint64_t kMaxOptions = 64;
    bool operator==(const DialogueOption&) const noexcept = default;
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(Topic, Info, Quest, Text, Wire::List(Players, kMaxPartyPlayers)); }

    COOP_SERIALIZE_FIELDS

    GameId Topic{};
    GameId Info{}; // the info said when picked
    GameId Quest{};
    std::string Text{};
    // The merged list's: the players whose own list has it (none: everyone in the conversation).
    std::vector<uint32_t> Players{};
};

using DialogueOptions = std::vector<DialogueOption>;

// A player's own list for a step: the host's engine judged its lines for that player's character.
struct PlayerDialogueOptions
{
    bool operator==(const PlayerDialogueOptions&) const noexcept = default;
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(PlayerId, Wire::List(Options, DialogueOption::kMaxOptions)); }

    uint32_t PlayerId{};
    DialogueOptions Options{};
};

