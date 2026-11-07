#include "Party.h"
#include "Fixtures.h"

TEST_CASE("The smoke actor check catches Skyrim's NPC copy mismatches before cleanup", "[copies][desync-checks]")
{
    const auto fault = GENERATE(std::string("animation"), std::string("name"), std::string("magicka"),
                                std::string("pose.head"), std::string("pose.pelvis"), std::string("weapon_state"));
    Party party(1, [](FakeEngine& engine) {
        SmokeWorld(engine);
        engine.Npcs[0x1C194].Name = "Onmund";
    });
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    uint32_t npc = 0;
    party.As(party.Host(), [&] { npc = host.LoadNpc(0x1C194, 0x31D2E, {}); });
    party.Until([&] { return guest.CopiesOf(0x1C194).size() == 1; }, "the NPC copy appears");
    const auto copy = guest.CopiesOf(0x1C194).front();
    ActionEvent gesture;
    gesture.ActionId = 0x489F1;
    gesture.IdleId = 0x10DEB8;
    gesture.EventName = "MotionDrivenDialogueGeneric";
    party.As(party.Host(), [&] { REQUIRE(host.PerformAnimation(npc, gesture)); });
    party.Until([&] { return guest.GetActorLatestAnimation(copy).IdleId == gesture.IdleId; }, "the gesture reaches the copy");
    party.Agree("the NPC agrees before the fault");
    host.Actors.at(npc).Pose = guest.Actors.at(copy).Pose = FakeEngine::Actor::BodyPose{83.1f, 50.5f, -1.4f};
    CHECK(party.ActorDifferences().empty());

    // Check the observer independently of the transport: these are the native failure boundaries,
    // not a claim that clearing a marker is the cause of the intermittent Skyrim zero-marker report.
    if (fault == "animation")
        guest.Actors.at(copy).LatestAnimation = {};
    else if (fault == "name")
        guest.Npcs.at(guest.GetActorBaseFormId(copy)).Name = "Bandit Outlaw";
    else if (fault == "magicka")
        guest.Actors.at(copy).Values.ActorValuesList[25] -= 30.f;
    else if (fault == "weapon_state")
        guest.Actors.at(copy).Weapon = WeaponState::Sheathing;
    else if (fault == "pose.head")
        guest.Actors.at(copy).Pose->Head = 122.2f;
    else
        guest.Actors.at(copy).Pose->Pelvis += 21.f;
    const auto differences = party.ActorDifferences();
    INFO(fault);
    REQUIRE(std::ranges::any_of(differences, [&](const std::string& difference) { return difference.contains(fault); }));
}

TEST_CASE("An off-screen NPC's old skeleton pose is not its current animation", "[copies][animation][culled-pose]")
{
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    uint32_t lydia = 0;
    party.As(party.Host(), [&] {
        lydia = host.LoadNpc(0xA2C8E, 0x31D2E, {});
        host.Actors.at(lydia).GraphState = "StandingIdle";
    });
    party.Until([&] { return guest.CopiesOf(0xA2C8E).size() == 1; }, "Lydia's copy appears");
    const auto copy = guest.CopiesOf(0xA2C8E).front();
    const FakeEngine::Actor::BodyPose landing{88.f, 52.f, -1.4f}, standing{124.f, 71.f, 0.f};
    host.RenderBodyPose(lydia, landing);
    guest.RenderBodyPose(copy, standing);
    host.SetBodyVisible(lydia, false); // look_apart in the native conversation
    ActionEvent land;
    land.ActionId = 0x937F5;
    land.EventName = "JumpLand";
    land.IdleId = 0xA8524;
    party.As(party.Host(), [&] { REQUIRE(host.PerformAnimation(lydia, land)); });
    party.Until([&] { return guest.GetActorLatestAnimation(copy).IdleId == land.IdleId; }, "both graphs took the landing action");
    // The graph advances, but an off-screen skeleton retains its last drawn pose. Loading
    // the same saved state did not fix the native reading; looking at its owner did.
    host.RenderBodyPose(lydia, standing);
    REQUIRE(host.Actors.at(lydia).Pose->Head == landing.Head);
    CHECK(guest.Actors.at(copy).GraphState == host.Actors.at(lydia).GraphState);
    party.Agree("a stale off-screen bone cache is not a copy desync");
    host.SetBodyVisible(lydia, true);
    CHECK(host.Actors.at(lydia).Pose->Head == standing.Head);
    CHECK(party.ActorDifferences().empty());
}

TEST_CASE("Look targets compare only where the engine reads them", "[copies][desync-checks][headtrack]")
{
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    uint32_t wolf = 0;
    party.As(party.Host(), [&] { wolf = host.LoadNpc(0xA2C8E, 0x31D2E, {}); });
    party.Until([&] { return guest.CopiesOf(0xA2C8E).size() == 1; }, "the copy appears");
    const uint32_t cCopy = guest.CopiesOf(0xA2C8E).front();
    const uint32_t cElsewhere = FakeEngine::kPlayer; // the guest's own player: not whom the owner looks at
    const auto cApart = [&] {
        return std::ranges::any_of(party.ActorDifferences(), [](const std::string& acDifference) { return acDifference.contains("looks at"); });
    };
    party.Agree("the two agree to begin with");

    // A live copy with its own target: a head a player sees turned elsewhere.
    guest.Actors.at(cCopy).Ai.HeadTrackIds[1] = cElsewhere;
    CHECK(cApart());

    // A slot on itself turns no head.
    guest.Actors.at(cCopy).Ai.HeadTrackIds[1] = cCopy;
    CHECK_FALSE(cApart());

    // A slot under a higher one is not read, on either side, until the higher one clears.
    guest.Actors.at(cCopy).Ai.HeadTrackIds = {cElsewhere, 0, 0, cCopy, 0, 0};
    CHECK_FALSE(cApart());
    guest.Actors.at(cCopy).Ai.HeadTrackIds[3] = 0;
    CHECK(cApart());

    // Knocked down (Actor::UpdateAnimation 37361 skips ProcessTracking): each game's body alone.
    guest.Actors.at(cCopy).KnockedDown = true;
    CHECK_FALSE(cApart());
    guest.Actors.at(cCopy).KnockedDown = false;
    CHECK(cApart());

    // Dead, as a killed wolf measured: each game's slots hold its own player, and nothing reads them.
    party.As(party.Host(), [&] { host.Actors.at(wolf).Dead = true; });
    guest.Actors.at(cCopy).Dead = true;
    host.Actors.at(wolf).Ai.HeadTrackIds[0] = FakeEngine::kPlayer;
    CHECK_FALSE(cApart());
}
