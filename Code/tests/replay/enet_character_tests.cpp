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

static std::string EnetFixturePath(const char* aName)
{
    return std::string(FIXTURE_PATH) + "/enet/" + aName;
}

static nlohmann::json MakeAuthLine()
{
    AuthenticationRequest req;
    req.Token = "test_token_abc123";
    req.Version = BUILD_COMMIT;
    req.Username = "TestPlayer";

    auto j = MessageSerializer::ClientToJson(req);
    j["dir"] = "c2s";
    j["peer"] = 1;
    j["ts"] = 0.0;
    return j;
}

static nlohmann::json MakeAssignCharacterLine(double aTimestamp)
{
    AssignCharacterRequest req;
    req.Cookie = 42;
    req.ReferenceId = GameId(0, 0x14);
    req.FormId = GameId{};
    req.CellId = GameId(0, 0x3C);
    req.WorldSpaceId = GameId(0, 0x3C);

    auto j = MessageSerializer::ClientToJson(req);
    j["dir"] = "c2s";
    j["peer"] = 1;
    j["ts"] = aTimestamp;
    return j;
}

static nlohmann::json MakeMovementLine(uint32_t aServerId, double aTimestamp)
{
    ClientReferencesMoveRequest req;
    req.Tick = 100;

    ReferenceUpdate update;
    update.UpdatedMovement.Position = {100.0f, 200.0f, 300.0f};

    req.Updates[aServerId] = update;

    auto j = MessageSerializer::ClientToJson(req);
    j["dir"] = "c2s";
    j["peer"] = 1;
    j["ts"] = aTimestamp;
    return j;
}

static void GenerateCharacterAssignFixture()
{
    std::filesystem::create_directories(std::string(FIXTURE_PATH) + "/enet");

    std::ofstream out(EnetFixturePath("character_assign.ndjson"));
    out << MakeAuthLine().dump() << "\n";
    out << MakeAssignCharacterLine(0.1).dump() << "\n";
}

static void GenerateMovementFixture()
{
    std::filesystem::create_directories(std::string(FIXTURE_PATH) + "/enet");

    std::ofstream out(EnetFixturePath("movement_sync.ndjson"));
    out << MakeAuthLine().dump() << "\n";
    out << MakeAssignCharacterLine(0.1).dump() << "\n";
    out << MakeMovementLine(1, 0.2).dump() << "\n";
}

TEST_CASE("Character assign replay produces AssignCharacterResponse", "[Enet6][Character]")
{
    GenerateCharacterAssignFixture();

    ServerReplayHarness harness;
    size_t count = harness.ReplayFile(EnetFixturePath("character_assign.ndjson"), 1);

    REQUIRE(count == 2);

    auto& captured = harness.GetCapturedServerMessages();
    REQUIRE(captured.size() >= 1);
    CHECK(captured[0]["type"] == "AuthenticationResponse");

    bool hasAssignResponse = false;
    for (auto& msg : captured)
    {
        if (msg["type"] == "AssignCharacterResponse")
        {
            hasAssignResponse = true;
            break;
        }
    }
    CHECK(hasAssignResponse);
}

TEST_CASE("Movement replay produces server response", "[Enet6][Movement]")
{
    GenerateMovementFixture();

    ServerReplayHarness harness;
    size_t count = harness.ReplayFile(EnetFixturePath("movement_sync.ndjson"), 1);

    REQUIRE(count == 3);

    auto& captured = harness.GetCapturedServerMessages();
    REQUIRE(captured.size() >= 1);
    CHECK(captured[0]["type"] == "AuthenticationResponse");

    auto& playerManager = harness.GetWorld().GetPlayerManager();
    auto* pPlayer = playerManager.GetByConnectionId(1);
    REQUIRE(pPlayer != nullptr);
}
