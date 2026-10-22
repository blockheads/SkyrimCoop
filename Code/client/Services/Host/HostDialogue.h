#pragma once

#include <network/ENetInterface.h>
#include <Structs/DialogueOption.h>
#include <Structs/GameId.h>

#include <algorithm>
#include <optional>
#include <NoCopyMove.h>

struct HostAuthority;
struct HostCharacters;
struct HostPlayers;
struct DialogueStartRequest;
struct DialogueOpenedRequest;
struct DialogueOptionsRequest;
struct DialoguePickRequest;
struct DialogueCloseRequest;
struct DialogueJoinRequest;
struct DialogueStartFailedRequest;

struct HostNetwork;

/**
 * @brief One conversation for the party, run by the host's game: whoever starts it, the host's game says its
 * lines and builds each step's list, which every game's menu shows. Any player picks from it: the host's game says
 * that option as if its player had, and the chat names who picked what (its script runs for the picker:
 * party-model.md, rule 2). Games report lists, picks, joins and exits; this decides which pick is said
 * and when it ends: once nobody is in it (docs/design/shared-dialogue.md).
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
    void OnStartFailed(const DialogueStartFailedRequest& acRequest, ConnectionId_t aSender) noexcept;
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
        // The host's game's list for this step; none yet after a pick.
        std::optional<DialogueOptions> Options;
        bool Choices{};

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
    // The start that player asked for will not happen; a chat line says why, when given.
    void Refuse(ConnectionId_t aPlayer, uint32_t aNpcId, const char* acpWhy) noexcept;
    // The step's list to that game, once the host's game has shown it.
    void SendList(ConnectionId_t aPlayer) noexcept;
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
};
