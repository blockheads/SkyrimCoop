#pragma once

#include <RmlUi/Core/DataModelHandle.h>

struct World;
struct UiRenderer;
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
 * Today: the party panel, everyone else in the session (shown while connected).
 */
struct UiService
{
    UiService(World& aWorld, IActorBridge& aActorBridge) noexcept;
    ~UiService() noexcept;

    TP_NOCOPYMOVE(UiService);

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

private:
    void UpdateParty() noexcept;
    void LoadDocuments() noexcept;
    // The newest change to any file in the UI folder.
    [[nodiscard]] std::filesystem::file_time_type LastChange() const noexcept;

    World& m_world;
    IActorBridge& m_actorBridge;
    IDXGISwapChain* m_pSwapChain = nullptr;
    UiRenderer* m_pRenderer = nullptr;
    Rml::Context* m_pContext = nullptr;
    Rml::ElementDocument* m_pParty = nullptr;
    Rml::String m_directory;
    std::filesystem::file_time_type m_lastChange{};
    std::chrono::steady_clock::time_point m_nextCheck{};

    Rml::DataModelHandle m_partyModel;
    Rml::Vector<Member> m_members;
    bool m_initialized = false;
};
