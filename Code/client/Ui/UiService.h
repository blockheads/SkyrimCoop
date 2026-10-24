#pragma once

#include <Events/Listen.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <NoCopyMove.h>

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
 * Today: the party panel (everyone else in the session, while connected) and the chat (Enter opens
 * its line; the last lines show for a while after they arrive).
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

    // A player in the party panel.
    struct Member
    {
        Rml::String Name;
        bool IsHost = false;
        int Level = 0;
        // 0..1 of its health, -1 while its character is not in our game.
        float Health = -1.f;

        bool operator==(const Member&) const = default;
    };

    // What the party panel lists (test harness), and whether its document is up.
    [[nodiscard]] const Rml::Vector<Member>& GetPartyMembers() const noexcept { return m_members; }
    [[nodiscard]] bool IsPartyPanelShown() const noexcept { return m_pParty != nullptr; }

    // A line in the chat.
    struct ChatLine
    {
        Rml::String Name;
        Rml::String Text;
        bool System = false;

        bool operator==(const ChatLine&) const = default;
    };

    // What the chat received (test harness), whether its line is open, and what is typed in it.
    [[nodiscard]] Rml::Vector<ChatLine> GetChatLines() const noexcept;
    [[nodiscard]] bool IsChatOpen() const noexcept { return m_chatOpen; }
    [[nodiscard]] const Rml::String& GetChatDraft() const noexcept { return m_chatDraft; }

private:
    void UpdateParty() noexcept;
    void OnChatMessage(const NotifyChatMessageBroadcast& acMessage) noexcept;
    void OnSessionNotice(const SessionNoticeEvent& acNotice) noexcept;
    // The keys typed into the chat line, and which lines show.
    void UpdateChat() noexcept;
    void SendChat() noexcept;
    // A line starting with '/': /settime <hours> <minutes> sets the host's clock (everyone's).
    void RunChatCommand(const Rml::String& acLine) noexcept;
    void ShowSystemLine(const Rml::String& acText) noexcept;
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
    Rml::String m_directory;
    std::filesystem::file_time_type m_lastChange{};
    std::chrono::steady_clock::time_point m_nextCheck{};

    Rml::DataModelHandle m_partyModel;
    Rml::Vector<Member> m_members;

    struct Received
    {
        ChatLine Line;
        std::chrono::steady_clock::time_point At;
    };
    Rml::DataModelHandle m_chatModel;
    Rml::Vector<Received> m_chatHistory;
    Rml::Vector<ChatLine> m_chatShown;
    Rml::String m_chatDraft;
    bool m_chatOpen = false;
    std::vector<entt::scoped_connection> m_connections;
    bool m_initialized = false;
};
