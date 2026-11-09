// A guest's own NPCs (docs/design/copy-contract.md, "A guest's own NPCs"): each game's engine runs its NPCs' AI on its
// own clock, so a guest's walks them where the host's does not (Skyrim 2026-11-09: Aela and Ria in the Whiterun market
// on the guest, in Jorrvaskr on the host). A guest holds them, without AI or 3D, until the host spawns their copies.
#include "Party.h"
#include "Fixtures.h"

#include <Events/MountEvent.h>

namespace
{
constexpr uint32_t kSmoke = 0x00031D2E;    // the cell everyone stands in
constexpr uint32_t kSinkhole = 0x0009C5E1; // another interior, nobody's loaded
constexpr uint32_t kRia = 0x0001A6D8;      // an NPC placed in kSinkhole (the same reference in every game)
constexpr uint32_t kRiaBase = 0x0001A6D7;
constexpr uint32_t kHorse = 0x00109E3D;
constexpr glm::vec3 kBesideParty{40.f, 10.f, 0.f};

void RiaInTheSinkhole(FakeEngine& aEngine)
{
    SmokeWorld(aEngine);
    aEngine.PlaceNpc(kRia, kRiaBase, kSinkhole, {-50.f, 20.f, 0.f});
}

// Frames enough for the guest to show what it shows: its engine's loads and the host's spawns land within them.
void Settle(Party& aParty)
{
    for (int i = 0; i < 30; ++i)
        aParty.Frame();
}
} // namespace

SCENARIO("A guest's engine walks its own NPC to the party: the guest shows her only as the host's copy",
         "[copies][guest-own-npc]")
{
    Party party(1, RiaInTheSinkhole);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());

    WHEN("the guest's engine walks its Ria to the party while the host's stays in the sinkhole")
    {
        party.As(party.Guest(), [&] { guest.NpcWalks(kRia, kSmoke, kBesideParty); });
        Settle(party);
        THEN("the guest shows no Ria, and her AI stood still")
        {
            CHECK_FALSE(guest.ActorHas3D(kRia));
            CHECK(guest.Actors.at(kRia).Cell == kSinkhole);
            party.Agree("the games agree: nobody shows Ria");
        }

        AND_WHEN("the host's Ria walks to the party too")
        {
            party.As(party.Host(), [&] { host.NpcWalks(kRia, kSmoke, kBesideParty); });
            party.Until([&] { return guest.IsActorCopy(kRia) && guest.ActorHas3D(kRia); },
                        "the guest shows the host's Ria as its copy",
                        [&] { return std::format("copy {}, 3D {}, cell {:X}", guest.IsActorCopy(kRia),
                                                 guest.ActorHas3D(kRia), guest.Actors.at(kRia).Cell); });
            CHECK(guest.Actors.at(kRia).Cell == kSmoke);
            party.Agree("the games agree on Ria");
        }
    }

    WHEN("the party travels to the sinkhole after the host's Ria walked out of it")
    {
        party.As(party.Host(), [&] { host.NpcWalks(kRia, kSmoke, kBesideParty); });
        party.As(party.Host(), [&] { host.PlayerTravels(kSinkhole, {10.f, 10.f, 0.f}); });
        party.Until([&] { return guest.GetPlayerParentCellId() == kSinkhole; }, "the guest is brought to the party");
        Settle(party);
        THEN("the guest's own Ria, where its save left her, gets no 3D as her cell loads")
        {
            CHECK(guest.Actors.at(kRia).Cell == kSinkhole);
            CHECK_FALSE(guest.ActorHas3D(kRia));
            CHECK_FALSE(guest.IsActorCopy(kRia));
            party.Agree("the games agree: nobody shows Ria");
        }

        AND_WHEN("the host's Ria walks back in")
        {
            party.As(party.Host(), [&] { host.NpcWalks(kRia, kSinkhole, {-20.f, 0.f, 0.f}); });
            party.Until([&] { return guest.IsActorCopy(kRia) && guest.ActorHas3D(kRia); },
                        "the guest's Ria, refused her 3D, is loaded as the host's copy");
            party.Agree("the games agree on Ria");
        }
    }

    // The host's spawn waits for the guest's reference to finish loading (CharacterService's pending spawns); refused
    // its 3D, no TESObjectLoadedEvent comes.
    WHEN("the host's Ria walks in while the guest's game is still loading its own")
    {
        guest.Actors.at(kRia).Cell = kSmoke;
        guest.Actors.at(kRia).Initializing = true;
        party.As(party.Host(), [&] { host.NpcWalks(kRia, kSmoke, kBesideParty); });
        Settle(party);
        REQUIRE_FALSE(guest.IsActorCopy(kRia));
        party.As(party.Guest(), [&] { guest.FinishLoading(kRia); });
        party.Until([&] { return guest.IsActorCopy(kRia) && guest.ActorHas3D(kRia); },
                    "the guest's Ria, done loading without 3D, is shown as the host's copy");
        party.Agree("the games agree on Ria");
    }
}

