#pragma once

#include "Message.h"

#include <Structs/DialogueOption.h>

// Shared dialogue: one conversation for the party, run by the host's game (docs/design/shared-dialogue.md).
// Conversation ids count from 1; steps count picks from 1.

// A game's player started talking to a copy: the host's game should start it with the real NPC.
struct DialogueStartRequest final : RequestMessage
{
    static constexpr RequestOpcode Opcode = kDialogueStartRequest;
    DialogueStartRequest() : RequestMessage(Opcode) {}

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    uint32_t NpcId{}; // session id
};

// The host's game: its player's dialogue menu opened with this NPC.
struct DialogueOpenedRequest final : RequestMessage
{
    static constexpr RequestOpcode Opcode = kDialogueOpenedRequest;
    DialogueOpenedRequest() : RequestMessage(Opcode) {}

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    uint32_t NpcId{};
};

// The host's game's options for a step, after every change (Choices: the last line's choices, not the speaker's
// topics).
struct DialogueOptionsRequest final : RequestMessage
{
    static constexpr RequestOpcode Opcode = kDialogueOptionsRequest;
    DialogueOptionsRequest() : RequestMessage(Opcode) {}

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    uint32_t Conversation{};
    uint32_t Step{};
    bool Choices{};
    DialogueOptions Options{};
};

// A game's player picked a topic of the step: Info is the line its own game gives its player for it (empty: none),
// the one said; Text, the option as its menu showed it.
struct DialoguePickRequest final : RequestMessage
{
    static constexpr RequestOpcode Opcode = kDialoguePickRequest;
    DialoguePickRequest() : RequestMessage(Opcode) {}

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    uint32_t Conversation{};
    uint32_t Step{};
    GameId Topic{};
    GameId Info{};
    std::string Text{};
};

// A game's player left the conversation (its menu closed); it ends once nobody is in it. From the host's game
// with Ended: its conversation ended (a goodbye, the NPC walking off), so it ends for everyone.
struct DialogueCloseRequest final : RequestMessage
{
    static constexpr RequestOpcode Opcode = kDialogueCloseRequest;
    DialogueCloseRequest() : RequestMessage(Opcode) {}

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    uint32_t Conversation{};
    bool Ended{};
};

// A game's player joined the conversation (its menu opened on the NPC, or its copy). The player who asked for it
// is in it from the start.
struct DialogueJoinRequest final : RequestMessage
{
    static constexpr RequestOpcode Opcode = kDialogueJoinRequest;
    DialogueJoinRequest() : RequestMessage(Opcode) {}

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    uint32_t Conversation{};
};

// The host's game: its player would have gained Speech experience from a line another player picked (a
// persuade's script runs there); it goes to that player.
struct DialogueExperienceRequest final : RequestMessage
{
    static constexpr RequestOpcode Opcode = kDialogueExperienceRequest;
    DialogueExperienceRequest() : RequestMessage(Opcode) {}

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    float Experience{};
};

// To the host's game: start its player's conversation with this NPC.
struct NotifyDialogueStart final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifyDialogueStart;
    NotifyDialogueStart() : NotifyMessage(Opcode) {}

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    uint32_t NpcId{};
};

// A conversation opened: the player who asked opens its menu on its copy (others join by talking to it); the
// host's game learns its id.
struct NotifyDialogueOpen final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifyDialogueOpen;
    NotifyDialogueOpen() : NotifyMessage(Opcode) {}

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    uint32_t Conversation{};
    uint32_t NpcId{};
};

// The host's options for a step: each game's menu shows their topics with its own player's lines (Choices: a
// line's choices, nothing else to say).
struct NotifyDialogueList final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifyDialogueList;
    NotifyDialogueList() : NotifyMessage(Opcode) {}

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    uint32_t Conversation{};
    uint32_t Step{};
    bool Choices{};
    DialogueOptions Options{};
};

// A topic was picked: the host's game says Info (the picker's line, else the host's); every other menu shows the
// pick and waits for the next step's list.
struct NotifyDialoguePicked final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifyDialoguePicked;
    NotifyDialoguePicked() : NotifyMessage(Opcode) {}

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    uint32_t Conversation{};
    uint32_t Step{};
    GameId Topic{};
    GameId Info{};
    uint32_t PlayerId{}; // who picked
};

// The conversation ended: every game closes its menu, the host's ends its conversation.
struct NotifyDialogueClosed final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifyDialogueClosed;
    NotifyDialogueClosed() : NotifyMessage(Opcode) {}

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    uint32_t Conversation{};
};

// Speech experience from a line this player picked: its player gains it.
struct NotifyDialogueExperience final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifyDialogueExperience;
    NotifyDialogueExperience() : NotifyMessage(Opcode) {}

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    float Experience{};
};
