#pragma once

#include "Message.h"

#include <Structs/DialogueOption.h>
#include <Structs/GameId.h>

// Shared dialogue: one conversation for the party, run by the host's game (docs/design/shared-dialogue.md).
// Conversation ids count from 1; steps count picks from 1.

// A game's player started talking to a copy: the host's game should start it with the real NPC.
struct DialogueStartRequest final : MessageOf<DialogueStartRequest, kDialogueStartRequest>
{
    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    uint32_t NpcId{}; // session id
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(NpcId); }
    bool operator==(const DialogueStartRequest&) const noexcept = default;
};

// The host's game: its player's dialogue menu opened with this NPC (Everyone: the NPC started it, a force greet).
struct DialogueOpenedRequest final : MessageOf<DialogueOpenedRequest, kDialogueOpenedRequest>
{
    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    uint32_t NpcId{};
    bool Everyone{};
    GameId Greeting{}; // host-selected INFO; copies have no arrest/alarm AI to select it
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(NpcId, Everyone, Greeting); }
    bool operator==(const DialogueOpenedRequest&) const noexcept = default;
};

// The host's game showed a step's list: its options in menu order (Choices: a line's choices), and each other
// player's own list for the step (Players), judged for its character.
struct DialogueOptionsRequest final : MessageOf<DialogueOptionsRequest, kDialogueOptionsRequest>
{
    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    uint32_t Conversation{};
    uint32_t Step{};
    bool Choices{};
    DialogueOptions Options{};
    std::vector<PlayerDialogueOptions> Players{};
    template <class TArchive> void Fields(TArchive& aArchive) noexcept
    {
        aArchive(Conversation, Step, Choices, Wire::List(Options, DialogueOption::kMaxOptions), Wire::List(Players, kMaxPartyPlayers));
    }
    bool operator==(const DialogueOptionsRequest&) const noexcept = default;
};

// A game's player picked an option of the step's list (its topic and line); Text, as its menu showed it.
struct DialoguePickRequest final : MessageOf<DialoguePickRequest, kDialoguePickRequest>
{
    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    uint32_t Conversation{};
    uint32_t Step{};
    GameId Topic{};
    GameId Info{};
    std::string Text{};
    template <class TArchive> void Fields(TArchive& aArchive) noexcept
    {
        aArchive(Conversation, Step, Topic, Info, Text);
    }
    bool operator==(const DialoguePickRequest&) const noexcept = default;
};

// A game's player left the conversation (its menu closed); it ends once nobody is in it. From the host's game
// with Ended: its conversation ended (a goodbye, the NPC walking off), so it ends for everyone.
struct DialogueCloseRequest final : MessageOf<DialogueCloseRequest, kDialogueCloseRequest>
{
    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    uint32_t Conversation{};
    bool Ended{};
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(Conversation, Ended); }
    bool operator==(const DialogueCloseRequest&) const noexcept = default;
};

// A game's player joined the conversation (its menu opened on the NPC, or its copy). The player who asked for it
// is in it from the start.
struct DialogueJoinRequest final : MessageOf<DialogueJoinRequest, kDialogueJoinRequest>
{
    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    uint32_t Conversation{};
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(Conversation); }
    bool operator==(const DialogueJoinRequest&) const noexcept = default;
};

// The host's game could not start the conversation a guest asked for (its player busy, the engine refused).
struct DialogueStartFailedRequest final : MessageOf<DialogueStartFailedRequest, kDialogueStartFailedRequest>
{
    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    uint32_t NpcId{};
    bool NothingToSay{}; // started, and the engine opened nothing: the NPC only says its hello
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(NpcId, NothingToSay); }
    bool operator==(const DialogueStartFailedRequest&) const noexcept = default;
};

// To the host's game: start its player's conversation with this NPC, for the player who asked.
struct NotifyDialogueStart final : MessageOf<NotifyDialogueStart, kNotifyDialogueStart>
{
    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    uint32_t NpcId{};
    uint32_t PlayerId{};
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(NpcId, PlayerId); }
    bool operator==(const NotifyDialogueStart&) const noexcept = default;
};

// A conversation opened: the player who asked opens its menu on its copy, every player when the NPC started it
// (Everyone); others join by talking to it. The host's game learns its id.
struct NotifyDialogueOpen final : MessageOf<NotifyDialogueOpen, kNotifyDialogueOpen>
{
    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    uint32_t Conversation{};
    uint32_t NpcId{};
    bool Everyone{};
    bool InIt{}; // this game is in it (it asked, or the NPC greeted everyone): its menu opens
    GameId Greeting{};
    uint32_t Step = 1; // a joiner's: the conversation's step now
    std::string Picked; // while the NPC answers a pick: its text, as the picker's menu showed it
    template <class TArchive> void Fields(TArchive& aArchive) noexcept
    {
        aArchive(Conversation, NpcId, Everyone, InIt, Greeting, Step, Picked);
    }
    bool operator==(const NotifyDialogueOpen&) const noexcept = default;
};

// A player's list for a step, judged for its character by the host's engine (Choices: a line's choices).
struct NotifyDialogueList final : MessageOf<NotifyDialogueList, kNotifyDialogueList>
{
    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    uint32_t Conversation{};
    uint32_t Step{};
    bool Choices{};
    DialogueOptions Options{};
    template <class TArchive> void Fields(TArchive& aArchive) noexcept
    {
        aArchive(Conversation, Step, Choices, Wire::List(Options, DialogueOption::kMaxOptions));
    }
    bool operator==(const NotifyDialogueList&) const noexcept = default;
};

// An option of the step was picked: the host's game says it as if its player had; every other menu waits for the
// next step's list.
struct NotifyDialoguePicked final : MessageOf<NotifyDialoguePicked, kNotifyDialoguePicked>
{
    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    uint32_t Conversation{};
    uint32_t Step{};
    GameId Topic{};
    GameId Info{};
    uint32_t PlayerId{}; // who pressed it and says it
    std::string Text{};  // as the picker's menu showed it: a menu joining mid-line shows it
    template <class TArchive> void Fields(TArchive& aArchive) noexcept
    {
        aArchive(Conversation, Step, Topic, Info, PlayerId, Text);
    }
    bool operator==(const NotifyDialoguePicked&) const noexcept = default;
};

// The conversation ended: every game closes its menu, the host's ends its conversation.
struct NotifyDialogueClosed final : MessageOf<NotifyDialogueClosed, kNotifyDialogueClosed>
{
    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    uint32_t Conversation{};
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(Conversation); }
    bool operator==(const NotifyDialogueClosed&) const noexcept = default;
};

// To the host's game: the player the NPC faced left the conversation; it turns to this one.
struct NotifyDialogueFace final : MessageOf<NotifyDialogueFace, kNotifyDialogueFace>
{
    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    uint32_t Conversation{};
    uint32_t PlayerId{};
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(Conversation, PlayerId); }
    bool operator==(const NotifyDialogueFace&) const noexcept = default;
};

// The conversation this game asked for will not start.
struct NotifyDialogueRefused final : MessageOf<NotifyDialogueRefused, kNotifyDialogueRefused>
{
    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    uint32_t NpcId{};
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(NpcId); }
    bool operator==(const NotifyDialogueRefused&) const noexcept = default;
};
