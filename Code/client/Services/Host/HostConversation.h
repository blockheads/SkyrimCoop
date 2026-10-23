#pragma once

#include <Events/Listen.h>
#include <Services/DialogueService.h>

#include <atomic>
#include <optional>

struct World;
struct NotifyDialogueStart;

/**
 * @brief The host's game in the party's conversation (docs/design/shared-dialogue.md): its engine runs every
 * conversation, a guest's in the background without its menu; each list its engine shows is a step's, which every
 * game's menu shows; it says every pick as if its player had, its script run for the picker (party-model.md,
 * rule 2).
 */
struct HostConversation final : DialogueService::Role
{
    HostConversation(World& aWorld, entt::dispatcher& aDispatcher, DialogueService& aDialogue) noexcept;
    ~HostConversation() noexcept override;

    NO_COPY_MOVE(HostConversation);

    bool OnStartAsked(uint32_t aActorFormId, bool aForceGreet) noexcept override;
    bool OnTalk(uint32_t aActorFormId) noexcept override;
    [[nodiscard]] bool RunsWithoutMenu() const noexcept override { return true; }
    [[nodiscard]] bool SaysPicksElsewhere() const noexcept override { return false; }
    // Its own player's pick, which its menu says at once: its lines are its own player's.
    void OnPickSent() noexcept override { m_picker = 0; }
    // A guest's pick: the speaker's lines run for that guest's character until the next pick (any thread).
    uint32_t LineFor(uint32_t aSpeakerFormId) noexcept override;
    std::optional<std::vector<IDialogueBridge::Option>> OnListBuilt() noexcept override;
    void OnOpen(const NotifyDialogueOpen& acMessage) noexcept override;
    void OnPicked(const NotifyDialoguePicked& acMessage, uint32_t aTopic, uint32_t aInfo, bool aOwnPick) noexcept override;
    void OnClosed() noexcept override;
    void OnUpdate(bool aMenuOpen, uint32_t aSpeaker) noexcept override;

private:
    // A guest asked for a conversation: this game's engine runs it without its menu.
    void OnStart(const NotifyDialogueStart& acMessage) noexcept;
    // The list the engine shows is the step's.
    void SendOptions() noexcept;
    // That player's character here (0: none).
    [[nodiscard]] uint32_t CharacterOf(uint32_t aPlayerId) const noexcept;

    World& m_world;
    DialogueService& m_dialogue;

    std::atomic<uint32_t> m_greeter{}; // the NPC force-greeting the player, until it opens
    // The character of the guest who picked last in the latest conversation, and its speaker (read on any thread).
    std::atomic<uint32_t> m_picker{};
    std::atomic<uint32_t> m_pickerSpeaker{};
    bool m_menuAsked{}; // the menu asked for on the conversation under way, not open yet
    uint32_t m_speaker{};
    // Picked: the next list the engine builds, once the NPC has answered, is the next step's.
    bool m_awaitingList{};

    std::vector<entt::scoped_connection> m_connections;
};