SCENARIO("A guest's own player and the horse it rides keep their AI", "[copies][guest-own-npc][mounts]")
{
    Party party(1, SmokeWorld);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    uint32_t horse = 0;
    party.As(party.Host(), [&] { horse = host.LoadNpc(kHorse, kSmoke); });
    party.Until([&] { return guest.CopiesOf(kHorse).size() == 1; }, "the guest has the host's horse");
    const uint32_t cHorse = guest.CopiesOf(kHorse)[0];
    party.As(party.Guest(), [&] {
        guest.ActorPutOnMount(FakeEngine::kPlayer, cHorse);
        party.Guest().GetRunner().Trigger(MountEvent{FakeEngine::kPlayer, cHorse});
    });
    party.Until([&] { return !guest.IsActorCopy(cHorse) && host.IsActorCopy(horse); }, "the guest owns the horse it rides");

    bool walked = false;
    party.As(party.Guest(), [&] { walked = guest.NpcWalks(cHorse, kSmoke, kBesideParty); });
    CHECK(walked);
    CHECK(guest.ActorHas3D(cHorse));
    CHECK(guest.ActorHas3D(FakeEngine::kPlayer));
}

// The host's save loads before a guest connects (join-flow.md): its NPCs have 3D as holding starts.
SCENARIO("A guest joins with its own NPC already shown: it is hidden", "[copies][guest-own-npc]")
{
    Party party(1, RiaInTheSinkhole, false);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.As(party.Guest(), [&] { guest.NpcWalks(kRia, kSmoke, kBesideParty); });
    REQUIRE(guest.ActorHas3D(kRia));

    party.Join(party.Guest());
    party.Until([&] { return PlayerCopy(host) != 0; }, "the guest joins");
    Settle(party);
    CHECK_FALSE(guest.ActorHas3D(kRia));
    party.Agree("the games agree: nobody shows Ria");
}

// QueueReference (13057) of a reference whose job is still out only changes its priority (codex, PR 45).
SCENARIO("The host's spawn lands while the guest's refused load is still out", "[copies][guest-own-npc]")
{
    Party party(1, RiaInTheSinkhole);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.As(party.Guest(), [&] {
        guest.LoadsHeld = true;
        guest.Actors.at(kRia).Cell = kSmoke;
        guest.Load3D(kRia);
    });
    party.As(party.Host(), [&] { host.NpcWalks(kRia, kSmoke, kBesideParty); });
    party.Until([&] { return guest.IsActorCopy(kRia); }, "the guest's Ria is the host's copy");
    CHECK(guest.Actors.at(kRia).LoadQueued);
    CHECK_FALSE(guest.ActorHas3D(kRia));

    guest.LoadsHeld = false;
    party.Until([&] { return guest.ActorHas3D(kRia); }, "the guest's Ria is loaded once her refused job ends");
    party.Agree("the games agree on Ria");
}

// A background Load3D (19727) that passed before holding started attaches its model at Finish (13022), after it
// (codex, PR 45, second round).
SCENARIO("A guest's own NPC whose load began before it joined is hidden once it attaches", "[copies][guest-own-npc]")
{
    Party party(1, RiaInTheSinkhole, false);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.As(party.Guest(), [&] {
        guest.LoadsHeld = true;
        guest.NpcWalks(kRia, kSmoke, kBesideParty);
    });
    REQUIRE(guest.Actors.at(kRia).ModelReady);

    party.Join(party.Guest());
    party.Until([&] { return PlayerCopy(host) != 0; }, "the guest joins");
    guest.LoadsHeld = false;
    Settle(party);
    CHECK_FALSE(guest.ActorHas3D(kRia));
    party.Agree("the games agree: nobody shows Ria");
}
