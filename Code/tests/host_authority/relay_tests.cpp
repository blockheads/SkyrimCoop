// HostAuthority's relays: who receives each request's notification.
#include "session.h"
#include <Structs/ItemsPart.h>
#include <Messages/NotifyInventoryChanges.h>
#include <Messages/RequestInventoryChanges.h>

TEST_CASE("Active effects: relayed from the owner only, to everyone else", "[HostAuthority][Magic]")
{
    Session session;
    const uint32_t cHostNpc = session.Character(kHost);

    RequestActiveEffects effects;
    effects.SessionId = cHostNpc;
    effects.EffectIds = {GameId(0, 0x12F03)};
    session.Receive(kPeer, effects);
    CHECK(session.SentTo<NotifyActiveEffects>(kOtherPeer).empty());

    session.Receive(kHost, effects);
    REQUIRE(session.SentTo<NotifyActiveEffects>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyActiveEffects>(kPeer)[0]->EffectIds == effects.EffectIds);
    CHECK(session.SentTo<NotifyActiveEffects>(kOtherPeer).size() == 1);
    CHECK(session.SentTo<NotifyActiveEffects>(kHost).empty());
}

TEST_CASE("Effect flashes: relayed from the owner only, to everyone else", "[HostAuthority][Magic]")
{
    Session session;
    EffectFlashRequest flash;
    flash.SessionId = session.Character(kHost);
    flash.EffectId = GameId(0, 0x12F03);
    session.Receive(kPeer, flash);
    CHECK(session.SentTo<NotifyEffectFlash>(kOtherPeer).empty());

    session.Receive(kHost, flash);
    REQUIRE(session.SentTo<NotifyEffectFlash>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyEffectFlash>(kPeer)[0]->EffectId == flash.EffectId);
    CHECK(session.SentTo<NotifyEffectFlash>(kOtherPeer).size() == 1);
    CHECK(session.SentTo<NotifyEffectFlash>(kHost).empty());
}

TEST_CASE("Cutscenes: a game's party call reaches every other game, named as that game's player's", "[HostAuthority][Scripts]")
{
    Session session;
    RoutedCallRequest call;
    call.To = CallTo::kOthers;
    call.Call.Class = "ImageSpaceModifier";
    call.Call.Function = "Apply";
    call.Call.Self = ScriptForm{.Form = GameId(0, 0xF756D)};
    call.Call.Arguments = {1.f};

    session.Receive(kPeer, call);
    for (const ConnectionId_t cOther : {kHost, kOtherPeer})
    {
        REQUIRE(session.SentTo<NotifyRoutedCall>(cOther).size() == 1);
        CHECK(*session.SentTo<NotifyRoutedCall>(cOther)[0] == [&] {
            NotifyRoutedCall notify;
            static_cast<Payloads::RoutedCall&>(notify) = call;
            notify.Source = session.PlayerId(kPeer);
            return notify;
        }());
    }
    CHECK(session.SentTo<NotifyRoutedCall>(kPeer).empty());
}

TEST_CASE("A latent party call's original returned: its copies' games get the result, named as the sender's", "[HostAuthority][Scripts]")
{
    Session session;
    CallEndedRequest ended;
    ended.Key = 0x5A;
    ended.Result = int32_t(1);

    session.Receive(kHost, ended);
    for (const ConnectionId_t cGuest : {kPeer, kOtherPeer})
    {
        const auto cSent = session.SentTo<NotifyCallEnded>(cGuest);
        REQUIRE(cSent.size() == 1);
        CHECK((cSent[0]->Key == 0x5A && cSent[0]->Result == ended.Result && cSent[0]->Source == session.PlayerId(kHost)));
    }
    CHECK(session.SentTo<NotifyCallEnded>(kHost).empty());
}

TEST_CASE("The party is the Dragonborn: the story's progression call reaches every guest once", "[HostAuthority][Scripts][rewards]")
{
    // A dragon's absorb run for the guest on the host: AbsorbActor.ModActorValue("DragonSouls", 1), its player each
    // game's own (dragonborn.md, "The party's calls").
    Session session;
    RoutedCallRequest call;
    call.To = CallTo::kOthers;
    call.Call.Class = "Actor";
    call.Call.Function = "ModActorValue";
    call.Call.Self = ScriptForm{.Form = GameId(0, 0x14)};
    call.Call.Arguments = {std::string("DragonSouls"), 1.f};

    session.Receive(kHost, call);
    for (const ConnectionId_t cGuest : {kPeer, kOtherPeer})
    {
        const auto cSent = session.SentTo<NotifyRoutedCall>(cGuest);
        REQUIRE(cSent.size() == 1);
        CHECK(cSent[0]->Call == call.Call);
    }
    CHECK(session.SentTo<NotifyRoutedCall>(kHost).empty());
}

