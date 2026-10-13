#include <catch2/catch.hpp>
#include "server_replay_harness.h"
#include <Recording/MessageSerializer.h>
#include <Recording/Base64.h>
#include <Messages/AuthenticationRequest.h>
#include <Messages/AuthenticationResponse.h>
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

// Generate auth fixture with a known token and BUILD_COMMIT version
static void GenerateAuthFixture()
{
    std::filesystem::create_directories(std::string(FIXTURE_PATH) + "/enet");

    AuthenticationRequest req;
    req.Token = "test_token_abc123";
    req.Version = BUILD_COMMIT;
    req.Username = "TestPlayer";

    auto j = MessageSerializer::ClientToJson(req);
    j["dir"] = "c2s";
    j["peer"] = 1;
    j["ts"] = 0.0;

    {
        std::ofstream out(EnetFixturePath("auth_flow.ndjson"));
        out << j.dump() << "\n";
    }
}

TEST_CASE("Auth replay creates player in server", "[Enet6][Auth]")
{
    GenerateAuthFixture();

    ServerReplayHarness harness;
    size_t count = harness.ReplayFile(EnetFixturePath("auth_flow.ndjson"), 1);

    REQUIRE(count == 1);

    auto& captured = harness.GetCapturedServerMessages();
    REQUIRE(captured.size() >= 1);
    CHECK(captured[0]["type"] == "AuthenticationResponse");
    CHECK(captured[0].contains("opcode"));
    CHECK(captured[0]["opcode"] == 0);

    auto& playerManager = harness.GetWorld().GetPlayerManager();
    auto* pPlayer = playerManager.GetByConnectionId(1);
    REQUIRE(pPlayer != nullptr);
    CHECK(pPlayer->GetUsername() == "TestPlayer");
}

TEST_CASE("Auth replay with wrong version is rejected", "[Enet6][Auth]")
{
    std::filesystem::create_directories(std::string(FIXTURE_PATH) + "/enet");

    AuthenticationRequest req;
    req.Token = "test_token";
    req.Version = "wrong_version";
    req.Username = "BadPlayer";

    auto j = MessageSerializer::ClientToJson(req);
    j["dir"] = "c2s";
    j["peer"] = 1;
    j["ts"] = 0.0;

    {
        std::ofstream out(EnetFixturePath("auth_wrong_version.ndjson"));
        out << j.dump() << "\n";
    }

    ServerReplayHarness harness;
    size_t count = harness.ReplayFile(EnetFixturePath("auth_wrong_version.ndjson"), 1);

    REQUIRE(count == 1);

    auto& captured = harness.GetCapturedServerMessages();
    REQUIRE(captured.size() >= 1);
    CHECK(captured[0]["type"] == "AuthenticationResponse");

    auto& playerManager = harness.GetWorld().GetPlayerManager();
    auto* pPlayer = playerManager.GetByConnectionId(1);
    CHECK(pPlayer == nullptr);
}
