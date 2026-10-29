// HostAuthority's relays: who receives each request's notification.
#include "session.h"

TEST_CASE("Hands: the owner's change reaches everyone else", "[HostAuthority][Inventory]")
{
    Session session;
    const uint32_t cActor = session.Character(kPeer);

    DrawWeaponRequest request;
    request.Id = cActor;
    request.IsWeaponDrawn = true;
    session.Receive(kPeer, request);

    for (const auto cTo : {kHost, kOtherPeer})
    {
        const auto cNotifies = session.SentTo<NotifyDrawWeapon>(cTo);
        REQUIRE(cNotifies.size() == 1);
        CHECK(cNotifies[0]->Id == cActor);
        CHECK(cNotifies[0]->IsWeaponDrawn);
    }
    CHECK(session.SentTo<NotifyDrawWeapon>(kPeer).empty());
}

TEST_CASE("Hands: only the owner decides", "[HostAuthority][Inventory]")
{
    Session session;
    const uint32_t cHostNpc = session.Character(kHost);

    DrawWeaponRequest request;
    request.Id = cHostNpc;
    request.IsWeaponDrawn = true;
    session.Receive(kPeer, request);

    CHECK(session.SentTo<NotifyDrawWeapon>(kHost).empty());
    CHECK(session.SentTo<NotifyDrawWeapon>(kOtherPeer).empty());
}

TEST_CASE("Equipment snapshot: relayed from the owner only", "[HostAuthority][Inventory]")
{
    Session session;
    const uint32_t cHostNpc = session.Character(kHost);

    RequestEquipmentSnapshot snapshot;
    snapshot.SessionId = cHostNpc;
    session.Receive(kPeer, snapshot);
    CHECK(session.SentTo<NotifyEquipmentSnapshot>(kOtherPeer).empty());

    session.Receive(kHost, snapshot);
    REQUIRE(session.SentTo<NotifyEquipmentSnapshot>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyEquipmentSnapshot>(kPeer)[0]->SessionId == cHostNpc);
    CHECK(session.SentTo<NotifyEquipmentSnapshot>(kHost).empty());
}

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

TEST_CASE("Items: anyone may change an actor's, the owner's game applies it", "[HostAuthority][Inventory]")
{
    Session session;
    const uint32_t cHostNpc = session.Character(kHost);

    RequestInventoryChanges change;
    change.SessionId = cHostNpc;
    change.Item.BaseId = GameId(0, 0x12EB7);
    change.Item.Count = 1;
    change.Drop = true;
    change.UpdateClients = true;
    session.Receive(kPeer, change);

    const auto cToHost = session.SentTo<NotifyInventoryChanges>(kHost);
    REQUIRE(cToHost.size() == 1);
    CHECK(cToHost[0]->SessionId == cHostNpc);
    CHECK(cToHost[0]->Item.Count == 1);
    CHECK_FALSE(cToHost[0]->Drop); // dropped items are not synced
    CHECK(session.SentTo<NotifyInventoryChanges>(kOtherPeer).size() == 1);
    CHECK(session.SentTo<NotifyInventoryChanges>(kPeer).empty());

    change.UpdateClients = false;
    session.Outbox.clear();
    session.Receive(kPeer, change);
    CHECK(session.Outbox.empty());
}

TEST_CASE("Equipment: a change reaches everyone else", "[HostAuthority][Inventory]")
{
    Session session;
    const uint32_t cActor = session.Character(kPeer);

    RequestEquipmentChanges change;
    change.SessionId = cActor;
    change.ItemId = GameId(0, 0x12EB7);
    change.Count = 1;
    session.Receive(kPeer, change);

    REQUIRE(session.SentTo<NotifyEquipmentChanges>(kHost).size() == 1);
    CHECK(session.SentTo<NotifyEquipmentChanges>(kHost)[0]->ItemId == GameId(0, 0x12EB7));
    CHECK(session.SentTo<NotifyEquipmentChanges>(kOtherPeer).size() == 1);
    CHECK(session.SentTo<NotifyEquipmentChanges>(kPeer).empty());
}

