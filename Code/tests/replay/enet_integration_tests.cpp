#include <catch2/catch.hpp>
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

static void GenerateIntegrationFixture()
{
    std::filesystem::create_directories(std::string(FIXTURE_PATH) + "/enet");

    std::ofstream out(IntegFixturePath("integration_flow.ndjson"));

    // Auth
    AuthenticationRequest authReq;
    authReq.Token = "integ_token";
    authReq.Version = BUILD_COMMIT;
    authReq.Username = "IntegPlayer";
    authReq.SKSEActive = true;
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

// Note: Multi-harness determinism test removed due to enet6 static state
// corruption after many GameServer instantiations in a single process.
// Determinism is verified by the replay_tests.cpp suite instead.
