// Kill moves as action claims (docs/design/one-simulator.md, piece 4b; docs/re/systems/kill-moves.md): the host's
// first claim holds, an NPC partner is its claimer's game's for the pair, and its death is the host's.
#include "Party.h"
#include "Fixtures.h"

#include <Components/FormIdComponent.h>
#include <Components/HeldCopyComponent.h>
#include <SessionIds.h>

namespace
{
constexpr uint32_t kSmoke = 0x00031D2E;
constexpr uint32_t kGuard = 0x0001A67C;

ActionEvent KillMoveOn(uint32_t aTarget)
{
    ActionEvent kill;
    kill.ActionId = 0x13005;
    kill.EventName = "pa_KillMoveI";
    kill.TargetEventName = "KillMoveI";
    kill.TargetId = aTarget;
    return kill;
}

// A host guard and a guest's copy of it.
struct GuardPair
{
    Party party;
    FakeEngine& host;
    FakeEngine& guest;
    uint32_t guard = 0;
    uint32_t copy = 0;

    explicit GuardPair(size_t aGuests = 1)
        : party(aGuests, SmokeWorld)
        , host(Party::Engine(party.Host()))
        , guest(Party::Engine(party.Guest()))
    {
        party.As(party.Host(), [&] { guard = host.LoadNpc(kGuard, kSmoke, {30.f, 40.f, 0.f}); });
        party.Until([&] { return guest.CopiesOf(kGuard).size() == 1 && PlayerCopy(host) != 0; }, "each game has the other's actors");
        copy = guest.CopiesOf(kGuard)[0];
    }

    void GuestSwings()
    {
        party.As(party.Guest(), [&] { REQUIRE(guest.PerformAnimation(FakeEngine::kPlayer, KillMoveOn(copy))); });
    }
    void Lent()
    {
        party.Until([&] { return !guest.Actors.at(copy).Copy && host.Actors.at(guard).Copy; },
                    "the guard is the guest's game's for the pair, a copy on the host");
    }
};
} // namespace

SCENARIO("A guest's kill move on a host NPC runs on the guest's game alone, and the host kills it for the guest's player",
         "[combat][kill-move][pair-claim]")
{
    // Skyrim (kill-moves.md): KillActorHandler (42838) credits the pair's partner, and a held kill is only counted
    // (37896). Before, each game ran its half of the pair, and the host's own instance decided the death.
    GuardPair pair;
    pair.GuestSwings();
    pair.Lent();
    CHECK(pair.guest.Actors.at(pair.copy).InKillMove);
    pair.party.Until([&] { return pair.host.Actors.at(pair.guard).InKillMove; }, "the host's copy of the guard is in the kill move, by its new owner's record");
    CHECK(pair.host.CollapsedPairs + pair.guest.CollapsedPairs == 0);
    // The host's copy of the guard has no stream to replay (its new owner gives no point in the instance, its half was
    // never sent): the host's game plays the pair itself, both copies in its instance.
    pair.party.Until([&] { return pair.host.Actors.at(pair.guard).InPair && pair.host.Actors.at(PlayerCopy(pair.host)).InPair; },
                     "the host's game plays the pair between its two copies");

    // The clip's KillActor on the guest's game: counted there, the guard holds its pose, nobody dies yet.
    pair.party.As(pair.party.Guest(), [&] {
        pair.guest.ClipKillActor(pair.copy);
        CHECK_FALSE(pair.guest.IsActorDead(pair.copy));
        CHECK(pair.guest.PoseHeld(pair.copy));
    });
    // The host's game waits for its own copy's clip to reach the blow, so the death comes with it there too.
    int frames = 0;
    pair.party.Until([&] { return ++frames > 30; }, "the done reaches the host");
    CHECK_FALSE(pair.host.IsActorDead(pair.guard));
    CHECK(pair.host.Actors.at(pair.guard).Copy);
    pair.party.As(pair.party.Host(), [&] { pair.host.ClipKillActor(pair.guard); });
    pair.party.Until([&] { return pair.host.IsActorDead(pair.guard); }, "the host's game kills its guard");
    CHECK_FALSE(pair.host.Actors.at(pair.guard).Copy);
    CHECK(pair.host.Actors.at(pair.guard).KilledBy == PlayerCopy(pair.host));
    pair.party.Until([&] { return pair.guest.Actors.at(pair.copy).Copy && pair.guest.IsActorDead(pair.copy); },
                     "the guest's guard is a copy again and dies with its owner");
}

