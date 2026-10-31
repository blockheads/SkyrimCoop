#include "Party.h"

#include <Components.h>
#include <Services/CharacterService.h>
#include <Messages/NotifyRemoveCharacter.h>

namespace
{
constexpr uint32_t kCell = 0x31D2E;
constexpr uint32_t kRabbitRef = 0x105A25;
constexpr uint32_t kFoxRef = 0x105A35;
constexpr uint32_t kRabbitBase = 0x6DC9A;
constexpr uint32_t kFoxBase = 0x829B3;

uint32_t CopyReference(World& aGame, uint32_t aSessionId)
{
    for (const auto cEntity : aGame.view<RemoteComponent, FormIdComponent>())
        if (aGame.get<RemoteComponent>(cEntity).Id == aSessionId)
        {
            CHECK(aGame.get<RemoteComponent>(cEntity).CachedRefId == aGame.get<FormIdComponent>(cEntity).Id);
            return aGame.get<FormIdComponent>(cEntity).Id;
        }
    return 0;
}

uint32_t SessionId(World& aGame, uint32_t aReference)
{
    for (const auto cEntity : aGame.view<LocalComponent, FormIdComponent>())
        if (aGame.get<FormIdComponent>(cEntity).Id == aReference)
            return aGame.get<LocalComponent>(cEntity).Id;
    return 0;
}
} // namespace

SCENARIO("Two placed wildlife copies take their owner's bases despite different local rolls",
         "[copies][placed-identity]")
{
    const bool cDifferentRolls = GENERATE(false, true);
    const bool cLoaded = GENERATE(false, true);
    CAPTURE(cDifferentRolls, cLoaded);
    Party party(1, [=](FakeEngine& aEngine) {
        aEngine.Actors.at(FakeEngine::kPlayer).Cell = kCell;
        aEngine.LoadCell(kCell);
        aEngine.Npcs[kRabbitBase] = {"Rabbit", 449689};
        aEngine.Npcs[kFoxBase] = {"Fox", 1088636};
        const bool cSwap = cDifferentRolls && aEngine.Name != "Dovahkiin";
        aEngine.PlaceNpc(kRabbitRef, cSwap ? kFoxBase : kRabbitBase, kCell, {100.f, 0.f, 0.f});
        aEngine.PlaceNpc(kFoxRef, cSwap ? kRabbitBase : kFoxBase, kCell, {200.f, 0.f, 0.f});
        if (aEngine.Name != "Dovahkiin")
        {
            aEngine.Actors.at(kRabbitRef).Loaded = cLoaded;
            aEngine.Actors.at(kFoxRef).Loaded = cLoaded;
        }
    });
    const auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    party.Until([&] { return guest.IsActorCopy(kRabbitRef) && guest.IsActorCopy(kFoxRef); },
                "both placed references become copies");

    const auto cCheckCopies = [&] {
        for (const uint32_t cReference : {kRabbitRef, kFoxRef})
        {
            CAPTURE(cReference);
            const uint32_t cSessionId = SessionId(party.Host(), cReference);
            REQUIRE(cSessionId != 0);
            const uint32_t cCopy = CopyReference(party.Guest(), cSessionId);
            REQUIRE(cCopy == cReference);
            CHECK(guest.GetActorBaseFormId(cCopy) == host.GetActorBaseFormId(cReference));
            CHECK(guest.GetActorDisplayName(cCopy) == host.GetActorDisplayName(cReference));
            CHECK(guest.GetActorRaceFormId(cCopy) == host.GetActorRaceFormId(cReference));
        }
    };
    cCheckCopies();

    WHEN("the placed references have different local bases again before refreshed spawns arrive in reverse order")
    {
        guest.Actors.at(kRabbitRef).Base = kFoxBase;
        guest.Actors.at(kFoxRef).Base = kRabbitBase;
        std::vector<NotifyCharacterSpawn> spawns;
        party.As(party.Host(), [&] { spawns = party.Host().GetCharacterService().BuildSpawnRequests(0); });
        std::reverse(spawns.begin(), spawns.end());
        const size_t cActors = guest.Actors.size();
        party.As(party.Guest(), [&] {
            for (const auto& cSpawn : spawns)
                if (cSpawn.FormId == GameId{0, kRabbitRef} || cSpawn.FormId == GameId{0, kFoxRef})
                    party.Guest().GetCharacterService().OnCharacterSpawn(cSpawn);
        });
        CHECK(guest.Actors.size() == cActors);
        cCheckCopies();
    }

    party.Agree("each placed reference copies its owner");
}

