#include <catch2/catch.hpp>
#include "server_replay_harness.h"
#include <Recording/MessageSerializer.h>
#include <Messages/AuthenticationRequest.h>
#include <Messages/AuthenticationResponse.h>
#include <Messages/AssignCharacterRequest.h>
#include <Messages/AssignCharacterResponse.h>
#include <Messages/CharacterSpawnRequest.h>
// Components.h must be used instead of individual component headers (include guard)
#include <Components.h>
#include <Structs/GameId.h>
#include <Structs/ReferenceUpdate.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <string>
#include <algorithm>

#ifndef FIXTURE_PATH
#error "FIXTURE_PATH must be defined by build system"
#endif

static std::string EnetFixturePath(const char* aName)
{
    return std::string(FIXTURE_PATH) + "/enet/" + aName;
}

// Helper: generate a two-player auth+assign fixture that mirrors the real session flow.
// Host authenticates first, then registers character. Client authenticates and registers.
static void GenerateTwoPlayerFixture()
{
    std::filesystem::create_directories(std::string(FIXTURE_PATH) + "/enet");

    // --- Host peer file ---
    {
        std::ofstream out(EnetFixturePath("two_player_host.ndjson"));

        // Host auth
        AuthenticationRequest authReq;
        authReq.Token = "";
        authReq.Version = BUILD_COMMIT;
        authReq.Username = "Host";
        authReq.SKSEActive = true;
        auto j = MessageSerializer::ClientToJson(authReq);
        j["dir"] = "c2s";
        j["peer"] = 1;
        j["ts"] = 0.0;
        out << j.dump() << "\n";

        // Host assigns character (empty FormId for player character)
        AssignCharacterRequest assignReq;
        assignReq.Cookie = 1;
        assignReq.ReferenceId = GameId{0, 0x14};
        // FormId left as default GameId{} — required for player characters
        assignReq.CurrentActorData.IsDead = false;
        auto j2 = MessageSerializer::ClientToJson(assignReq);
        j2["dir"] = "c2s";
        j2["peer"] = 1;
        j2["ts"] = 0.1;
        out << j2.dump() << "\n";

    }

    // --- Client peer file ---
    {
        std::ofstream out(EnetFixturePath("two_player_client.ndjson"));

        // Client auth
        AuthenticationRequest authReq;
        authReq.Token = "";
        authReq.Version = BUILD_COMMIT;
        authReq.Username = "JoinPlayer";
        authReq.SKSEActive = true;
        auto j = MessageSerializer::ClientToJson(authReq);
        j["dir"] = "c2s";
        j["peer"] = 2;
        j["ts"] = 5.0;
        out << j.dump() << "\n";

        // Client assigns character (empty FormId)
        AssignCharacterRequest assignReq;
        assignReq.Cookie = 1;
        assignReq.ReferenceId = GameId{0, 0x14};
        assignReq.CurrentActorData.IsDead = false;
        auto j2 = MessageSerializer::ClientToJson(assignReq);
        j2["dir"] = "c2s";
        j2["peer"] = 2;
        j2["ts"] = 5.1;
        out << j2.dump() << "\n";
    }
}

// Helper to find messages of a given type in captured output
static std::vector<nlohmann::json> FilterByType(
    const std::vector<nlohmann::json>& aCaptured, const std::string& aType)
{
    std::vector<nlohmann::json> result;
    for (auto& msg : aCaptured)
    {
        if (msg.contains("type") && msg["type"] == aType)
            result.push_back(msg);
    }
    return result;
}

// =============================================================================
// Test 1: Two-player auth — both players authenticate and get accepted
// =============================================================================
TEST_CASE("Two-player: both players authenticate successfully", "[Enet6][TwoPlayer]")
{
    GenerateTwoPlayerFixture();

    ServerReplayHarness harness;

    // Replay host auth + assign + movement
    size_t hostMsgs = harness.ReplayFile(EnetFixturePath("two_player_host.ndjson"), 1);
    REQUIRE(hostMsgs == 2);

    // Replay client auth + assign + movement
    size_t clientMsgs = harness.ReplayFile(EnetFixturePath("two_player_client.ndjson"), 2);
    REQUIRE(clientMsgs == 2);

    // Both players should exist in the player manager
    auto& pm = harness.GetWorld().GetPlayerManager();
    auto* pHost = pm.GetByConnectionId(1);
    auto* pClient = pm.GetByConnectionId(2);
    REQUIRE(pHost != nullptr);
    REQUIRE(pClient != nullptr);
    CHECK(pHost->GetUsername() == "Host");
    CHECK(pClient->GetUsername() == "JoinPlayer");
}