SCENARIO("An NPC the host takes back from a pair that did not kill it can still die", "[combat][kill-move][pair-claim]")
{
    // Skyrim (kill-moves.md, read): a copy's scripted deferred kill (UpdateCopyAI) is ended by no engine path
    // (38618's callers all pass scripted 0), so it outlives the copy: taken back, every kill was only counted.
    GuardPair pair;
    pair.GuestSwings();
    pair.Lent();
    pair.party.As(pair.party.Guest(), [&] {
        for (const uint32_t cHalf : {FakeEngine::kPlayer, pair.copy})
        {
            pair.guest.EndKillMove(cHalf);
            pair.guest.StopPairClip(cHalf);
        }
    });
    pair.party.Until([&] { return !pair.host.Actors.at(pair.guard).Copy && pair.guest.Actors.at(pair.copy).Copy; },
                     "the pair over, the guard is the host's again");
    CHECK_FALSE(pair.host.IsActorDead(pair.guard));
    pair.party.As(pair.party.Host(), [&] { pair.host.Hit(pair.guard, 1000.f); });
    CHECK(pair.host.IsActorDead(pair.guard));
}

SCENARIO("Two guests' kill moves on one NPC: the host's first claim plays, the other ends with no blow", "[combat][kill-move][pair-claim]")
{
    GuardPair pair(2);
    FakeEngine& other = Party::Engine(pair.party.Guest(1));
    pair.party.Until([&] { return other.CopiesOf(kGuard).size() == 1; }, "the second guest has the guard");
    const uint32_t cOtherCopy = other.CopiesOf(kGuard)[0];
    pair.GuestSwings();
    pair.party.As(pair.party.Guest(1), [&] { REQUIRE(other.PerformAnimation(FakeEngine::kPlayer, KillMoveOn(cOtherCopy))); });
    pair.Lent();
    pair.party.Until([&] { return !other.Actors.at(FakeEngine::kPlayer).InKillMove; }, "the refused guest's pair ends");
    CHECK(other.Actors.at(cOtherCopy).Copy);
    CHECK(other.PlainSwings == 0);
    // Its clip's blow, were it still to come, is dropped.
    const float cHealth = pair.guest.GetActorValue(pair.copy, FakeEngine::kHealth);
    pair.party.As(pair.party.Guest(1), [&] { other.Strikes(FakeEngine::kPlayer, cOtherCopy, 30.f); });
    int frames = 0;
    pair.party.Until([&] { return ++frames > 60; }, "a moment passes");
    CHECK(pair.guest.GetActorValue(pair.copy, FakeEngine::kHealth) == cHealth);
    CHECK(pair.guest.Actors.at(FakeEngine::kPlayer).InKillMove); // the first pair plays on
}

SCENARIO("A refused kill move between two copies is not entered on the game watching it", "[combat][kill-move][pair-claim]")
{
    // Two copies on their streams enter a pair at its action (39004's EnterKillMove on each, AnimationSystem): a
    // refused one would leave the watching game's NPC in a kill move with the loser as its killer.
    GuardPair pair(2);
    FakeEngine& other = Party::Engine(pair.party.Guest(1));
    pair.party.Until([&] { return other.CopiesOf(kGuard).size() == 1; }, "the second guest has the guard");
    const uint32_t cOtherCopy = other.CopiesOf(kGuard)[0];
    for (FakeEngine* pEngine : {&pair.host, &pair.guest, &other})
        pEngine->Graph = FakeGraphRules{};
    pair.party.As(pair.party.Guest(), [&] { pair.guest.Actors.at(FakeEngine::kPlayer).GraphState = "Standing"; });
    pair.party.As(pair.party.Guest(1), [&] { other.Actors.at(FakeEngine::kPlayer).GraphState = "Standing"; });
    pair.party.Until([&] { return PlayerCopy(pair.host, 1) != 0; }, "the host's game has both guests' players");
    const uint32_t cCopies[] = {PlayerCopy(pair.host), PlayerCopy(pair.host, 1)};
    for (const uint32_t cCopy : cCopies)
    {
        pair.host.Actors.at(cCopy).GraphState = "Standing";
        pair.party.Until([&] { return pair.party.Host().GetGraphStreams().OnStream(cCopy); }, "the host's copy is on its owner's stream");
    }

    // The guard goes to the first guest's game; the host's game holds two copies for the refused pair.
    pair.GuestSwings();
    pair.Lent();
    pair.party.Until([&] { return std::ranges::any_of(cCopies, [&](uint32_t aCopy) { return pair.host.Actors.at(aCopy).InPair; }); },
                     "the host's game plays the first guest's pair");
    const uint32_t otherOnHost = pair.host.Actors.at(cCopies[0]).InPair ? cCopies[1] : cCopies[0];
    pair.party.As(pair.party.Guest(1), [&] { REQUIRE(other.PerformAnimation(FakeEngine::kPlayer, KillMoveOn(cOtherCopy))); });
    pair.party.Until([&] { return pair.host.GetActorLatestAnimation(otherOnHost).EventName == "pa_KillMoveI"; }, "the host's copy reached the refused kill move");
    CHECK_FALSE(pair.host.Actors.at(otherOnHost).InKillMove);
    CHECK(pair.host.Actors.at(pair.guard).Killer != otherOnHost);
}

