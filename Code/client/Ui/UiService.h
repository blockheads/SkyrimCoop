#pragma once

#include <Events/Listen.h>
#include <Messages/RelayedMessages.h>
#include <NoCopyMove.h>
#include <Services/JoinStatus.h>
#include <optional>
#include <RmlUi/Core/EventListener.h>
#include <ui_models/PartyActivity.h>
#include <ui_models/UiModels.h>

struct World;
struct UiRenderer;
struct NotifyChatMessageBroadcast;
struct SessionNoticeEvent;
struct IDXGISwapChain;
class IActorBridge;

namespace Rml
{
class Context;
class ElementDocument;
} // namespace Rml

/**
 * @brief The in-game UI: RmlUi documents (Code/client/Ui/assets) drawn into the game's frame.
 *
 * Documents are HTML/CSS-like (.rml, .rcss) and bind to data models this service keeps current.
 * The UI folder is Data/SKSE/Plugins/SkyrimCoop/ui (deployed with the DLL), or
 * SKYRIMCOOP_UI_DIR to work on the files in place.
 *
 * Today: the party panel (everyone else in the session, after party activity), the chat (Enter opens
 * its line; the last lines show for a while after they arrive) and the join panel (a join on its way, or why it
 * was refused, until Back).
 */
struct UiService
{
    UiService(World& aWorld, IActorBridge& aActorBridge) noexcept;
    ~UiService() noexcept;

    NO_COPY_MOVE(UiService);

    // On the render thread, from RenderSystemD3D11.
    void Create(IDXGISwapChain* apSwapChain) noexcept;
    void Render() noexcept;
    // Reloads every document and style sheet from disk (done by itself when a file changes).
    void Reload() noexcept;

    using Member = Ui::Member;
    using ChatLine = Ui::ChatLine;

    // What the party panel lists (test harness), and whether its document is up.
    [[nodiscard]] const Rml::Vector<Member>& GetPartyMembers() const noexcept
    {
        return m_party.Members;
    }
    [[nodiscard]] bool IsPartyPanelShown() const noexcept
    {
        return m_pParty != nullptr;
    }
    [[nodiscard]] bool IsPartyPanelVisible() const noexcept
    {
        return m_pParty && m_party.Visible;
    }
    [[nodiscard]] float PartyPanelOpacity() const noexcept
    {
        return m_party.Opacity;
    }

    // What the chat received (test harness), whether its line is open, and what is typed in it.
    [[nodiscard]] Rml::Vector<ChatLine> GetChatLines() const noexcept;
    [[nodiscard]] bool IsChatOpen() const noexcept
    {
        return m_chat.Open;
    }
    [[nodiscard]] const Rml::String& GetChatDraft() const noexcept
    {
        return m_chat.Draft;
    }

  private:
    void UpdateParty() noexcept;
    void UpdateDeath() noexcept;
    // The join panel follows JoinService's status; its buttons take the mouse while they show.
    void UpdateJoin() noexcept;
    void OnJoinButton(const Rml::String& acId) noexcept;
    void OnChatMessage(const NotifyChatMessageBroadcast& acMessage) noexcept;
    // The party's gold changed by another game's player: this HUD's notice of it (party-purse.md).
    void OnPartyGold(const NotifyPartyGold& acMessage) noexcept;
    void OnSessionNotice(const SessionNoticeEvent& acNotice) noexcept;
    // The keys typed into the chat line, and which lines show.
    void UpdateChat() noexcept;
    void SendChat() noexcept;
    // A line starting with '/': /settime <hours> <minutes> sets the host's clock (everyone's).
    void RunChatCommand(const Rml::String& acLine) noexcept;
    void ShowSystemLine(const Rml::String& acText, bool aPerished = false) noexcept;
    void LoadDocuments() noexcept;
    // The newest change to any file in the UI folder.
    [[nodiscard]] std::filesystem::file_time_type LastChange() const noexcept;

    World& m_world;
    IActorBridge& m_actorBridge;
    IDXGISwapChain* m_pSwapChain = nullptr;
    UiRenderer* m_pRenderer = nullptr;
    Rml::Context* m_pContext = nullptr;
    Rml::ElementDocument* m_pParty = nullptr;
    Rml::ElementDocument* m_pChat = nullptr;
    Rml::ElementDocument* m_pJoin = nullptr;
    Rml::ElementDocument* m_pDeath = nullptr;
    Ui::DeathModel m_death;
    Rml::String m_directory;
    std::filesystem::file_time_type m_lastChange{};
    std::chrono::steady_clock::time_point m_nextCheck{};

    Ui::PartyModel m_party;
    Ui::PlayerColors m_playerColors;
    Ui::PartyActivity m_partyActivity;

    struct Received
    {
        ChatLine Line;
        std::chrono::steady_clock::time_point At;
    };
    Ui::JoinModel m_join;
    // The refusal Back put away: hidden until the status changes.
    std::optional<JoinStatus> m_dismissed;
    struct JoinButtons final : Rml::EventListener
    {
        UiService* pService = nullptr;
        void ProcessEvent(Rml::Event& aEvent) override;
    } m_joinButtons;

    Ui::ChatModel m_chat;
    Rml::Vector<Received> m_chatHistory;
    std::vector<entt::scoped_connection> m_connections;
    bool m_initialized = false;
};
