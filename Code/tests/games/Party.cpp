#include "Party.h"

namespace GameTest
{
Party::Party(size_t aGuests, const WorldPlugins& acPlugins, bool aJoin,
               const std::optional<NetConditions>& acNet)
{
    if (acNet)
        m_net = *acNet;
    m_games.push_back(NewGame("Dovahkiin", acPlugins));
    As(Host(), [&] {
        REQUIRE(Host().GetHostService().StartHosting(Port()));
        Host().GetTransport().ConnectLocal(*Host().GetHostService().GetNetwork());
    });
    Until([&] { return Host().GetTransport().IsOnline(); }, "the host's game joins its own session");

    const char* cNames[] = {"Lydia", "Aela", "Serana"};
    for (size_t i = 0; i < aGuests; ++i)
    {
        m_games.push_back(NewGame(cNames[i], acPlugins));
        Engine(*m_games.back()).IsGuest = true;
        if (aJoin)
            Join(*m_games.back());
    }
}

void Party::Join(World& aGuest)
{
    uint16_t port = Host().GetHostService().GetNetwork()->GetPort();
    if (m_net.Any())
    {
        auto& pRelay = m_relays[&aGuest];
        if (!pRelay)
            pRelay = std::make_unique<UdpRelay>(port, m_net, m_net.Seed + static_cast<uint32_t>(m_relays.size()));
        port = pRelay->Port();
    }
    As(aGuest, [&] { REQUIRE(aGuest.GetTransport().Connect(std::format("127.0.0.1:{}", port))); });
    Until([&] { return aGuest.GetTransport().IsOnline(); }, std::format("{} joins", Engine(aGuest).Name));
}

void Party::Agree(const std::string& acStep)
{
    Until([&] { return ActorDifferences().empty(); }, acStep, [&] {
        std::string why;
        for (const auto& cDifference : ActorDifferences())
            why += cDifference + "; ";
        return why;
    });
}

Party::~Party()
{
    for (auto& pGame : m_games)
        As(*pGame, [&] { pGame->GetTransport().Close(); });
    SetCurrentWorld(nullptr);
}

void Party::As(World& aGame, const std::function<void()>& aAction)
{
    SetCurrentWorld(&aGame);
    aAction();
}

void Party::Frame()
{
    PumpRelays();
    for (auto& pGame : m_games)
        As(*pGame, [&] { pGame->Update(); });
}

void Party::Step(World& aGame)
{
    PumpRelays();
    As(aGame, [&] { aGame.Update(); });
}

void Party::Until(const std::function<bool()>& aDone, const std::string& acStep,
           const std::function<std::string()>& acWhy, World* apOnly,
           std::chrono::seconds aTimeout)
{
    const auto cDeadline = std::chrono::steady_clock::now() + std::max(std::chrono::seconds(m_net.Any() ? 20 : 2), aTimeout);
    size_t frames = 0;
    while (!aDone())
    {
        if (std::chrono::steady_clock::now() > cDeadline)
            FAIL("never happened: " << acStep << (acWhy ? " (" + acWhy() + ")" : std::string()) << ", " << frames
                                    << " frames" << (m_net.Any() ? ", " + m_net.Describe() : std::string()));
        apOnly ? Step(*apOnly) : Frame();
        ++frames;
    }
}

void Party::WorldScript(World& aGame, const LocalCall& acCall)
{
    As(aGame, [&] { Engine(aGame).RunScript(acCall); });
}

void Party::Leave(World& aGuest)
{
    As(aGuest, [&] { aGuest.GetTransport().Close(); });
}

void Party::PumpRelays()
{
    for (auto& [cpGame, pRelay] : m_relays)
        pRelay->Pump();
}

uint16_t Party::Port()
{
    const char* cpPort = std::getenv("GAMETESTS_PORT");
    return cpPort ? static_cast<uint16_t>(std::atoi(cpPort)) : 21578;
}

std::unique_ptr<World> Party::NewGame(const char* acpName, const WorldPlugins& acPlugins)
{
    auto pGame = std::make_unique<World>();
    Engine(*pGame).Name = acpName;
    pGame->ctx().at<FakeSteam&>().Name = acpName;
    if (acPlugins)
        acPlugins(Engine(*pGame));
    return pGame;
}
} // namespace GameTest