SCENARIO("A guest's kill move on a host NPC with its AI off is refused: the guest ends its pair and swings plainly",
         "[combat][kill-move][pair-claim]")
{
    // Skyrim (duo, frozen-wolf-kill-move, 2026-11-18): a wolf whose graph does not run (kProcessMe clear) never ends a
    // pair; held in the kill move, it refused every later hit. The engine's own fallback for a refused pair is the
    // same action with no target (PlayerControls 42350, read).
    GuardPair pair;
    pair.party.As(pair.party.Host(), [&] { pair.host.Actors.at(pair.guard).ProcessMe = false; });
    pair.GuestSwings();
    pair.party.Until([&] { return pair.guest.PlainSwings == 1; }, "the guest's player swings plainly");
    CHECK_FALSE(pair.guest.Actors.at(FakeEngine::kPlayer).InKillMove);
    CHECK(pair.guest.Actors.at(pair.copy).Copy);
    CHECK_FALSE(pair.host.Actors.at(pair.guard).Copy);
    pair.party.Until([&] { return pair.host.GetActorLatestAnimation(PlayerCopy(pair.host)).EventName == FakeEngine::kPlainSwing; },
                     "the host's copy of the guest swings too");
    CHECK_FALSE(pair.host.Actors.at(pair.guard).InKillMove);

    // The plain swing's blow lands as any hit: the host's game applies it.
    pair.party.As(pair.party.Guest(), [&] { pair.guest.Strikes(FakeEngine::kPlayer, pair.copy, pair.host.GetActorValue(pair.guard, FakeEngine::kHealth)); });
    pair.party.Until([&] { return pair.host.IsActorDead(pair.guard); }, "the host's NPC dies of the guest's blow");
    CHECK(pair.host.Actors.at(pair.guard).KilledBy == PlayerCopy(pair.host));
}

SCENARIO("A guest leaving mid kill move gives the NPC back to the host alive", "[combat][kill-move][pair-claim]")
{
    GuardPair pair;
    pair.GuestSwings();
    pair.Lent();
    // The host's copy already counted its own clip's kill: the hand-back's word (alive) holds, nothing queued (40036).
    pair.party.Until([&] { return pair.host.Actors.at(pair.guard).InPair; }, "the host's game plays the pair");
    pair.party.As(pair.party.Host(), [&] { pair.host.ClipKillActor(pair.guard); });
    pair.party.Leave(pair.party.Guest());
    pair.party.Until([&] { return !pair.host.Actors.at(pair.guard).Copy; }, "the guard is the host's again");
    int frames = 0;
    pair.party.Until([&] { return ++frames > 30; }, "a moment passes");
    CHECK_FALSE(pair.host.IsActorDead(pair.guard));
}

SCENARIO("A guest's kill move whose clip kills at its end, with no KillActor, still kills the host's NPC", "[combat][kill-move][pair-claim]")
{
    // Skyrim (38614, read by the PR #85 review): KillMoveEnd QueueKills the victim by its recorded killer; under the
    // lent NPC's hold that kill is only counted (DeferKill 38617), with no KillActor event (42838).
    GuardPair pair;
    pair.GuestSwings();
    pair.Lent();
    pair.party.As(pair.party.Guest(), [&] {
        pair.guest.Actors.at(pair.copy).KillCounted = true;
        for (const uint32_t cHalf : {FakeEngine::kPlayer, pair.copy})
        {
            pair.guest.EndKillMove(cHalf);
            pair.guest.StopPairClip(cHalf);
        }
    });
    pair.party.Until([&] { return pair.host.IsActorDead(pair.guard); }, "the host's game kills its guard");
    CHECK(pair.host.Actors.at(pair.guard).KilledBy == PlayerCopy(pair.host));
}

