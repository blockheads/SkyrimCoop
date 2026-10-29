#include <Services/Guest/GuestScene.h>

#include <Bridges/GuestEngine.h>
#include <Components/PlayerComponent.h>
#include <Components/RemoteComponent.h>
#include <Messages/Relayed.h>
#include <Services/SessionService.h>
#include <World.h>

#include <algorithm>

GuestScene::GuestScene(World& aWorld, entt::dispatcher& aDispatcher, GuestEngine& aEngine) noexcept
    : m_world(aWorld), m_engine(aEngine)
{
    Listen<NotifyAiState, &GuestScene::OnAiState>(m_connections, aDispatcher, this);
}

GuestScene::~GuestScene() noexcept
{
    if (m_scene != GameId{})
        m_engine.EndScenePackage();
}

void GuestScene::OnAiState(const NotifyAiState& acMessage) noexcept
{
    // Only the host's player drives the party's scenes; another guest's ordinary AI state must not end one.
    const auto actors = m_world.view<RemoteComponent, PlayerComponent>();
    const auto actor = std::ranges::find_if(actors, [&](entt::entity aEntity) {
        return actors.get<RemoteComponent>(aEntity).Id == acMessage.SessionId;
    });
    if (actor == actors.end())
        return;
    const auto& players = m_world.GetSessionService().GetPlayers();
    const auto player = players.find(actors.get<PlayerComponent>(*actor).Id);
    if (player == players.end() || !player->second.IsHost)
        return;
    const ActorAiState& cState = acMessage.State;
    if (cState.Scene == GameId{} || cState.Package == GameId{})
    {
        if (m_scene == GameId{})
            return;
        spdlog::info("GuestScene: scene {:X} let the host's player go: this game's player is its own again", m_scene.LogFormat());
        m_engine.EndScenePackage();
        m_scene = m_package = GameId{};
        return;
    }
    if (cState.Package == m_package)
        return;
    if (!m_engine.RunScenePackage(cState.Package))
    {
        spdlog::warn("GuestScene: the host's scene package {:X} is not in this game", cState.Package.LogFormat());
        return;
    }
    spdlog::info("GuestScene: scene {:X}: this game's player runs the host's player's package {:X}", cState.Scene.LogFormat(),
                 cState.Package.LogFormat());
    m_scene = cState.Scene;
    m_package = cState.Package;
}