// =============================================================================
// Test 2: Character assignment — server creates entities and responds
// =============================================================================
TEST_CASE("Two-player: character assignment creates server entities", "[Enet6][TwoPlayer]")
{
    GenerateTwoPlayerFixture();

    ServerReplayHarness harness;
    harness.ReplayFile(EnetFixturePath("two_player_host.ndjson"), 1);
    harness.ReplayFile(EnetFixturePath("two_player_client.ndjson"), 2);

    auto& captured = harness.GetCapturedServerMessages();

    // Should have AssignCharacterResponse for both players
    auto assignResponses = FilterByType(captured, "AssignCharacterResponse");
    REQUIRE(assignResponses.size() >= 2);

    // Each should have a ServerId
    CHECK(assignResponses[0].contains("payload"));
    CHECK(assignResponses[1].contains("payload"));

    // Both players should have character entities assigned
    auto& pm = harness.GetWorld().GetPlayerManager();
    auto* pHost = pm.GetByConnectionId(1);
    auto* pClient = pm.GetByConnectionId(2);
    REQUIRE(pHost != nullptr);
    REQUIRE(pClient != nullptr);

    // Player::GetCharacter() returns optional<entity> — should have value
    REQUIRE(pHost->GetCharacter().has_value());
    REQUIRE(pClient->GetCharacter().has_value());
    CHECK(pHost->GetCharacter().value() != pClient->GetCharacter().value());
}

// =============================================================================
// Test 3: CharacterSpawnRequest — when client joins, host gets spawn notification
// =============================================================================
TEST_CASE("Two-player: joining client triggers CharacterSpawnRequest to host", "[Enet6][TwoPlayer]")
{
    GenerateTwoPlayerFixture();

    ServerReplayHarness harness;

    // Host registers first
    harness.ReplayFile(EnetFixturePath("two_player_host.ndjson"), 1);

    // Clear captured so we only see messages from client joining
    harness.GetCapturedServerMessages().clear();

    // Client joins
    harness.ReplayFile(EnetFixturePath("two_player_client.ndjson"), 2);

    auto& captured = harness.GetCapturedServerMessages();

    // Server should send CharacterSpawnRequest (to notify host about client's character)
    auto spawnRequests = FilterByType(captured, "CharacterSpawnRequest");
    CHECK(spawnRequests.size() >= 1);

    // Server should also send AssignCharacterResponse to the client
    auto assignResponses = FilterByType(captured, "AssignCharacterResponse");
    CHECK(assignResponses.size() >= 1);
}

// =============================================================================
// Test 4: Character components — entities have correct ECS components
// =============================================================================
TEST_CASE("Two-player: character entities have correct components", "[Enet6][TwoPlayer]")
{
    GenerateTwoPlayerFixture();

    ServerReplayHarness harness;
    harness.ReplayFile(EnetFixturePath("two_player_host.ndjson"), 1);
    harness.ReplayFile(EnetFixturePath("two_player_client.ndjson"), 2);

    auto& world = harness.GetWorld();
    auto& pm = world.GetPlayerManager();

    REQUIRE(pm.GetByConnectionId(1)->GetCharacter().has_value());
    REQUIRE(pm.GetByConnectionId(2)->GetCharacter().has_value());
    auto hostEntity = pm.GetByConnectionId(1)->GetCharacter().value();
    auto clientEntity = pm.GetByConnectionId(2)->GetCharacter().value();

    // Both entities should have CharacterComponent
    REQUIRE(world.all_of<Server::CharacterComponent>(hostEntity));
    REQUIRE(world.all_of<Server::CharacterComponent>(clientEntity));

    // Both should be marked as player characters
    CHECK(world.get<Server::CharacterComponent>(hostEntity).IsPlayer());
    CHECK(world.get<Server::CharacterComponent>(clientEntity).IsPlayer());

    // Both should have MovementComponent
    REQUIRE(world.all_of<Server::MovementComponent>(hostEntity));
    REQUIRE(world.all_of<Server::MovementComponent>(clientEntity));

    // Both should have OwnerComponent
    REQUIRE(world.all_of<Server::OwnerComponent>(hostEntity));
    REQUIRE(world.all_of<Server::OwnerComponent>(clientEntity));
}