SCENARIO("A kill move on the host player's summon is refused: no game would simulate it", "[combat][kill-move][pair-claim]")
{
    // A summon's copy is commanded by a player, and no game takes one over (CharacterService::TakeOwnership).
    GuardPair pair;
    SECTION("the host refuses it")
    {
        pair.party.As(pair.party.Host(), [&] { pair.host.Actors.at(pair.guard).Commander = FakeEngine::kPlayer; });
        pair.guest.Actors.at(pair.copy).Commander = FakeEngine::kPlayer;
        pair.GuestSwings();
        pair.party.Until([&] { return pair.guest.PlainSwings == 1; }, "the guest's player swings plainly");
    }
    SECTION("granted, the guest cannot take it: the pair ends and the NPC goes back")
    {
        pair.guest.Actors.at(pair.copy).Commander = FakeEngine::kPlayer;
        pair.GuestSwings();
        int frames = 0;
        pair.party.Until([&] { return ++frames > 30; }, "the claim's round trip");
        CHECK_FALSE(pair.guest.Actors.at(FakeEngine::kPlayer).InKillMove);
    }
    CHECK(pair.guest.Actors.at(pair.copy).Copy);
    CHECK_FALSE(pair.host.Actors.at(pair.guard).Copy);
}

SCENARIO("An NPC given back from a pair is placed exactly where the claimer's game left it", "[combat][kill-move][pair-claim][claims]")
{
    // A give-back carries its holder's placement, a horse's and a kill move's partner's alike (ActorGiveBack); the
    // host's game takes it there exactly (CharacterService::PlaceAt), a small move and a turn too (FollowOwner skips
    // those: under 64 units, same cell).
    GuardPair pair;
    pair.GuestSwings();
    pair.Lent();
    float there[3]{};
    pair.party.As(pair.party.Guest(), [&] {
        auto& guard = pair.guest.Actors.at(pair.copy);
        std::ranges::copy(guard.Position, there);
        there[0] += 30.f;
        std::ranges::copy(there, guard.Position);
        guard.Rotation[2] = 1.25f;
        for (const uint32_t cHalf : {FakeEngine::kPlayer, pair.copy})
        {
            pair.guest.EndKillMove(cHalf);
            pair.guest.StopPairClip(cHalf);
        }
    });
    pair.party.Until([&] { return !pair.host.Actors.at(pair.guard).Copy; }, "the guard is the host's again");
    // Ours again: not a copy its owner left (that mark drops an actor from the next session at a disconnect).
    for (const auto cEntity : pair.party.Host().view<FormIdComponent>())
        if (pair.party.Host().get<FormIdComponent>(cEntity).Id == pair.guard)
            CHECK_FALSE(pair.party.Host().all_of<HeldCopyComponent>(cEntity));
    CHECK(pair.host.Actors.at(pair.guard).Position[0] == Approx(there[0]));
    CHECK(pair.host.Actors.at(pair.guard).Position[1] == Approx(there[1]));
    CHECK(pair.host.Actors.at(pair.guard).Rotation[2] == Approx(1.25f));
}

SCENARIO("A kill counted while the NPC's give-back waits for its last update still kills it", "[combat][kill-move][pair-claim][claims]")
{
    // Skyrim (38614, read): KillMoveEnd queues its kill on the task queue (37316) as it clears the kill move flag. When
    // that task runs against our give-back is not read: this flow models the kill counted a frame after the pair ends.
    GuardPair pair;
    pair.GuestSwings();
    pair.Lent();
    pair.party.As(pair.party.Guest(), [&] {
        for (const uint32_t cHalf : {FakeEngine::kPlayer, pair.copy})
        {
            pair.guest.EndKillMove(cHalf);
            pair.guest.StopPairClip(cHalf);
        }
    });
    pair.party.Frame();
    pair.guest.Actors.at(pair.copy).KillCounted = true; // the task queue's KillImpl, under the hold
    pair.party.Until([&] { return pair.host.IsActorDead(pair.guard); }, "the host's game kills its guard");
    CHECK(pair.host.Actors.at(pair.guard).KilledBy == PlayerCopy(pair.host));
}

SCENARIO("A kill move's NPC the claimer's game lost before the host's answer goes straight back", "[combat][kill-move][pair-claim][claims]")
{
    // The guest's copy went (a cell change) between its claim and the grant: nobody would simulate the NPC.
    GuardPair pair;
    pair.GuestSwings();
    pair.party.As(pair.party.Guest(), [&] {
        for (const auto cEntity : pair.party.Guest().view<FormIdComponent>())
            if (pair.party.Guest().get<FormIdComponent>(cEntity).Id == pair.copy)
                pair.party.Guest().destroy(cEntity);
    });
    int frames = 0;
    pair.party.Until([&] { return ++frames > 90; }, "the claim's round trip and the take-back");
    CHECK_FALSE(pair.host.Actors.at(pair.guard).Copy);
}