SCENARIO("A placed spawn waits for native initialization before applying the owner's state", "[copies][spawn-loading]")
{
    Party party(1, [](FakeEngine& aEngine) {
        aEngine.Actors.at(FakeEngine::kPlayer).Cell = kCell;
        aEngine.LoadCell(kCell);
        aEngine.Npcs[kRabbitBase] = {"Rabbit", 449689};
        aEngine.Npcs[kFoxBase] = {"Fox", 1088636};
        aEngine.PlaceNpc(kRabbitRef, aEngine.Name == "Dovahkiin" ? kRabbitBase : kFoxBase, kCell, {100.f, 0.f, 0.f});
        if (aEngine.Name != "Dovahkiin")
        {
            aEngine.Actors.at(kRabbitRef).Initializing = true;
            aEngine.Actors.at(kRabbitRef).Loaded = false;
        }
    });
    auto& guest = Party::Engine(party.Guest());
    party.Until([&] { return !guest.CopiesOf().empty() && !Party::Engine(party.Host()).CopiesOf().empty(); },
                "both players finish joining while the placed actor initializes");
    const uint32_t cSession = SessionId(party.Host(), kRabbitRef);
    REQUIRE(cSession);
    std::vector<NotifyCharacterSpawn> spawns;
    party.As(party.Host(), [&] { spawns = party.Host().GetCharacterService().BuildSpawnRequests(0); });
    const auto it = std::find_if(spawns.begin(), spawns.end(), [](const auto& acSpawn) { return acSpawn.FormId == GameId{0, kRabbitRef}; });
    REQUIRE(it != spawns.end());
    auto spawn = *it;
    spawn.Position = {300.f, 0.f, 0.f};
    party.As(party.Guest(), [&] { party.Guest().GetCharacterService().OnCharacterSpawn(spawn); });
    CHECK_FALSE(guest.IsActorCopy(kRabbitRef));
    CHECK(guest.GetActorBaseFormId(kRabbitRef) == kFoxBase);
    CHECK(guest.Actors.at(kRabbitRef).Position[0] == 100.f);
    CHECK(CopyReference(party.Guest(), cSession) == 0);

    SECTION("the latest spawn is applied when initialization ends")
    {
        spawn.Position = {400.f, 0.f, 0.f};
        Party::Engine(party.Host()).Actors.at(kRabbitRef).Position[0] = 400.f;
        party.As(party.Guest(), [&] { party.Guest().GetCharacterService().OnCharacterSpawn(spawn); });
        party.As(party.Guest(), [&] { guest.FinishLoading(kRabbitRef); });
        party.Until([&] { return CopyReference(party.Guest(), cSession) == kRabbitRef; }, "the initialized actor becomes its owner's copy");
        CHECK(guest.GetActorBaseFormId(kRabbitRef) == kRabbitBase);
        CHECK(guest.Actors.at(kRabbitRef).Position[0] == 400.f);
    }
    SECTION("a refresh waits too, then applies once")
    {
        party.As(party.Guest(), [&] { guest.FinishLoading(kRabbitRef); });
        party.Until([&] { return guest.IsActorCopy(kRabbitRef); }, "the first spawn completes");
        guest.Actors.at(kRabbitRef).Initializing = true;
        guest.Actors.at(kRabbitRef).Loaded = false;
        spawn.BaseId = {0, kFoxBase};
        Party::Engine(party.Host()).Actors.at(kRabbitRef).Base = kFoxBase;
        party.As(party.Guest(), [&] { party.Guest().GetCharacterService().OnCharacterSpawn(spawn); });
        CHECK(guest.GetActorBaseFormId(kRabbitRef) == kRabbitBase);
        party.As(party.Guest(), [&] { guest.FinishLoading(kRabbitRef); });
        party.Until([&] { return guest.GetActorBaseFormId(kRabbitRef) == kFoxBase; }, "the initialized copy takes its refreshed base");
    }
    SECTION("leaving cancels a pending spawn")
    {
        party.Leave(party.Guest());
        party.As(party.Guest(), [&] { guest.FinishLoading(kRabbitRef); });
        for (int i = 0; i < 3; ++i)
            party.Frame();
        CHECK_FALSE(guest.IsActorCopy(kRabbitRef));
        CHECK(CopyReference(party.Guest(), cSession) == 0);
    }
    SECTION("removing a pending spawn cancels it")
    {
        party.As(party.Guest(), [&] { NotifyRemoveCharacter remove; remove.SessionId = cSession; party.Guest().GetCharacterService().OnRemoveCharacter(remove); });
        party.As(party.Guest(), [&] { guest.FinishLoading(kRabbitRef); });
        for (int i = 0; i < 3; ++i)
            party.Frame();
        CHECK_FALSE(guest.IsActorCopy(kRabbitRef));
        CHECK(CopyReference(party.Guest(), cSession) == 0);
    }
}