// =============================================================================
// Test 5: No "forging packets" rejection — empty FormId passes validation
// =============================================================================
TEST_CASE("Two-player: empty FormId is accepted (no forging packets rejection)", "[Enet6][TwoPlayer]")
{
    std::filesystem::create_directories(std::string(FIXTURE_PATH) + "/enet");

    // Create a fixture with the OLD bug: non-empty FormId on player character
    {
        std::ofstream out(EnetFixturePath("bad_formid.ndjson"));

        AuthenticationRequest authReq;
        authReq.Token = "";
        authReq.Version = BUILD_COMMIT;
        authReq.Username = "BadPlayer";
        authReq.SKSEActive = true;
        auto j = MessageSerializer::ClientToJson(authReq);
        j["dir"] = "c2s";
        j["peer"] = 1;
        j["ts"] = 0.0;
        out << j.dump() << "\n";

        // This was the bug: setting FormId to non-empty for player character
        AssignCharacterRequest assignReq;
        assignReq.Cookie = 1;
        assignReq.ReferenceId = GameId{0, 0x14};
        assignReq.FormId = GameId{0, 0x14};  // BUG: should be empty
        assignReq.CurrentActorData.IsDead = false;
        auto j2 = MessageSerializer::ClientToJson(assignReq);
        j2["dir"] = "c2s";
        j2["peer"] = 1;
        j2["ts"] = 0.1;
        out << j2.dump() << "\n";
    }

    // Bad FormId: server rejects
    {
        ServerReplayHarness harness;
        harness.ReplayFile(EnetFixturePath("bad_formid.ndjson"), 1);

        auto* pPlayer = harness.GetWorld().GetPlayerManager().GetByConnectionId(1);
        REQUIRE(pPlayer != nullptr);
        CHECK(pPlayer->GetCharacter().has_value() == false);
    }

    // Good FormId: server accepts
    {
        std::ofstream out(EnetFixturePath("good_formid.ndjson"));

        AuthenticationRequest authReq;
        authReq.Token = "";
        authReq.Version = BUILD_COMMIT;
        authReq.Username = "GoodPlayer";
        authReq.SKSEActive = true;
        auto j = MessageSerializer::ClientToJson(authReq);
        j["dir"] = "c2s";
        j["peer"] = 1;
        j["ts"] = 0.0;
        out << j.dump() << "\n";

        AssignCharacterRequest assignReq;
        assignReq.Cookie = 1;
        assignReq.ReferenceId = GameId{0, 0x14};
        assignReq.CurrentActorData.IsDead = false;
        auto j2 = MessageSerializer::ClientToJson(assignReq);
        j2["dir"] = "c2s";
        j2["peer"] = 1;
        j2["ts"] = 0.1;
        out << j2.dump() << "\n";
    }

    {
        ServerReplayHarness harness;
        harness.ReplayFile(EnetFixturePath("good_formid.ndjson"), 1);

        auto* pPlayer = harness.GetWorld().GetPlayerManager().GetByConnectionId(1);
        REQUIRE(pPlayer != nullptr);
        CHECK(pPlayer->GetCharacter().has_value());
    }
}

// =============================================================================
// Test 6: Message flow summary — verify expected protocol sequence
// =============================================================================
TEST_CASE("Two-player: full protocol message flow", "[Enet6][TwoPlayer]")
{
    GenerateTwoPlayerFixture();

    ServerReplayHarness harness;
    harness.ReplayFile(EnetFixturePath("two_player_host.ndjson"), 1);
    harness.ReplayFile(EnetFixturePath("two_player_client.ndjson"), 2);

    auto& captured = harness.GetCapturedServerMessages();

    // Collect all unique message types
    std::vector<std::string> types;
    for (auto& msg : captured)
    {
        auto t = msg["type"].get<std::string>();
        if (std::find(types.begin(), types.end(), t) == types.end())
            types.push_back(t);
    }

    // Must have at minimum these message types in the server's response
    auto hasType = [&](const std::string& t) {
        return std::find(types.begin(), types.end(), t) != types.end();
    };

    CHECK(hasType("AuthenticationResponse"));
    CHECK(hasType("AssignCharacterResponse"));
    CHECK(hasType("ServerTimeSettings"));
    CHECK(hasType("NotifyPlayerJoined"));

    // Total captured messages should be substantial (auth responses, time settings,
    // player list updates, character assigns, spawn notifications)
    CHECK(captured.size() >= 8);
}
