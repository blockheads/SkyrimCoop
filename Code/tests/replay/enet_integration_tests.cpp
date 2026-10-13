#include <catch2/catch.hpp>
#include "dual_replay_harness.h"
#include "server_replay_harness.h"
#include <Recording/MessageSerializer.h>
#include <Messages/AuthenticationRequest.h>
#include <Messages/AssignCharacterRequest.h>
#include <Messages/ClientReferencesMoveRequest.h>
#include <Structs/GameId.h>
#include <Structs/ReferenceUpdate.h>
#include <Structs/Movement.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <string>

#ifndef FIXTURE_PATH
#error "FIXTURE_PATH must be defined by build system"
#endif

static std::string IntegFixturePath(const char* aName)
{
    return std::string(FIXTURE_PATH) + "/enet/" + aName;
}

static std::string ClientFixturePath(const char* aName)
{
    return std::string(FIXTURE_PATH) + "/" + aName;
}

static void GenerateIntegrationFixture()
{
    std::filesystem::create_directories(std::string(FIXTURE_PATH) + "/enet");

    std::ofstream out(IntegFixturePath("integration_flow.ndjson"));

    // Auth
    AuthenticationRequest authReq;
    authReq.Token = "integ_token";
    authReq.Version = BUILD_COMMIT;
    authReq.Username = "IntegPlayer";
    {
        auto j = MessageSerializer::ClientToJson(authReq);
        j["dir"] = "c2s";
        j["peer"] = 1;
        j["ts"] = 0.0;
        out << j.dump() << "\n";
    }

    // Character assign
    AssignCharacterRequest assignReq;
    assignReq.Cookie = 1;
    assignReq.ReferenceId = GameId(0, 0x14);
    assignReq.FormId = GameId{};
    assignReq.CellId = GameId(0, 0x3C);
    assignReq.WorldSpaceId = GameId(0, 0x3C);
    {
        auto j = MessageSerializer::ClientToJson(assignReq);
        j["dir"] = "c2s";
        j["peer"] = 1;
        j["ts"] = 0.1;
        out << j.dump() << "\n";
    }

    // Movement
    ClientReferencesMoveRequest moveReq;
    moveReq.Tick = 200;
    ReferenceUpdate update;
    update.UpdatedMovement.Position = {500.0f, 600.0f, 700.0f};
    moveReq.Updates[1] = update;
    {
        auto j = MessageSerializer::ClientToJson(moveReq);
        j["dir"] = "c2s";
        j["peer"] = 1;
        j["ts"] = 0.2;
        out << j.dump() << "\n";
    }
}

TEST_CASE("Full integration: auth + assign + movement in single replay", "[Enet6][Integration]")
{
    GenerateIntegrationFixture();

    ServerReplayHarness harness;
    size_t count = harness.ReplayFile(IntegFixturePath("integration_flow.ndjson"), 1);

    REQUIRE(count == 3);

    auto& captured = harness.GetCapturedServerMessages();
    REQUIRE(captured.size() >= 2);

    CHECK(captured[0]["type"] == "AuthenticationResponse");

    size_t authResponses = 0;
    size_t stringCacheUpdates = 0;
    for (auto& msg : captured)
    {
        const auto type = msg["type"].get<std::string>();
        if (type == "AuthenticationResponse")
            authResponses++;
        else if (type == "StringCacheUpdate")
            stringCacheUpdates++;
    }

    CHECK(authResponses == 1);
    CHECK(stringCacheUpdates >= 1);

    auto* pPlayer = harness.GetWorld().GetPlayerManager().GetByConnectionId(1);
    REQUIRE(pPlayer != nullptr);
    CHECK(pPlayer->GetUsername() == "IntegPlayer");
}

static void GenerateAuthFixtureForDual()
{
    std::filesystem::create_directories(std::string(FIXTURE_PATH) + "/enet");

    AuthenticationRequest req;
    req.Token = "dual_token";
    req.Version = BUILD_COMMIT;
    req.Username = "DualPlayer";

    auto j = MessageSerializer::ClientToJson(req);
    j["dir"] = "c2s";
    j["peer"] = 1;
    j["ts"] = 0.0;

    std::ofstream out(IntegFixturePath("dual_auth.ndjson"));
    out << j.dump() << "\n";
}

TEST_CASE("Dual harness replays both sides independently", "[DualReplay]")
{
    GenerateAuthFixtureForDual();

    DualReplayHarness harness;

    // Replay client-side hook events (existing fixture from Phase 12-13)
    size_t clientEvents = harness.ReplayClientFile(ClientFixturePath("actor_spawn.ndjson"));
    REQUIRE(clientEvents == 2);

    // Replay server-side enet6 messages (generated fixture)
    size_t serverMessages = harness.ReplayServerFile(IntegFixturePath("dual_auth.ndjson"), 1);
    REQUIRE(serverMessages == 1);

    // Client side assertions: actor_spawn.ndjson spawns formId 0x14
    REQUIRE(harness.Client().GetPointerTable().Contains(0x14));

    // Server side assertions: auth should produce AuthenticationResponse
    auto& captured = harness.Server().GetCapturedServerMessages();
    REQUIRE(captured.size() >= 1);
    CHECK(captured[0]["type"] == "AuthenticationResponse");
}
