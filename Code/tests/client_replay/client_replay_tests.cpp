/// client_replay_tests.cpp
/// Smoke tests for the ReplayBridge compilation boundary.
/// These tests prove that ReplayBridge compiles and links without any SKSE header.

#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>

#include <Bridges/ReplayBridge.h>

TEST_CASE("ReplayBridge can be instantiated without SKSE", "[bridge_smoke]")
{
    ReplayBridge bridge;

    SECTION("Default actor queries return safe values")
    {
        CHECK(bridge.ActorExists(0x12345) == false);
        CHECK(bridge.IsActorDead(0x12345) == false);
        CHECK(bridge.IsRemotePlayer(0x12345) == false);
        CHECK(bridge.GetActorValue(0x12345, 24) == 0.f);
    }

    SECTION("Default player queries return safe values")
    {
        // 0x14 is the canonical Skyrim player form ID
        CHECK(bridge.GetPlayerFormId() == 0x14u);
        CHECK(bridge.IsPlayerAlive() == true);
        CHECK(bridge.GetPlayerLevel() == 1u);
    }

    SECTION("Default form queries return safe values")
    {
        CHECK(bridge.FormExists(0xABCDu) == false);
        CHECK(bridge.GetGlobalValue(0xABCDu) == 0.f);
    }

    SECTION("Default world queries return safe values")
    {
        CHECK(bridge.GetCurrentWeatherId() == 0u);
        CHECK(bridge.GetGameTimeHours() == Approx(12.f));
    }

    SECTION("LoadRecording on missing file does not crash")
    {
        bridge.LoadRecording("/nonexistent/path/recording.ndjson");
        // After loading a missing file the bridge still returns defaults
        CHECK(bridge.ActorExists(0x1) == false);
    }
}

TEST_CASE("ReplayBridge event injection calls registered callbacks", "[bridge_smoke]")
{
    ReplayBridge bridge;

    bool questStartReceived = false;
    uint32_t receivedQuestId = 0;
    bool receivedStarted = false;

    bridge.RegisterQuestStartStopSink([&](uint32_t aQuestId, bool aStarted) {
        questStartReceived = true;
        receivedQuestId = aQuestId;
        receivedStarted = aStarted;
    });

    bridge.InjectQuestStartStopEvent(0xDEAD, true);

    CHECK(questStartReceived == true);
    CHECK(receivedQuestId == 0xDEADu);
    CHECK(receivedStarted == true);
}
