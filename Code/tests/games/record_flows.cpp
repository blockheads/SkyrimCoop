#include "Party.h"
#include "Fixtures.h"
#include <Events/AiStateChangeEvent.h>
#include <Events/Listen.h>
#include <Messages/Relayed.h>
#include <Services/CopyRecords.h>
#include <Services/CopyRecordsService.h>

// The copy record (docs/design/one-simulator.md, piece 1): a copy takes its owner's last state whenever it appears.
namespace
{
constexpr uint32_t kCell = 0x31D2E;
constexpr uint32_t kGuard = 0xD8C56;
constexpr uint32_t kGuardBase = 0x3DEE0;
constexpr uint32_t kMerchant = 0x1A66B;
constexpr uint32_t kMerchantBase = 0x13BA1;
constexpr uint32_t kPackage = 0x5C84B;

// How many AI state parts a game heard.
struct PartsHeard
{
    explicit PartsHeard(World& aWorld) { Listen<NotifyRecord, &PartsHeard::On>(m_connections, aWorld.GetDispatcher(), this); }
    void On(const NotifyRecord& acMessage) noexcept { Count += acMessage.Part.Kind == RecordKind::kAiState; }
    int Count = 0;

private:
    std::vector<entt::scoped_connection> m_connections;
};

// Both games have the two placed NPCs; aLate's reference is still loading on the guest.
auto TwoNpcs(uint32_t aLate)
{
    return [aLate](FakeEngine& aEngine) {
        SmokeWorld(aEngine);
        aEngine.PlaceNpc(kGuard, kGuardBase, kCell, {});
        aEngine.PlaceNpc(kMerchant, kMerchantBase, kCell, {});
        if (aLate && aEngine.Name != "Dovahkiin")
        {
            aEngine.Actors.at(aLate).Initializing = true;
            aEngine.Actors.at(aLate).Loaded = false;
        }
    };
}
} // namespace

SCENARIO("A part sent before its copy exists is on the copy when it spawns", "[records][copy-records][ai]")
{
    Party party(1, TwoNpcs(kGuard));
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    party.Until([&] { return guest.IsActorCopy(kMerchant); }, "the guest has the other NPC's copy: the spawns are through");
    REQUIRE_FALSE(guest.IsActorCopy(kGuard));

    PartsHeard heard(party.Guest());
    party.As(party.Host(), [&] {
        host.Actors.at(kGuard).Ai.PackageId = kPackage;
        party.Host().GetDispatcher().trigger(AiStateChangeEvent{kGuard});
    });
    party.Until([&] { return heard.Count > 0; }, "the guest hears the guard's AI state while its reference loads");
    party.As(party.Guest(), [&] { guest.FinishLoading(kGuard); });
    party.Until([&] { return guest.IsActorCopy(kGuard); }, "the reference becomes the copy");
    party.Until([&] { return guest.GetActorAiFields(kGuard).PackageId == kPackage; }, "the copy holds its owner's package");
}

SCENARIO("A part naming an actor that spawns later", "[records][copy-records][ai]")
{
    Party party(1, TwoNpcs(kMerchant));
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    party.Until([&] { return guest.IsActorCopy(kGuard); }, "the guest has the guard's copy");
    REQUIRE_FALSE(guest.IsActorCopy(kMerchant));

    PartsHeard heard(party.Guest());
    party.As(party.Host(), [&] { host.LooksAt(kGuard, FakeEngine::kDialogueHeadTrack, kMerchant); });
    party.Until([&] { return heard.Count > 0; }, "the guest hears the guard looks at an actor it does not have yet");
    CHECK(guest.GetActorAiFields(kGuard).HeadTrackIds[FakeEngine::kDialogueHeadTrack] == 0);
    party.As(party.Guest(), [&] { guest.FinishLoading(kMerchant); });
    party.Until([&] { return guest.IsActorCopy(kMerchant); }, "the merchant's copy appears");
    party.Until([&] { return guest.GetActorAiFields(kGuard).HeadTrackIds[FakeEngine::kDialogueHeadTrack] == kMerchant; },
                "the guard's copy looks at the merchant's copy");
    party.Agree("the games agree on both");
}

SCENARIO("A part a game asks for comes again from its owner, under a new version", "[records][copy-records][ai]")
{
    Party party(1, TwoNpcs(0));
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    party.Until([&] { return guest.IsActorCopy(kGuard); }, "the guest has the guard's copy");
    party.As(party.Host(), [&] {
        host.Actors.at(kGuard).Ai.PackageId = kPackage;
        party.Host().GetDispatcher().trigger(AiStateChangeEvent{kGuard});
    });
    party.Until([&] { return guest.GetActorAiFields(kGuard).PackageId == kPackage; }, "the copy holds its owner's package");

    uint32_t id = 0;
    for (const auto cEntity : party.Guest().view<RemoteComponent, FormIdComponent>())
        if (party.Guest().get<FormIdComponent>(cEntity).Id == kGuard)
            id = party.Guest().get<RemoteComponent>(cEntity).Id;
    REQUIRE(id);
    const auto& cRecords = party.Guest().ctx().at<CopyRecords>();
    const uint32_t cVersion = cRecords.Find(id, RecordKind::kAiState)->Version;
    // The copy drifted (here: by hand); the guest asks.
    party.As(party.Guest(), [&] {
        guest.SetCopyAiFields(kGuard, {});
        party.Guest().ctx().at<CopyRecordsService>().Ask(id, RecordKind::kAiState);
    });
    party.Until([&] { return cRecords.Find(id, RecordKind::kAiState)->Version > cVersion; }, "the owner's part comes again");
    CHECK(guest.GetActorAiFields(kGuard).PackageId == kPackage);
}
