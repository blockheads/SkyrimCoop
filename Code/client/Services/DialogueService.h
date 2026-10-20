#pragma once

#include <Bridges/IDialogueBridge.h>
#include <Structs/DialogueOption.h>

#include <atomic>

struct World;
struct UpdateEvent;
struct DisconnectedEvent;
struct TransportService;
struct NotifyDialogueStart;
struct NotifyDialogueOpen;
struct NotifyDialoguePicked;
struct NotifyDialogueList;
struct NotifyDialogueClosed;
struct NotifyDialogueExperience;
struct NotifyDialogueRefused;
struct NotifyDialogueTraining;
struct NotifyDialogue;
struct NotifySubtitle;

/**
 * @brief This game's side of the party's conversation (HostDialogue decides; docs/design/shared-dialogue.md).
 * Every game runs it, the host's too: talking to a copy asks the host instead, whose game runs it without its
 * menu; talking to its NPC joins the one under way, leaving the menu leaves it, and it ends once nobody is in it.
 * Only the host's game says lines, each with the picker's own. Each menu shows the host's topics with its own
 * player's lines. The Speech experience a line's script gives is the picker's.
 */
struct DialogueService
{
    DialogueService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport,
                    IDialogueBridge& aDialogueBridge) noexcept;

    TP_NOCOPYMOVE(DialogueService);

private:
    // The player talks to an actor: joining the conversation under way (true: done here).
    bool OnTalk(uint32_t aActorFormId) noexcept;
    // Before the player's conversation with an actor starts (false: it does not; aForceGreet: the actor starts it).
    bool OnStartAsked(uint32_t aActorFormId, bool aForceGreet) noexcept;
    void OnUpdate(const UpdateEvent&) noexcept;
    void OnDisconnected(const DisconnectedEvent&) noexcept;
    void OnStart(const NotifyDialogueStart& acMessage) noexcept;
    void OnOpen(const NotifyDialogueOpen& acMessage) noexcept;
    // The player clicked an option: it goes to the host (true: this game's menu does not say it).
    bool OnPickAsked(uint32_t aIndex) noexcept;
    void OnList(const NotifyDialogueList& acMessage) noexcept;
    void OnPicked(const NotifyDialoguePicked& acMessage) noexcept;
    void OnClosed(const NotifyDialogueClosed& acMessage) noexcept;
    // The host's game: Speech experience its player would gain from another player's pick (any thread; true: it
    // goes to that player, through the host).
    bool OnSpeechExperience(float aExperience) noexcept;
    void OnExperience(const NotifyDialogueExperience& acMessage) noexcept;
    void OnRefused(const NotifyDialogueRefused& acMessage) noexcept;
    // The host's game: a trainer's menu about to open (any thread; true: it opens on the last picker's game).
    bool OnTrainingAsked(uint32_t aTrainerFormId) noexcept;
    void OnTraining(const NotifyDialogueTraining& acMessage) noexcept;
    // The owner's lines relayed to copies: the last one of each kind, to say again once the menu is open.
    void OnLine(const NotifyDialogue& acMessage) noexcept;
    void OnSubtitle(const NotifySubtitle& acMessage) noexcept;
    // A peer's menu opening on the copy silences it: the host's greeting, relayed just before, plays again.
    void ReplayGreeting() noexcept;
    // Closes this game's menu, once: a second start of the SWF's fade-out restarts it.
    void CloseMenu() noexcept;

    // The host's game: its options, when they changed, for the step; after a pick, once the NPC has answered.
    void SendOptions() noexcept;
    // A peer's menu: the host's topics with its own player's lines, then its own extras.
    void ShowOwnLines() noexcept;
    [[nodiscard]] std::vector<IDialogueBridge::Option> OwnLines() const noexcept;
    [[nodiscard]] bool IsHost() const noexcept;
    // The copy with this form id's server id (none: not a copy).
    [[nodiscard]] std::optional<uint32_t> CopyServerId(uint32_t aFormId) const noexcept;

    World& m_world;
    TransportService& m_transport;
    IDialogueBridge& m_dialogue;

    uint32_t m_conversation{}; // the one this game is in (0: none)
    uint32_t m_npcServerId{};
    bool m_joined{}; // in the conversation: this game's menu is open on its NPC, the join sent
    bool m_opening{}; // the peer who asked: in it from the open, its menu not shown yet
    uint32_t m_openingFrames{};
    uint32_t m_asked{}; // the NPC this peer asked the host to talk to (server id), until it opens
    std::atomic<uint32_t> m_greeter{}; // the host's game: the NPC force-greeting its player, until it opens
    uint32_t m_step{};
    // The host's game: the player who picked last in the latest conversation, when not its own (read by
    // OnSpeechExperience and OnTrainingAsked on any thread).
    std::atomic<uint32_t> m_talker{};
    // The trainer whose menu opens once this game's dialogue menu has closed.
    uint32_t m_trainer{};
    // The menu as last seen.
    bool m_menuOpen{};
    bool m_closing{}; // its close asked for, the menu still fading out
    bool m_menuAsked{}; // the host's menu asked for on its conversation under way, not open yet
    uint32_t m_speaker{};
    struct HeardLine
    {
        uint32_t ServerId{};
        std::string Text; // the sound file, or the subtitle
        uint32_t TopicInfo{};
        std::chrono::steady_clock::time_point At;
    };
    HeardLine m_lastLine;
    HeardLine m_lastSubtitle;
    std::vector<IDialogueBridge::Option> m_sentOptions;
    uint32_t m_sentStep{};
    // The host's game after a pick: the lists its engine had shown (the next one is the step's).
    std::optional<uint32_t> m_listsAtPick;
    // The host's list of a step, and whether it is a line's choices.
    DialogueOptions m_shared;
    uint32_t m_sharedStep{};
    bool m_sharedChoices{};
    // A peer: its engine's own options (its menu opening on the copy), and what its menu shows (none: to work out).
    std::vector<IDialogueBridge::Option> m_own;
    std::optional<std::vector<IDialogueBridge::Option>> m_lines;
    bool m_redraw{}; // the menu shows its list again (a new step's, or a click that was not said)
    uint32_t m_pickedStep{}; // the step this game's last pick went for

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_disconnectConnection;
    entt::scoped_connection m_startConnection;
    entt::scoped_connection m_openConnection;
    entt::scoped_connection m_pickedConnection;
    entt::scoped_connection m_listConnection;
    entt::scoped_connection m_closedConnection;
    entt::scoped_connection m_experienceConnection;
    entt::scoped_connection m_refusedConnection;
    entt::scoped_connection m_trainingConnection;
    entt::scoped_connection m_lineConnection;
    entt::scoped_connection m_subtitleConnection;
};