TEST_CASE("World calls: a guest's script's quest stage goes to the host's game, for that guest", "[HostAuthority][Scripts]")
{
    Session session;
    const uint32_t cCharacter = session.Character(kPeer);
    RoutedCallRequest call;
    call.To = CallTo::kHost;
    call.Call.Class = "Quest";
    call.Call.Function = "SetCurrentStageID";
    call.Call.Self = ScriptForm{.Form = GameId(0, 0x1F258)};
    call.Call.Arguments = {int32_t(15)};

    session.Receive(kPeer, call);
    const auto cSent = session.SentTo<NotifyRoutedCall>(kHost);
    REQUIRE(cSent.size() == 1);
    CHECK(cSent[0]->SessionId == cCharacter);
    CHECK(cSent[0]->Call == call.Call);
    CHECK(session.SentTo<NotifyRoutedCall>(kOtherPeer).empty());

    // The host's game makes its own calls.
    session.Receive(kHost, call);
    CHECK(session.SentTo<NotifyRoutedCall>(kHost).size() == 1);
    CHECK(session.SentTo<NotifyRoutedCall>(kPeer).empty());
}

TEST_CASE("An item change from a game that does not own the actor is dropped", "[HostAuthority][Inventory][items-part]")
{
    Session session;
    const uint32_t cHostNpc = session.Character(kHost);
    ItemsPart sword;
    sword.Full = true;
    sword.Items.Entries.push_back({.BaseId = GameId(0, 0x12EB7), .Count = 1});

    // A guest's engine changed the host NPC's copy (an event, or a part of it): nothing reaches anyone.
    RequestInventoryChanges change;
    change.SessionId = cHostNpc;
    change.Item = sword.Items.Entries[0];
    session.Receive(kPeer, change);
    RecordRequest stolen;
    stolen.Id = cHostNpc;
    stolen.Part = {RecordKind::kItems, 1, ToPartBytes(sword)};
    session.Receive(kPeer, stolen);
    CHECK(session.Outbox.empty());

    // Its owner's part goes to every other game.
    session.Receive(kHost, stolen);
    CHECK(session.SentTo<NotifyRecord>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyRecord>(kOtherPeer).size() == 1);
    CHECK(session.SentTo<NotifyRecord>(kHost).empty());
}

TEST_CASE("A host script's item change on a guest's character goes to that guest alone", "[HostAuthority][Inventory][items-part]")
{
    Session session;
    const uint32_t cGuestCharacter = session.Character(kPeer);
    RequestInventoryChanges taken; // a quest's hand-in takes the guest's claw on the host's copy
    taken.SessionId = cGuestCharacter;
    taken.Item = {.BaseId = GameId(0, 0x999E3), .Count = -1};
    session.Receive(kHost, taken);
    const auto cToOwner = session.SentTo<NotifyInventoryChanges>(kPeer);
    REQUIRE(cToOwner.size() == 1);
    CHECK(cToOwner[0]->Item.Count == -1);
    CHECK(session.SentTo<NotifyInventoryChanges>(kOtherPeer).empty()); // the owner's part carries it on
}

TEST_CASE("An items change the host cannot merge asks its owner for the whole part", "[HostAuthority][Inventory][items-part]")
{
    Session session;
    const uint32_t cCharacter = session.Character(kPeer);
    ItemsPart change; // only the stacks that changed, on top of a version the host never had
    change.Items.Entries.push_back({.BaseId = GameId(0, 0xF), .Count = 3});
    RecordRequest late;
    late.Id = cCharacter;
    late.Part = {RecordKind::kItems, 7, ToPartBytes(change)};
    session.Receive(kPeer, late);
    CHECK(session.SentTo<NotifyRecord>(kOtherPeer).empty());
    const auto cAsked = session.SentTo<NotifyRecordAsk>(kPeer);
    REQUIRE(cAsked.size() == 1);
    CHECK(cAsked[0]->Id == cCharacter);
    CHECK(cAsked[0]->Kind == RecordKind::kItems);
}

