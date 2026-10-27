#pragma once

#include "Message.h"

#include <Structs/DialogueOption.h>
#include <Structs/GameId.h>

// Shared dialogue: one conversation for the party, run by the host's game (docs/design/shared-dialogue.md).
// Conversation ids count from 1; steps count picks from 1.

// A game's player started talking to a copy: the host's game should start it with the real NPC.
struct DialogueStartRequest final : RequestMessage
{
    static constexpr RequestOpcode Opcode = kDialogueStartRequest;
    DialogueStartRequest() : RequestMessage(Opcode) {}

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    uint32_t NpcId{}; // session id
};

// The host's game: its player's dialogue menu opened with this NPC (Everyone: the NPC started it, a force greet).
struct DialogueOpenedRequest final : RequestMessage
{
    static constexpr RequestOpcode Opcode = kDialogueOpenedRequest;
    DialogueOpenedRequest() : RequestMessage(Opcode) {}

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    uint32_t NpcId{};
    bool Everyone{};
    GameId Greeting{}; // host-selected INFO; copies have no arrest/alarm AI to select it
};

// The host's game showed a step's list: its options in menu order (Choices: a line's choices).
struct DialogueOptionsRequest final : RequestMessage
{
    static constexpr RequestOpcode Opcode = kDialogueOptionsRequest;
    DialogueOptionsRequest() : RequestMessage(Opcode) {}

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    uint32_t Conversation{};
    uint32_t Step{};
    bool Choices{};
    DialogueOptions Options{};
};

// A game's player picked an option of the step's list (its topic and line); Text, as its menu showed it.
struct DialoguePickRequest final : RequestMessage
{
    static constexpr RequestOpcode Opcode = kDialoguePickRequest;
    DialoguePickRequest() : RequestMessage(Opcode) {}

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

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

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    uint32_t Conversation{};
    bool Ended{};
};

// A game's player joined the conversation (its menu opened on the NPC, or its copy). The player who asked for it
// is in it from the start.
struct DialogueJoinRequest final : RequestMessage
{
    static constexpr RequestOpcode Opcode = kDialogueJoinRequest;
    DialogueJoinRequest() : RequestMessage(Opcode) {}

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    uint32_t Conversation{};
};

// The host's game could not start the conversation a guest asked for (its player busy, the engine refused).
struct DialogueStartFailedRequest final : RequestMessage
{
    static constexpr RequestOpcode Opcode = kDialogueStartFailedRequest;
    DialogueStartFailedRequest() : RequestMessage(Opcode) {}

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    uint32_t NpcId{};
};

// To the host's game: start its player's conversation with this NPC.
struct NotifyDialogueStart final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifyDialogueStart;
    NotifyDialogueStart() : NotifyMessage(Opcode) {}

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    uint32_t NpcId{};
};

// A conversation opened: the player who asked opens its menu on its copy, every player when the NPC started it
// (Everyone); others join by talking to it. The host's game learns its id.
struct NotifyDialogueOpen final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifyDialogueOpen;
    NotifyDialogueOpen() : NotifyMessage(Opcode) {}

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    uint32_t Conversation{};
    uint32_t NpcId{};
    bool Everyone{};
    GameId Greeting{};
};

// The host's list for a step: every game's menu shows these options (Choices: a line's choices).
struct NotifyDialogueList final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifyDialogueList;
    NotifyDialogueList() : NotifyMessage(Opcode) {}

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    uint32_t Conversation{};
    uint32_t Step{};
    bool Choices{};
    DialogueOptions Options{};
};

// An option of the step was picked: the host's game says it as if its player had; every other menu waits for the
// next step's list.
struct NotifyDialoguePicked final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifyDialoguePicked;
    NotifyDialoguePicked() : NotifyMessage(Opcode) {}

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

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

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    uint32_t Conversation{};
};

// The conversation this game asked for will not start.
struct NotifyDialogueRefused final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifyDialogueRefused;
    NotifyDialogueRefused() : NotifyMessage(Opcode) {}

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    uint32_t NpcId{};
};
