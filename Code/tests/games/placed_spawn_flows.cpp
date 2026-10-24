#include "Party.h"

#include <Components.h>
#include <Services/CharacterService.h>

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
            CHECK(guest.GetActorBaseName(cCopy) == host.GetActorBaseName(cReference));
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