TEST_CASE("Values: the owner's reach everyone else, nobody else's do", "[HostAuthority][Values]")
{
    Session session;
    const uint32_t cHostNpc = session.Character(kHost);

    RequestActorValueChanges values;
    values.Id = cHostNpc;
    values.Values[24] = 50.f; // health
    values.TemporaryValues[24] = 30.f;
    session.Receive(kHost, values);
    REQUIRE(session.SentTo<NotifyActorValueChanges>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyActorValueChanges>(kPeer)[0]->Values.at(24) == 50.f);
    CHECK(session.SentTo<NotifyActorValueChanges>(kPeer)[0]->TemporaryValues.at(24) == 30.f);
    CHECK(session.SentTo<NotifyActorValueChanges>(kOtherPeer).size() == 1);
    CHECK(session.SentTo<NotifyActorValueChanges>(kHost).empty());

    session.Outbox.clear();
    session.Receive(kPeer, values);
    CHECK(session.Outbox.empty());
}

TEST_CASE("Death: from the owner only", "[HostAuthority][Values]")
{
    Session session;
    const uint32_t cHostNpc = session.Character(kHost);

    RequestDeathStateChange death;
    death.Id = cHostNpc;
    death.IsDead = true;
    death.DeathActionTick = 12345;
    session.Receive(kPeer, death);
    CHECK(session.Outbox.empty());

    session.Receive(kHost, death);
    REQUIRE(session.SentTo<NotifyDeathStateChange>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyDeathStateChange>(kPeer)[0]->IsDead);
    CHECK(session.SentTo<NotifyDeathStateChange>(kPeer)[0]->DeathActionTick == death.DeathActionTick);
}

TEST_CASE("Hits: go to the actor's owner alone", "[HostAuthority][Values]")
{
    Session session;
    const uint32_t cHostNpc = session.Character(kHost);

    RequestHealthChangeBroadcast hit;
    hit.Id = cHostNpc;
    hit.DeltaHealth = -12.f;
    session.Receive(kPeer, hit);

    const auto cToHost = session.SentTo<NotifyHealthChangeBroadcast>(kHost);
    REQUIRE(cToHost.size() == 1);
    CHECK(cToHost[0]->DeltaHealth == -12.f);
    CHECK(session.SentTo<NotifyHealthChangeBroadcast>(kOtherPeer).empty());

    // The owner's own hits are already applied in its game.
    session.Outbox.clear();
    session.Receive(kHost, hit);
    CHECK(session.Outbox.empty());
}

TEST_CASE("Friendly fire: a player's hit or spell on another player goes to that player's game alone",
          "[HostAuthority][Values][Magic]")
{
    Session session;
    const uint32_t cPeerPlayer = session.Character(kPeer);
    const uint32_t cOtherPlayer = session.Character(kOtherPeer);

    RequestHealthChangeBroadcast hit;
    hit.Id = cOtherPlayer;
    hit.DeltaHealth = -20.f;
    hit.HasAttacker = true;
    hit.AttackerId = cPeerPlayer;
    session.Receive(kPeer, hit);
    const auto cHit = session.SentTo<NotifyHealthChangeBroadcast>(kOtherPeer);
    REQUIRE(cHit.size() == 1);
    CHECK(cHit[0]->AttackerId == cPeerPlayer);
    CHECK(session.SentTo<NotifyHealthChangeBroadcast>(kHost).empty());

    AddTargetRequest effect;
    effect.TargetId = cOtherPlayer;
    effect.CasterId = cPeerPlayer;
    effect.Magnitude = 25.f;
    session.Receive(kPeer, effect);
    REQUIRE(session.SentTo<NotifyAddTarget>(kOtherPeer).size() == 1);
    CHECK(session.SentTo<NotifyAddTarget>(kHost).empty());

    // The host's own player hits a guest the same way.
    session.Outbox.clear();
    hit.AttackerId = session.CharacterId(kHost);
    session.Receive(kHost, hit);
    CHECK(session.SentTo<NotifyHealthChangeBroadcast>(kOtherPeer).size() == 1);
    CHECK(session.SentTo<NotifyHealthChangeBroadcast>(kPeer).empty());
}

