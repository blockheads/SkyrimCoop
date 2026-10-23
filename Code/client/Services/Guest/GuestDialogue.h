#pragma once

#include <Services/DialogueService.h>

#include <chrono>
#include <string>
#include <Messages/RelayedMessages.h>

struct World;
struct NotifyDialogueList;
struct NotifyDialogueRefused;

/**
 * @brief A guest's game in the party's conversation (docs/design/shared-dialogue.md): talking to a copy asks the
 * host, whose game runs it; its menu opens on the copy and shows each step's list as the host's game built it; its
 * picks go to the host, whose game says them.
 */
struct GuestDialogue final : DialogueService::Role
{
    GuestDialogue(World& aWorld, entt::dispatcher& aDispatcher, DialogueService& aDialogue) noexcept;
    ~GuestDialogue() noexcept override;

    NO_COPY_MOVE(GuestDialogue);

    bool OnStartAsked(uint32_t aActorFormId, bool aForceGreet) noexcept override;
    bool OnTalk(uint32_t aActorFormId) noexcept override;
    [[nodiscard]] bool RunsWithoutMenu() const noexcept override { return false; }
    [[nodiscard]] bool SaysPicksElsewhere() const noexcept override { return true; }
    void OnOpen(const NotifyDialogueOpen& acMessage) noexcept override;
    void OnClosed() noexcept override;
    // The host's list for the current step, in place of any its engine builds.
    std::optional<std::vector<IDialogueBridge::Option>> OnListBuilt() noexcept override;
    void OnUpdate(bool aMenuOpen, uint32_t aSpeaker) noexcept override;

private:
    void OnList(const NotifyDialogueList& acMessage) noexcept;
    void OnRefused(const NotifyDialogueRefused& acMessage) noexcept;
    // The owner's lines relayed to copies: the last one of each kind, to say again once the menu is open.
    void OnLine(const NotifyDialogue& acMessage) noexcept;
    void OnSubtitle(const NotifySubtitle& acMessage) noexcept;
    // A menu opening on the copy silences it: the host's greeting, relayed just before, plays again.
    void ReplayGreeting() noexcept;
    // The host's list in an open menu.
    void ShowList() noexcept;

    World& m_world;
    DialogueService& m_dialogue;

    uint32_t m_asked{}; // the NPC this guest asked the host to talk to (session id), until it opens
    bool m_menuOpen{};
    // The host's list for a step (this game's form ids), and which step this game's menu shows.
    uint32_t m_step{};
    std::vector<IDialogueBridge::Option> m_options;
    bool m_choices{};
    uint32_t m_shownStep{};
    struct HeardLine
    {
        uint32_t SessionId{};
        std::string Text; // the sound file, or the subtitle
        uint32_t TopicInfo{};
        std::chrono::steady_clock::time_point At;
    };
    HeardLine m_lastLine;
    HeardLine m_lastSubtitle;

    entt::scoped_connection m_listConnection;
    entt::scoped_connection m_refusedConnection;
    entt::scoped_connection m_lineConnection;
    entt::scoped_connection m_subtitleConnection;
};
