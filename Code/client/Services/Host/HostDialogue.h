#pragma once

#include <network/ENetInterface.h>
#include <Messages/RelayedMessages.h>
#include <Structs/DialogueOption.h>
#include <Structs/GameId.h>

#include <algorithm>
#include <map>
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

// One player's own list for the step, as the party's merge reads it.
struct PartyDialogueList
{
    uint32_t PlayerId{};
    const DialogueOptions* pOptions{};
};

// The party's list (docs/design/shared-dialogue.md): the host's options in its engine's order, then those only others
// have, in aMembers' order; each topic once, with who has it (none: everyone). aBase: the host's game's list.
[[nodiscard]] DialogueOptions MergeDialogueLists(const DialogueOptions& acBase, const std::vector<PartyDialogueList>& acMembers);
// The line a player says for that topic: its own list's (none: it does not know how to say it).
[[nodiscard]] const DialogueOption* DialogueLineFor(const GameId& acTopic, const DialogueOptions& acOwn);

/**
 * @brief One conversation for the party, run by the host's game: whoever starts it, the host's game says its
 * lines and builds each step's list for each player, judged for its character. Every menu shows the party's list
 * (every player's options, tagged with who has them) and one highlight anyone moves; a player says only an option of
 * its own list: the host's game says its line, its script run for it (party-model.md, rule 2). Games report lists,
 * picks, highlights, joins and exits; this decides which pick is said and when it ends: once nobody is in it
 * (docs/design/shared-dialogue.md).
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
    void OnHighlight(const DialogueHighlightRequest& acRequest, ConnectionId_t aSender) noexcept;
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
        // Each other player's own list for the step, by player id (none for a player: the host's).
        std::map<uint32_t, DialogueOptions> PlayerOptions;
        // The party's list for the step, for the players in it now.
        DialogueOptions Merged;
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
    // That game's own list for the step (null: none shown yet).
    [[nodiscard]] const DialogueOptions* OptionsFor(ConnectionId_t aPlayer) const noexcept;
    // The players in it with their own lists, in the order they joined.
    [[nodiscard]] std::vector<PartyDialogueList> Members() const noexcept;
    // The party's list again, after a new list or a player joining or leaving, to every game in it.
    void Merge() noexcept;
    // The step's party list to that game, once the host's game has shown it.
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