TEST_CASE("Shots: replayed everywhere else, from the shooter's owner only", "[HostAuthority][Magic]")
{
    Session session;
    const uint32_t cPeerPlayer = session.Character(kPeer);
    const uint32_t cHostNpc = session.Character(kHost);

    ProjectileLaunchRequest shot;
    shot.ShooterID = cPeerPlayer;
    shot.OriginX = 10.f;
    session.Receive(kPeer, shot);
    REQUIRE(session.SentTo<NotifyProjectileLaunch>(kHost).size() == 1);
    CHECK(session.SentTo<NotifyProjectileLaunch>(kHost)[0]->OriginX == 10.f);

    session.Outbox.clear();
    shot.ShooterID = cHostNpc;
    session.Receive(kPeer, shot);
    CHECK(session.Outbox.empty());
}

TEST_CASE("Shots: an object's (no shooter) replayed everywhere else, from whichever game ran it", "[HostAuthority][Magic]")
{
    Session session;
    session.Character(kPeer);

    // An object's shot with no spell (an arrow trap's) goes nowhere.
    ProjectileLaunchRequest shot;
    shot.OriginZ = 3600.f;
    session.Receive(kPeer, shot);
    CHECK(session.Outbox.empty());

    // A Storm Call bolt from its script's activator (projectiles.md).
    shot.SpellID = GameId(0, 0xE4CB7);
    shot.ProjectileBaseID = GameId(0, 0xE4CB5);
    session.Receive(kPeer, shot);
    REQUIRE(session.SentTo<NotifyProjectileLaunch>(kHost).size() == 1);
    CHECK(session.SentTo<NotifyProjectileLaunch>(kHost)[0]->ShooterID == 0);
    CHECK(session.SentTo<NotifyProjectileLaunch>(kPeer).empty());

    session.Outbox.clear();
    session.Receive(kHost, shot);
    CHECK(session.SentTo<NotifyProjectileLaunch>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyProjectileLaunch>(kHost).empty());
}

TEST_CASE("Spell effects: applied by the target's owner alone", "[HostAuthority][Magic]")
{
    Session session;
    const uint32_t cPeerPlayer = session.Character(kPeer);
    const uint32_t cHostNpc = session.Character(kHost);

    AddTargetRequest effect;
    effect.TargetId = cHostNpc;
    effect.CasterId = cPeerPlayer;
    effect.Magnitude = 25.f;
    session.Receive(kPeer, effect);

    REQUIRE(session.SentTo<NotifyAddTarget>(kHost).size() == 1);
    CHECK(session.SentTo<NotifyAddTarget>(kHost)[0]->Magnitude == 25.f);
    CHECK(session.SentTo<NotifyAddTarget>(kOtherPeer).empty());
    CHECK(session.SentTo<NotifyAddTarget>(kPeer).empty());

    // On its own actor the caster's game already applied it.
    session.Outbox.clear();
    effect.TargetId = cPeerPlayer;
    session.Receive(kPeer, effect);
    CHECK(session.Outbox.empty());
}

TEST_CASE("Spell removal: reaches everyone else", "[HostAuthority][Magic]")
{
    Session session;
    const uint32_t cHostNpc = session.Character(kHost);

    RemoveSpellRequest removal;
    removal.TargetId = cHostNpc;
    removal.SpellId = GameId(0, 0x12FCD);
    session.Receive(kPeer, removal);

    REQUIRE(session.SentTo<NotifyRemoveSpell>(kHost).size() == 1);
    CHECK(session.SentTo<NotifyRemoveSpell>(kOtherPeer).size() == 1);
    CHECK(session.SentTo<NotifyRemoveSpell>(kPeer).empty());
}

TEST_CASE("Factions and actor state: each actor's parts from its owner only", "[HostAuthority][Characters][records]")
{
    Session session;
    const uint32_t cHostNpc = session.Character(kHost);
    const uint32_t cPeerPlayer = session.Character(kPeer);

    for (const RecordKind cKind : {RecordKind::kFactions, RecordKind::kActorState})
    {
        session.Outbox.clear();
        RecordRequest part;
        part.Id = cHostNpc; // not the peer's to say
        part.Part = {cKind, 1, std::string("\x01", 1)};
        session.Receive(kPeer, part);
        CHECK(session.Outbox.empty());

        part.Id = cPeerPlayer;
        session.Receive(kPeer, part);
        REQUIRE(session.SentTo<NotifyRecord>(kHost).size() == 1);
        CHECK(session.SentTo<NotifyRecord>(kHost)[0]->Part.Kind == cKind);
        CHECK(session.SentTo<NotifyRecord>(kPeer).empty());
    }
}