TEST_CASE("Values: the owner's reach everyone else, nobody else's do", "[HostAuthority][Values]")
{
    Session session;
    const uint32_t cHostNpc = session.Character(kHost);

    RequestActorValueChanges values;
    values.Id = cHostNpc;
    values.Values[24] = 50.f; // health
    session.Receive(kHost, values);
    REQUIRE(session.SentTo<NotifyActorValueChanges>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyActorValueChanges>(kPeer)[0]->Values.at(24) == 50.f);
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

TEST_CASE("Casts and shots: replayed everywhere else, from the caster's owner only", "[HostAuthority][Magic]")
{
    Session session;
    const uint32_t cPeerPlayer = session.Character(kPeer);
    const uint32_t cHostNpc = session.Character(kHost);

    SpellCastRequest cast;
    cast.CasterId = cPeerPlayer;
    cast.SpellFormId = GameId(0, 0x12FCD); // firebolt
    session.Receive(kPeer, cast);
    REQUIRE(session.SentTo<NotifySpellCast>(kHost).size() == 1);
    CHECK(session.SentTo<NotifySpellCast>(kHost)[0]->SpellFormId == GameId(0, 0x12FCD));
    CHECK(session.SentTo<NotifySpellCast>(kOtherPeer).size() == 1);
    CHECK(session.SentTo<NotifySpellCast>(kPeer).empty());

    ProjectileLaunchRequest shot;
    shot.ShooterID = cPeerPlayer;
    shot.OriginX = 10.f;
    session.Receive(kPeer, shot);
    REQUIRE(session.SentTo<NotifyProjectileLaunch>(kHost).size() == 1);
    CHECK(session.SentTo<NotifyProjectileLaunch>(kHost)[0]->OriginX == 10.f);

    session.Outbox.clear();
    cast.CasterId = cHostNpc;
    session.Receive(kPeer, cast);
    shot.ShooterID = cHostNpc;
    session.Receive(kPeer, shot);
    CHECK(session.Outbox.empty());
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

TEST_CASE("Factions: each actor's from its owner only", "[HostAuthority][Characters]")
{
    Session session;
    const uint32_t cHostNpc = session.Character(kHost);
    const uint32_t cPeerPlayer = session.Character(kPeer);

    RequestFactionsChanges changes;
    changes.Changes[cPeerPlayer] = {};
    changes.Changes[cHostNpc] = {}; // not the peer's to say
    session.Receive(kPeer, changes);

    const auto cToHost = session.SentTo<NotifyFactionsChanges>(kHost);
    REQUIRE(cToHost.size() == 1);
    CHECK(cToHost[0]->Changes.size() == 1);
    CHECK(cToHost[0]->Changes.count(cPeerPlayer) == 1);
    CHECK(session.SentTo<NotifyFactionsChanges>(kPeer).empty());

    session.Outbox.clear();
    changes.Changes.erase(cPeerPlayer);
    session.Receive(kPeer, changes);
    CHECK(session.Outbox.empty());
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

TEST_CASE("Objects: an activation reaches everyone else; a guest's lock and container change are the host's to take", "[HostAuthority][Objects]")
{
    Session session;
    const uint32_t cPlayer = session.Character(kPeer);

    ActivateRequest activate;
    activate.Id = GameId(0, 0x1A2B);
    activate.CellId = GameId(0, 0x9732);
    activate.ActivatorId = cPlayer;
    activate.PreActivationOpenState = 2;
    session.Receive(kPeer, activate);
    for (const auto cTo : {kHost, kOtherPeer})
    {
        const auto cActivations = session.SentTo<NotifyActivate>(cTo);
        REQUIRE(cActivations.size() == 1);
        CHECK(cActivations[0]->Id == GameId(0, 0x1A2B));
        CHECK(cActivations[0]->ActivatorId == cPlayer);
        CHECK(cActivations[0]->PreActivationOpenState == 2);
    }
    CHECK(session.SentTo<NotifyActivate>(kPeer).empty());

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

TEST_CASE("AI state: the owner's reaches everyone else; nobody else's counts", "[HostAuthority][AiState]")
{
    Session session;
    const uint32_t cHostNpc = session.Character(kHost);

    AiStateRequest state;
    state.SessionId = cHostNpc;
    state.State.Package = GameId(0, 0x1A6A5);
    state.State.CombatTarget.SessionId = 7;
    session.Receive(kPeer, state); // not its
    CHECK(session.Outbox.empty());

    session.Receive(kHost, state);
    for (const auto cTo : {kPeer, kOtherPeer})
    {
        const auto cNotifies = session.SentTo<NotifyAiState>(cTo);
        REQUIRE(cNotifies.size() == 1);
        CHECK(cNotifies[0]->SessionId == cHostNpc);
        CHECK(cNotifies[0]->State == state.State);
    }
    CHECK(session.SentTo<NotifyAiState>(kHost).empty());
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
