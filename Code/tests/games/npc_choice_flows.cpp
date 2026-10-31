#include "Party.h"
#include "Fixtures.h"

#include <Services/CharacterService.h>
#include <Games/EngineNames.h>

TEST_CASE("A copied NPC name retains the first pooled spelling in each game", "[copies][npc-name-pool]")
{
    constexpr uint32_t cFace = 0x3DEE0, cResolved = 0xFF000F00;
    Party party(1, [=](FakeEngine& engine) {
        SmokeWorld(engine);
        engine.Npcs[cFace].Name = "Bandit Outlaw";
        engine.Npcs[cResolved] = engine.Npcs[cFace];
        engine.Npcs[cResolved].Name = "Bandit Chief";
        engine.Npcs[cResolved].FaceBase = cFace;
    });
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    guest.PooledNames["bandit chief"] = "bandit chief";
    party.As(party.Host(), [&] { host.LoadNpc(cResolved, 0x31D2E, {}); });
    party.Until([&] { return guest.CopiesOf(cFace).size() == 1; }, "the resolved NPC copy appears");
    const uint32_t cCopy = guest.CopiesOf(cFace).front();
    CHECK(guest.GetActorDisplayName(cCopy) == "bandit chief");
    party.Agree("the same pooled NPC name identifies the same choice");
}

TEST_CASE("A leveled NPC copy keeps the resolved name beside its chosen face", "[copies][npc-choice]")
{
    constexpr uint32_t cFace = 0x3DEE0;
    constexpr uint32_t cResolved = 0xFF000F00;
    constexpr uint32_t cPlaced = 0xD8C56;
    const bool cPersistent = GENERATE(false, true);
    Party party(1, [&](FakeEngine& aEngine) {
        SmokeWorld(aEngine);
        aEngine.Npcs[cFace].Name = "Bandit Outlaw";
        aEngine.Npcs[cFace].Race = 0x13746;
        aEngine.Npcs[cResolved] = aEngine.Npcs[cFace];
        aEngine.Npcs[cResolved].Name = "Bandit Chief";
        aEngine.Npcs[cResolved].FaceBase = cFace;
        if (cPersistent)
            aEngine.PlaceNpc(cPlaced, cFace, 0x31D2E, {});
    });
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    uint32_t npc = cPlaced;
    party.As(party.Host(), [&] {
        if (cPersistent)
            host.Actors.at(npc).Base = cResolved;
        else
            npc = host.LoadNpc(cResolved, 0x31D2E, {});
    });
    uint32_t sid = 0;
    party.Until([&] {
        for (auto&& [entity, form, local] : party.Host().view<FormIdComponent, LocalComponent>().each())
            if (form.Id == npc)
                sid = local.Id;
        return sid != 0;
    }, "the host owns the resolved NPC");
    uint32_t copy = 0;
    party.Until([&] {
        for (auto&& [entity, form, remote] : party.Guest().view<FormIdComponent, RemoteComponent>().each())
            if (remote.Id == sid)
                copy = form.Id;
        return copy && guest.ActorHas3D(copy);
    }, "the guest shows the resolved NPC's copy");
    CHECK(host.GetActorDisplayName(npc) == "Bandit Chief");
    CHECK(guest.GetActorDisplayName(copy) == host.GetActorDisplayName(npc));
    CHECK(guest.GetActorRaceFormId(copy) == host.GetActorRaceFormId(npc));
    CHECK(guest.Npcs.at(cFace).Name == "Bandit Outlaw");
    CHECK(host.Npcs.at(cFace).Name == "Bandit Outlaw");
    if (cPersistent)
        CHECK(copy == npc);

    const size_t cActors = guest.Actors.size();
    std::vector<NotifyCharacterSpawn> spawns;
    party.As(party.Host(), [&] { spawns = party.Host().GetCharacterService().BuildSpawnRequests(0); });
    party.As(party.Guest(), [&] {
        for (const auto& spawn : spawns)
            if (spawn.SessionId == sid)
                party.Guest().GetCharacterService().OnCharacterSpawn(spawn);
    });
    CHECK(guest.Actors.size() == cActors);
    CHECK(guest.GetActorDisplayName(copy) == "Bandit Chief");
    CHECK(guest.Npcs.at(cFace).Name == "Bandit Outlaw");
    // ExtraLeveledCreature's load can destroy a temporary NPC before the actor's GC runs.
    const uint32_t cCopyBase = guest.GetActorBaseFormId(copy);
    if (cCopyBase >= 0xFF000000)
        guest.Npcs.erase(cCopyBase);
    CHECK(guest.Npcs.contains(guest.GetActorBaseFormId(copy)));
    CHECK(guest.GetActorDisplayName(copy) == "Bandit Chief");
    if (cPersistent)
    {
        party.Leave(party.Guest());
        CHECK(guest.ActorExists(copy));
        CHECK_FALSE(guest.IsActorDeleted(copy));
        CHECK_FALSE(guest.IsActorCopy(copy));
    }
}

TEST_CASE("An ordinary named NPC retains its dialogue base despite saved runtime attributes", "[copies][npc-choice]")
{
    constexpr uint32_t cOnmund = 0x1C194;
    Party party(1, [&](FakeEngine& aEngine) {
        SmokeWorld(aEngine);
        aEngine.Npcs[cOnmund].Name = "Onmund";
        aEngine.Npcs[cOnmund].ChangeFlags = 0x20A;
    });
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    party.As(party.Host(), [&] { host.LoadNpc(cOnmund, 0x31D2E, {}); });
    party.Until([&] { return guest.CopiesOf(0).size() > 1; }, "the named NPC reaches the guest");
    uint32_t copy = 0;
    for (const auto& [cId, cActor] : guest.Actors)
        if (cActor.Copy && guest.GetActorDisplayName(cId) == "Onmund")
            copy = cId;
    REQUIRE(copy);
    CHECK(guest.GetActorBaseFormId(copy) == cOnmund);
    CHECK(guest.GetActorDisplayName(copy) == "Onmund");
}
