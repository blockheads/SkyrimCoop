#pragma once

#include <network/ENetInterface.h>
#include <Structs/DialogueOption.h>

#include <algorithm>
#include <optional>

struct HostAuthority;
struct HostCharacters;
struct HostPlayers;
struct DialogueStartRequest;
struct DialogueOpenedRequest;
struct DialogueOptionsRequest;
struct DialoguePickRequest;
struct DialogueCloseRequest;
struct DialogueJoinRequest;
struct DialogueExperienceRequest;

struct HostNetwork;

/**
 * @brief One conversation for the party, run by the host's game: whoever starts it, the host's game says its
 * lines. The step's list is the host's options; each game's menu shows their topics with its own player's lines.
 * A pick is said with the picker's own line, and the chat names who picked what; the Speech experience its script
 * gives is the picker's. Games report their options, picks, joins and exits; this decides the list, which pick
 * is said and when it ends: once nobody is in it (docs/design/shared-dialogue.md).
 */
struct HostDialogue
{
    using ConnectionId_t = Net::ConnectionId_t;

    HostDialogue(HostAuthority& aAuthority, HostNetwork& aNetwork, const HostPlayers& acPlayers,
                 const HostCharacters& acCharacters) noexcept;

    NO_COPY_MOVE(HostDialogue);

    void OnStart(const DialogueStartRequest& acRequest, ConnectionId_t aSender) noexcept;
    void OnOpened(const DialogueOpenedRequest& acRequest, ConnectionId_t aSender) noexcept;
    void OnOptions(const DialogueOptionsRequest& acRequest, ConnectionId_t aSender) noexcept;
    void OnPick(const DialoguePickRequest& acRequest, ConnectionId_t aSender) noexcept;
    void OnClose(const DialogueCloseRequest& acRequest, ConnectionId_t aSender) noexcept;
    void OnJoin(const DialogueJoinRequest& acRequest, ConnectionId_t aSender) noexcept;
    void OnExperience(const DialogueExperienceRequest& acRequest, ConnectionId_t aSender) noexcept;
    // After the player is off the list: it leaves the conversation.
    void OnPlayerLeft(ConnectionId_t aPlayer) noexcept;

private:
    struct Conversation
    {
        uint32_t Id{};
        uint32_t NpcId{};
        uint32_t Step = 1;
        // The games in it, in the order they joined: whose menu is open on it, and the peer who asked for it.
        std::vector<ConnectionId_t> Players;
        // The host's game's options for this step (none yet after a pick).
        std::optional<DialogueOptions> HostOptions;

        [[nodiscard]] bool Has(ConnectionId_t aPlayer) const noexcept
        {
            return std::find(Players.begin(), Players.end(), aPlayer) != Players.end();
        }
    };

    // A start the host's game was asked for: that player is in the conversation it opens.
    struct Asked
    {
        ConnectionId_t Player{};
        uint32_t NpcId{};
    };

    // The conversation that request is for (null: none open, or another one or step).
    [[nodiscard]] Conversation* Current(uint32_t aId) noexcept;
    [[nodiscard]] Conversation* Current(uint32_t aId, uint32_t aStep) noexcept;
    // Ends it, telling every game but aExcept (0: all of them).
    void End(ConnectionId_t aExcept = 0) noexcept;
    // The player is out of it: the conversation ends when it was the last one in it.
    void Leave(ConnectionId_t aPlayer) noexcept;

    HostNetwork& m_network;
    const HostPlayers& m_players;
    const HostCharacters& m_characters;
    std::optional<Conversation> m_conversation;
    uint32_t m_lastId = 0;
    std::optional<Asked> m_asked;
    // Who picked last in the latest conversation: the Speech experience of the lines said is theirs.
    ConnectionId_t m_talker{};
};