TEST_CASE("Subtitles, experience: reach everyone else", "[HostAuthority][Characters]")
{
    Session session;
    const uint32_t cHostNpc = session.Character(kHost);

    SubtitleRequest subtitle;
    subtitle.SessionId = cHostNpc;
    subtitle.Text = "Let me guess...";
    subtitle.TopicFormId = 0x1234;
    subtitle.Force = true;
    session.Receive(kHost, subtitle);
    const auto cSubtitles = session.SentTo<NotifySubtitle>(kPeer);
    REQUIRE(cSubtitles.size() == 1);
    CHECK(cSubtitles[0]->Text == "Let me guess...");
    CHECK(cSubtitles[0]->TopicFormId == 0x1234);
    CHECK(cSubtitles[0]->Force);


}

TEST_CASE("Scripts: a native on the player the host's script ran for a guest runs on that guest's game", "[HostAuthority][Scripts]")
{
    Session session;
    RoutedCallRequest request;
    request.To = CallTo::kOwner;
    request.SessionId = session.Character(kPeer);
    request.Call.Class = "Game";
    request.Call.Function = "AdvanceSkill";
    request.Call.Arguments = {std::string("Speechcraft"), 2.5f};

    // Only the host's game runs scripts for other players.
    session.Receive(kOtherPeer, request);
    CHECK(session.SentTo<NotifyRoutedCall>(kPeer).empty());

    session.Receive(kHost, request);
    const auto cSent = session.SentTo<NotifyRoutedCall>(kPeer);
    REQUIRE(cSent.size() == 1);
    CHECK(cSent[0]->Call == request.Call);
    CHECK(session.SentTo<NotifyRoutedCall>(kHost).empty());
    CHECK(session.SentTo<NotifyRoutedCall>(kOtherPeer).empty());
}

TEST_CASE("Scripts: a native on the player returning a value answers the host's waiting script", "[HostAuthority][Scripts]")
{
    Session session;
    RoutedCallRequest request;
    request.To = CallTo::kOwner;
    request.SessionId = session.Character(kPeer);
    request.Call.Class = "Game";
    request.Call.Function = "QueryStat";
    request.Call.Arguments = {std::string("Bribes")};
    request.ReplyId = 77;
    session.Receive(kHost, request);
    REQUIRE(session.SentTo<NotifyRoutedCall>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyRoutedCall>(kPeer)[0]->ReplyId == 77);

    PlayerNativeResultRequest result;
    result.ReplyId = 77;
    result.Result = int32_t(3);

    // Only the game asked answers, once, and only the host's game hears it.
    session.Receive(kOtherPeer, result);
    CHECK(session.SentTo<NotifyPlayerNativeResult>(kHost).empty());
    session.Receive(kPeer, result);
    session.Receive(kPeer, result);
    const auto cAnswers = session.SentTo<NotifyPlayerNativeResult>(kHost);
    REQUIRE(cAnswers.size() == 1);
    CHECK((cAnswers[0]->ReplyId == 77 && cAnswers[0]->Result == result.Result));
    CHECK(session.SentTo<NotifyPlayerNativeResult>(kPeer).empty());
    CHECK(session.SentTo<NotifyPlayerNativeResult>(kOtherPeer).empty());
}

TEST_CASE("Scripts: a guest leaving before it answers leaves the host's script with none", "[HostAuthority][Scripts]")
{
    Session session;
    RoutedCallRequest request;
    request.To = CallTo::kOwner;
    request.SessionId = session.Character(kPeer);
    request.Call.Class = "Game";
    request.Call.Function = "QueryStat";
    request.ReplyId = 78;
    session.Receive(kHost, request);
    CHECK(session.SentTo<NotifyPlayerNativeResult>(kHost).empty());

    session.Leave(kPeer);
    const auto cAnswers = session.SentTo<NotifyPlayerNativeResult>(kHost);
    REQUIRE(cAnswers.size() == 1);
    CHECK((cAnswers[0]->ReplyId == 78 && std::holds_alternative<std::monostate>(cAnswers[0]->Result)));

    // Asked after it left: answered at once, with none.
    request.ReplyId = 79;
    session.Receive(kHost, request);
    const auto cLater = session.SentTo<NotifyPlayerNativeResult>(kHost);
    REQUIRE(cLater.size() == 2);
    CHECK((cLater[1]->ReplyId == 79 && std::holds_alternative<std::monostate>(cLater[1]->Result)));
}

