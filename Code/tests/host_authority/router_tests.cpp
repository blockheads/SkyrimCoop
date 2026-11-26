// HostRouter: the host's table of rules, with no part registered but the test's own.
#include <core/HostNetwork.h>
#include <core/HostPlayers.h>
#include <core/HostRouter.h>

#include <Messages/Relayed.h>
#include <Messages/RelayedMessages.h>

#include <catch2/catch.hpp>

#include <string>
#include <vector>

namespace
{
struct Part
{
    void Rule(const EffectFlashRequest&, Net::ConnectionId_t aSender) noexcept { Heard.push_back("rule " + std::to_string(aSender)); }
    void Watcher(const EffectFlashRequest&, Net::ConnectionId_t) noexcept { Heard.push_back("watcher"); }
    void Other(const EffectFlashRequest&, Net::ConnectionId_t) noexcept { Heard.push_back("other"); }
    std::vector<std::string> Heard;
};
} // namespace

TEST_CASE("A request with no rule is dropped", "[HostRouter]")
{
    HostNetwork network;
    HostPlayers players(network);
    HostRouter router(network, players);
    Part part;
    router.Watch(part, &Part::Watcher);

    CHECK_FALSE(router.Dispatch(EffectFlashRequest{}, 7));
    CHECK(part.Heard.empty()); // a watcher alone hears nothing
}

TEST_CASE("A request's watchers hear it before its rule", "[HostRouter]")
{
    HostNetwork network;
    HostPlayers players(network);
    HostRouter router(network, players);
    Part part;
    router.On(part, &Part::Rule);
    router.Watch(part, &Part::Watcher);

    CHECK(router.Dispatch(EffectFlashRequest{}, 7));
    CHECK(part.Heard == std::vector<std::string>{"watcher", "rule 7"});
}

TEST_CASE("A request has one rule: the last one registered", "[HostRouter]")
{
    HostNetwork network;
    HostPlayers players(network);
    HostRouter router(network, players);
    Part part;
    router.On(part, &Part::Rule);
    router.On(part, &Part::Other);

    CHECK(router.Dispatch(EffectFlashRequest{}, 7));
    CHECK(part.Heard == std::vector<std::string>{"other"});
}
