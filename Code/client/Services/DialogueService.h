#pragma once

#include <Events/Listen.h>
#include <Bridges/IDialogueBridge.h>

#include <optional>
#include <NoCopyMove.h>

struct World;
struct UpdateEvent;
struct MenuEvent;
struct DisconnectedEvent;
struct TransportService;
struct NotifyDialogueOpen;
struct NotifyDialoguePicked;
struct NotifyDialogueClosed;

/**
 * @brief This game's side of the party's conversation (HostDialogue decides; docs/design/shared-dialogue.md), the
 * part every game runs: the conversation it is in, joining it (its menu open on the NPC) and leaving it, and its
 * player's picks. The host's game runs the
 * conversation and builds each step's list (Services/Host/HostConversation), a guest's menu shows that list
 * (Services/Guest/GuestDialogue): each plugs in as the role while it exists.
 */
struct DialogueService
{
    // What the host's game or a guest's adds. The engine's questions go to it (IDialogueBridge::Filters).
    struct Role
    {
        virtual ~Role() = default;
        // Before the player's conversation with an actor starts; false stops it.
        virtual bool OnStartAsked(uint32_t aActorFormId, bool aForceGreet) noexcept = 0;
        // The player talks to an actor; true: handled here (a join).
        virtual bool OnTalk(uint32_t aActorFormId) noexcept = 0;
        // The conversation runs on this game without its menu (the host's).
        [[nodiscard]] virtual bool RunsWithoutMenu() const noexcept = 0;
        // A pick from this game's menu is said elsewhere (a guest's): the menu does not say it.
        [[nodiscard]] virtual bool SaysPicksElsewhere() const noexcept = 0;
        // The engine has built a list and shows it (any thread): the options to show instead (none: its own).
        virtual std::optional<std::vector<IDialogueBridge::Option>> OnListBuilt() noexcept { return std::nullopt; }
        // This game's player picked from its menu, before its menu says it (if it does).
        virtual void OnPickSent() noexcept {}
        // An actor's line runs its script (any thread): the player character it runs for (0: this game's).
        virtual uint32_t LineFor(uint32_t /*aSpeakerFormId*/) noexcept { return 0; }
        virtual void OnOpen(const NotifyDialogueOpen&) noexcept {}
        virtual void OnPicked(const NotifyDialoguePicked&, uint32_t /*aTopic*/, uint32_t /*aInfo*/, bool /*aOwnPick*/) noexcept {}
        virtual void OnClosed() noexcept {}
        // Each update, after the shared part, with the menu's state and the speaker this game talks to.
        virtual void OnUpdate(bool /*aMenuOpen*/, uint32_t /*aSpeaker*/) noexcept {}
    };

    DialogueService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport, IDialogueBridge& aBridge) noexcept;
    ~DialogueService() noexcept;

    NO_COPY_MOVE(DialogueService);

    // The role while it exists (null: none, offline).
    void SetRole(Role* apRole) noexcept;

    [[nodiscard]] IDialogueBridge& Bridge() noexcept { return m_bridge; }
    [[nodiscard]] TransportService& Transport() noexcept { return m_transport; }
    // The conversation this game knows of (0: none), its NPC's session id and step.
    [[nodiscard]] uint32_t Conversation() const noexcept { return m_conversation; }
    [[nodiscard]] uint32_t NpcSessionId() const noexcept { return m_npcSessionId; }
    [[nodiscard]] uint32_t Step() const noexcept { return m_step; }
    [[nodiscard]] bool Joined() const noexcept { return m_joined; }
    // This game's menu opens on the NPC (a guest's on its copy): in it from now, until it opens or comes to nothing.
    void Opening() noexcept;
    // A new conversation of the host's own, before the host's rules give it an id.
    void Restart() noexcept;
    // Closes this game's menu, once: a second start of the SWF's fade-out restarts it.
    void CloseMenu() noexcept;
    // The copy with this form id's session id (none: not a copy).
    [[nodiscard]] std::optional<uint32_t> CopySessionId(uint32_t aFormId) const noexcept;

private:
    void OnUpdate(const UpdateEvent&) noexcept;
    void OnMenu(const MenuEvent& acEvent) noexcept;
    void OnDisconnected(const DisconnectedEvent&) noexcept;
    void OnOpen(const NotifyDialogueOpen& acMessage) noexcept;
    void OnPicked(const NotifyDialoguePicked& acMessage) noexcept;
    void OnClosed(const NotifyDialogueClosed& acMessage) noexcept;
    // The player clicked an option: it goes to the host (true: this game's menu does not say it).
    bool OnPickAsked(uint32_t aIndex) noexcept;

    World& m_world;
    TransportService& m_transport;
    IDialogueBridge& m_bridge;
    Role* m_pRole = nullptr;

    uint32_t m_conversation{};
    uint32_t m_npcSessionId{};
    uint32_t m_step{};
    bool m_joined{}; // in it: this game's menu is open on its NPC, the join sent
    bool m_opening{};
    uint32_t m_openingFrames{};
    bool m_menuOpen{};
    bool m_bartering{};
    bool m_closing{}; // its close asked for, the menu still fading out

    std::vector<entt::scoped_connection> m_connections;
};