TEST_CASE("Scripts: a guest's script creating a reference gets the host's game's answer, only that guest", "[HostAuthority][Scripts]")
{
    Session session;
    const uint32_t cPlayer = session.Character(kPeer);
    session.Character(kOtherPeer);
    RoutedCallRequest request;
    request.To = CallTo::kHost;
    request.Call.Class = "ObjectReference";
    request.Call.Function = "PlaceAtMe";
    request.ReplyId = 41;
    session.Receive(kPeer, request);
    const auto cCalls = session.SentTo<NotifyRoutedCall>(kHost);
    REQUIRE(cCalls.size() == 1);
    CHECK((cCalls[0]->ReplyId == 41 && cCalls[0]->SessionId == cPlayer));

    PlayerNativeResultRequest result;
    result.ReplyId = 41;
    result.SessionId = cPlayer;
    result.Result = ScriptForm{.Form = GameId(0xFFFFFFFF, 0x812)};
    // Only the host's game answers it, once, and only the asking guest hears it.
    session.Receive(kOtherPeer, result);
    CHECK(session.SentTo<NotifyPlayerNativeResult>(kPeer).empty());
    session.Receive(kHost, result);
    session.Receive(kHost, result);
    const auto cAnswers = session.SentTo<NotifyPlayerNativeResult>(kPeer);
    REQUIRE(cAnswers.size() == 1);
    CHECK((cAnswers[0]->ReplyId == 41 && cAnswers[0]->Result == result.Result));
    CHECK(session.SentTo<NotifyPlayerNativeResult>(kOtherPeer).empty());
    CHECK(session.SentTo<NotifyPlayerNativeResult>(kHost).empty());
}

TEST_CASE("Summons: a guest's summon is made by the host's game, cast by that guest's character", "[HostAuthority][Summons]")
{
    Session session;
    const uint32_t cPlayer = session.Character(kPeer);
    SummonRequest summon;
    summon.SpellId = GameId(0, 0x640B6);
    summon.EffectId = GameId(0, 0x640B4);
    summon.Magnitude = 60.f;
    session.Outbox.clear();
    session.Receive(kPeer, summon);
    REQUIRE(session.Summons.size() == 1);
    CHECK(session.Summons[0] == std::pair{cPlayer, GameId(0, 0x640B6)});
    // Nothing is relayed: the familiar reaches the others as the host's actor.
    CHECK(session.Outbox.empty());

    // The host's own player makes its own: a request from it is nothing.
    session.Receive(kHost, summon);
    CHECK(session.Summons.size() == 1);
}

TEST_CASE("Objects: a guest's activation runs on the host's game only; a guest's lock and container change are the host's to take", "[HostAuthority][Objects]")
{
    Session session;
    const uint32_t cPlayer = session.Character(kPeer);

    ActivateRequest activate;
    activate.Id = GameId(0, 0x1A2B);
    activate.CellId = GameId(0, 0x9732);
    activate.ActivatorId = cPlayer;
    session.Outbox.clear();
    session.Receive(kPeer, activate);
    REQUIRE(session.Activations.size() == 1);
    CHECK(session.Activations[0] == std::tuple{GameId(0, 0x1A2B), 0u, cPlayer});
    // The other games get what changed as the host's rows, never the activation.
    CHECK(session.Outbox.empty());

    // The host's own activation, and one naming another game's character, go nowhere.
    session.Outbox.clear();
    ActivateRequest hosts = activate;
    hosts.ActivatorId = session.Character(kHost);
    session.Receive(kHost, hosts);
    ActivateRequest other = activate;
    other.ActivatorId = session.Character(kOtherPeer);
    session.Receive(kPeer, other);
    CHECK(session.Activations.size() == 1);

    // A guest picked a lock: the host's game takes it (and sends it as its own change); nothing is relayed.
    LockChangeRequest lock;
    lock.Id = GameId(0, 0x1A2B);
    lock.IsLocked = false;
    lock.LockLevel = 25;
    session.Outbox.clear();
    session.Receive(kPeer, lock);
    REQUIRE(session.LocksSet.size() == 1);
    CHECK(std::get<0>(session.LocksSet[0]) == GameId(0, 0x1A2B));
    CHECK_FALSE(std::get<1>(session.LocksSet[0]));
    CHECK(std::get<2>(session.LocksSet[0]) == 25);
    CHECK(session.Outbox.empty());

    // The host's own lock goes out with its object changes, not this way.
    session.Receive(kHost, lock);
    CHECK(session.LocksSet.size() == 1);

    // A guest took from a chest: the host's game takes its count now; nothing is relayed.
    ContainerChangeRequest items;
    items.Container = GameId(0, 0xA05B5);
    items.Item = GameId(0, 0x64B3D);
    items.Count = 1;
    session.Receive(kPeer, items);
    REQUIRE(session.ItemsSet.size() == 1);
    CHECK(session.ItemsSet[0].Object == GameId(0, 0xA05B5));
    CHECK(session.ItemsSet[0].Item == GameId(0, 0x64B3D));
    CHECK(session.ItemsSet[0].Value == 1u);
    CHECK(session.ItemsSet[0].Stacks == items.Stacks);
    CHECK(session.Outbox.empty());

    // The host's own containers go out with its object changes.
    session.Receive(kHost, items);
    CHECK(session.ItemsSet.size() == 1);
}

TEST_CASE("A record's part: its owner's reaches everyone else; nobody else's counts", "[HostAuthority][records]")
{
    Session session;
    const uint32_t cHostNpc = session.Character(kHost);

    ActorAiState ai;
    ai.Package = GameId(0, 0x1A6A5);
    ai.CombatTarget.SessionId = 7;
    RecordRequest part;
    part.Id = cHostNpc;
    part.Part = {RecordKind::kAiState, 2, ToPartBytes(ai)};
    session.Receive(kPeer, part); // not its
    CHECK(session.Outbox.empty());

    session.Receive(kHost, part);
    for (const auto cTo : {kPeer, kOtherPeer})
    {
        const auto cNotifies = session.SentTo<NotifyRecord>(cTo);
        REQUIRE(cNotifies.size() == 1);
        CHECK(cNotifies[0]->Id == cHostNpc);
        CHECK(cNotifies[0]->Part == part.Part);
    }
    CHECK(session.SentTo<NotifyRecord>(kHost).empty());

    // An older version than the one held is dropped (order free: the last state wins).
    session.Outbox.clear();
    part.Part.Version = 1;
    session.Receive(kHost, part);
    CHECK(session.Outbox.empty());
}

TEST_CASE("A joiner gets every part: each spawn carries its actor's record as last declared", "[HostAuthority][records]")
{
    Session session;
    AnnounceCharacterRequest announce;
    announce.SessionId = session.NewCharacterId();
    announce.ReferenceId = GameId(1, 0x2000);
    ActorAiState ai;
    ai.Package = GameId(0, 0x1A6A5);
    announce.Record.Parts.push_back({RecordKind::kAiState, 1, ToPartBytes(ai)});
    session.Receive(kHost, announce);
    const auto cFirst = session.SentTo<NotifyCharacterSpawn>(kPeer);
    REQUIRE(cFirst.size() == 1);
    CHECK(cFirst[0]->Record == announce.Record);

    // The owner declares it again; the host's game still reads the actor as it announced it.
    ai.Combat = 1;
    RecordRequest part;
    part.Id = announce.SessionId;
    part.Part = {RecordKind::kAiState, 2, ToPartBytes(ai)};
    session.Receive(kHost, part);
    session.Spawns.push_back(NotifyCharacterSpawn::FromAnnouncement(announce, announce.SessionId, 0));
    session.Outbox.clear();

    const ConnectionId_t cJoiner = 4;
    session.Join(cJoiner, JoinRequest("Farkas"));
    AnnounceCharacterRequest player;
    player.SessionId = session.CharacterId(cJoiner);
    player.ReferenceId = GameId(0, 0x14);
    session.Receive(cJoiner, player);
    const auto cSpawns = session.SentTo<NotifyCharacterSpawn>(cJoiner);
    const auto cNpc = std::ranges::find_if(cSpawns, [&](const auto* apSpawn) { return apSpawn->SessionId == announce.SessionId; });
    REQUIRE(cNpc != cSpawns.end());
    REQUIRE((*cNpc)->Record.Parts.size() == 1);
    CHECK((*cNpc)->Record.Parts[0] == part.Part);

    // Gone, its record goes with it.
    RequestRemoveCharacter remove;
    remove.SessionId = announce.SessionId;
    session.Receive(kHost, remove);
    session.Outbox.clear();
    part.Part.Version = 3;
    session.Receive(kHost, part);
    CHECK(session.Outbox.empty());
}

TEST_CASE("An asked part: the actor's owner alone is told", "[HostAuthority][records]")
{
    Session session;
    const uint32_t cHostNpc = session.Character(kHost);
    RecordAskRequest ask;
    ask.Id = cHostNpc;
    ask.Kind = RecordKind::kAiState;
    session.Receive(kHost, ask); // its own
    CHECK(session.Outbox.empty());
    session.Receive(kPeer, ask);
    REQUIRE(session.SentTo<NotifyRecordAsk>(kHost).size() == 1);
    CHECK(session.SentTo<NotifyRecordAsk>(kHost)[0]->Kind == RecordKind::kAiState);
    CHECK(session.SentTo<NotifyRecordAsk>(kPeer).empty());
    CHECK(session.SentTo<NotifyRecordAsk>(kOtherPeer).empty());
    ask.Id = 0xFFFF; // nobody's
    session.Outbox.clear();
    session.Receive(kPeer, ask);
    CHECK(session.Outbox.empty());
}


TEST_CASE("Melee staggers come from the attacker's owner and go to the victim alone", "[HostAuthority][Combat][melee-stagger]")
{
    Session session;
    MeleeStaggerRequest stagger;
    stagger.HitterId = session.Character(kPeer);
    stagger.HitteeId = session.Character(kOtherPeer);
    stagger.Magnitude = 0.75f;
    session.Outbox.clear();
    session.Receive(kHost, stagger);
    CHECK(session.Outbox.empty());
    session.Receive(kPeer, stagger);
    const auto cSent = session.SentTo<NotifyMeleeStagger>(kOtherPeer);
    REQUIRE(cSent.size() == 1);
    CHECK(cSent[0]->Magnitude == stagger.Magnitude);
    CHECK(session.SentTo<NotifyMeleeStagger>(kHost).empty());
    CHECK(session.SentTo<NotifyMeleeStagger>(kPeer).empty());
    session.Outbox.clear();
    stagger.Magnitude = 0.f;
    session.Receive(kPeer, stagger);
    CHECK(session.Outbox.empty());
}

TEST_CASE("A quest one per player: the game that learned it tells every other", "[HostAuthority][SystemQuests]")
{
    Session session;
    SystemQuestsRequest learned;
    learned.Quests = {GameId(0, 0xEAFD5)}; // PlayerVampireQuest, entered by the peer's disease
    session.Receive(kPeer, learned);
    for (const auto cTo : {kHost, kOtherPeer})
    {
        const auto cNotifies = session.SentTo<NotifySystemQuests>(cTo);
        REQUIRE(cNotifies.size() == 1);
        CHECK(cNotifies[0]->Quests == learned.Quests);
    }
    CHECK(session.SentTo<NotifySystemQuests>(kPeer).empty());
}

TEST_CASE("Graph stream: ops from each actor's owner, sync points asked of the owner", "[HostAuthority][Characters]")
{
    Session session;
    const uint32_t cHostNpc = session.Character(kHost);
    const uint32_t cPeerPlayer = session.Character(kPeer);

    GraphOpsRequest ops;
    ops.Ops.Frames = {{100, 0.016f}};
    ops.Ops.Runs = {{cHostNpc, 0, {{.Frame = 0}}}, {cPeerPlayer, 0, {{.Frame = 0}}}}; // the player's: not the host's to say
    session.Receive(kHost, ops);
    const auto cOps = session.SentTo<NotifyGraphOps>(kPeer);
    REQUIRE(cOps.size() == 1);
    REQUIRE(cOps[0]->Ops.Runs.size() == 1);
    CHECK(cOps[0]->Ops.Runs[0].ActorId == cHostNpc);
    CHECK(session.SentTo<NotifyGraphOps>(kHost).empty());

    GraphSyncRequest ask;
    ask.Id = cHostNpc;
    session.Receive(kPeer, ask);
    CHECK(session.SentTo<NotifyGraphSync>(kHost).size() == 1); // the owner's game alone
    CHECK(session.SentTo<NotifyGraphSync>(kOtherPeer).empty());

    GraphSyncPointRequest point;
    point.Id = cHostNpc;
    session.Receive(kPeer, point); // not the peer's actor
    CHECK(session.SentTo<NotifyGraphSyncPoint>(kOtherPeer).empty());
    session.Receive(kHost, point);
    CHECK(session.SentTo<NotifyGraphSyncPoint>(kPeer).size() == 1);
}
