// The bridge's relays: who receives each request's notification.
#include "session.h"

TEST_CASE("Hands: the owner's change reaches everyone else", "[Bridge][Inventory]")
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

TEST_CASE("Hands: only the owner decides", "[Bridge][Inventory]")
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

TEST_CASE("Equipment snapshot: relayed from the owner only", "[Bridge][Inventory]")
{
    Session session;
    const uint32_t cHostNpc = session.Character(kHost);

    RequestEquipmentSnapshot snapshot;
    snapshot.ServerId = cHostNpc;
    session.Receive(kPeer, snapshot);
    CHECK(session.SentTo<NotifyEquipmentSnapshot>(kOtherPeer).empty());

    session.Receive(kHost, snapshot);
    REQUIRE(session.SentTo<NotifyEquipmentSnapshot>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyEquipmentSnapshot>(kPeer)[0]->ServerId == cHostNpc);
    CHECK(session.SentTo<NotifyEquipmentSnapshot>(kHost).empty());
}

TEST_CASE("Active effects: relayed from the owner only, to everyone else", "[Bridge][Magic]")
{
    Session session;
    const uint32_t cHostNpc = session.Character(kHost);

    RequestActiveEffects effects;
    effects.ServerId = cHostNpc;
    effects.EffectIds = {GameId(0, 0x12F03)};
    session.Receive(kPeer, effects);
    CHECK(session.SentTo<NotifyActiveEffects>(kOtherPeer).empty());

    session.Receive(kHost, effects);
    REQUIRE(session.SentTo<NotifyActiveEffects>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyActiveEffects>(kPeer)[0]->EffectIds == effects.EffectIds);
    CHECK(session.SentTo<NotifyActiveEffects>(kOtherPeer).size() == 1);
    CHECK(session.SentTo<NotifyActiveEffects>(kHost).empty());
}

TEST_CASE("Items: anyone may change an actor's, the owner's game applies it", "[Bridge][Inventory]")
{
    Session session;
    const uint32_t cHostNpc = session.Character(kHost);

    RequestInventoryChanges change;
    change.ServerId = cHostNpc;
    change.Item.BaseId = GameId(0, 0x12EB7);
    change.Item.Count = 1;
    change.Drop = true;
    change.UpdateClients = true;
    session.Receive(kPeer, change);

    const auto cToHost = session.SentTo<NotifyInventoryChanges>(kHost);
    REQUIRE(cToHost.size() == 1);
    CHECK(cToHost[0]->ServerId == cHostNpc);
    CHECK(cToHost[0]->Item.Count == 1);
    CHECK_FALSE(cToHost[0]->Drop); // dropped items are not synced
    CHECK(session.SentTo<NotifyInventoryChanges>(kOtherPeer).size() == 1);
    CHECK(session.SentTo<NotifyInventoryChanges>(kPeer).empty());

    change.UpdateClients = false;
    session.Outbox.clear();
    session.Receive(kPeer, change);
    CHECK(session.Outbox.empty());
}

TEST_CASE("Equipment: a change reaches everyone else", "[Bridge][Inventory]")
{
    Session session;
    const uint32_t cActor = session.Character(kPeer);

    RequestEquipmentChanges change;
    change.ServerId = cActor;
    change.ItemId = GameId(0, 0x12EB7);
    change.Count = 1;
    session.Receive(kPeer, change);

    REQUIRE(session.SentTo<NotifyEquipmentChanges>(kHost).size() == 1);
    CHECK(session.SentTo<NotifyEquipmentChanges>(kHost)[0]->ItemId == GameId(0, 0x12EB7));
    CHECK(session.SentTo<NotifyEquipmentChanges>(kOtherPeer).size() == 1);
    CHECK(session.SentTo<NotifyEquipmentChanges>(kPeer).empty());
}

TEST_CASE("Values: the owner's reach everyone else, nobody else's do", "[Bridge][Values]")
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

TEST_CASE("Death: from the owner only", "[Bridge][Values]")
{
    Session session;
    const uint32_t cHostNpc = session.Character(kHost);

    RequestDeathStateChange death;
    death.Id = cHostNpc;
    death.IsDead = true;
    session.Receive(kPeer, death);
    CHECK(session.Outbox.empty());

    session.Receive(kHost, death);
    REQUIRE(session.SentTo<NotifyDeathStateChange>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyDeathStateChange>(kPeer)[0]->IsDead);
}

TEST_CASE("Hits: go to the actor's owner alone", "[Bridge][Values]")
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

TEST_CASE("Casts and shots: replayed everywhere else, from the caster's owner only", "[Bridge][Magic]")
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

TEST_CASE("Spell effects: applied by the target's owner alone", "[Bridge][Magic]")
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

TEST_CASE("Spell removal: reaches everyone else", "[Bridge][Magic]")
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

TEST_CASE("Factions: each actor's from its owner only", "[Bridge][Characters]")
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

TEST_CASE("Mounts, subtitles, experience: reach everyone else", "[Bridge][Characters]")
{
    Session session;
    const uint32_t cHostNpc = session.Character(kHost);
    const uint32_t cHorse = session.Character(kHost);

    MountRequest mount;
    mount.RiderId = cHostNpc;
    mount.MountId = cHorse;
    session.Receive(kPeer, mount);
    REQUIRE(session.SentTo<NotifyMount>(kHost).size() == 1);
    CHECK(session.SentTo<NotifyMount>(kHost)[0]->MountId == cHorse);

    SubtitleRequest subtitle;
    subtitle.ServerId = cHostNpc;
    subtitle.Text = "Let me guess...";
    subtitle.TopicFormId = 0x1234;
    session.Receive(kHost, subtitle);
    const auto cSubtitles = session.SentTo<NotifySubtitle>(kPeer);
    REQUIRE(cSubtitles.size() == 1);
    CHECK(cSubtitles[0]->Text == "Let me guess...");
    CHECK(cSubtitles[0]->TopicFormId == 0x1234);

    SyncExperienceRequest experience;
    experience.Experience = 12.5f;
    session.Receive(kPeer, experience);
    REQUIRE(session.SentTo<NotifySyncExperience>(kHost).size() == 1);
    CHECK(session.SentTo<NotifySyncExperience>(kOtherPeer)[0]->Experience == 12.5f);
    CHECK(session.SentTo<NotifySyncExperience>(kPeer).empty());
}

TEST_CASE("Objects: an activation, a lock and a script animation reach everyone else", "[Bridge][Objects]")
{
    Session session;
    const uint32_t cPlayer = session.Character(kPeer);

    ActivateRequest activate;
    activate.Id = GameId(0, 0x1A2B);
    activate.CellId = GameId(0, 0x9732);
    activate.ActivatorId = cPlayer;
    activate.PreActivationOpenState = 2;
    session.Receive(kPeer, activate);

    LockChangeRequest lock;
    lock.Id = GameId(0, 0x1A2B);
    lock.CellId = GameId(0, 0x9732);
    lock.IsLocked = false;
    lock.LockLevel = 25;
    session.Receive(kPeer, lock);

    ScriptAnimationRequest animation;
    animation.FormID = 0x1A2C;
    animation.EventName = "Open";
    session.Receive(kHost, animation);

    for (const auto cTo : {kHost, kOtherPeer})
    {
        const auto cActivations = session.SentTo<NotifyActivate>(cTo);
        REQUIRE(cActivations.size() == 1);
        CHECK(cActivations[0]->Id == GameId(0, 0x1A2B));
        CHECK(cActivations[0]->ActivatorId == cPlayer);
        CHECK(cActivations[0]->PreActivationOpenState == 2);

        const auto cLocks = session.SentTo<NotifyLockChange>(cTo);
        REQUIRE(cLocks.size() == 1);
        CHECK_FALSE(cLocks[0]->IsLocked);
        CHECK(cLocks[0]->LockLevel == 25);
    }
    CHECK(session.SentTo<NotifyActivate>(kPeer).empty());
    CHECK(session.SentTo<NotifyLockChange>(kPeer).empty());

    for (const auto cTo : {kPeer, kOtherPeer})
    {
        const auto cAnimations = session.SentTo<NotifyScriptAnimation>(cTo);
        REQUIRE(cAnimations.size() == 1);
        CHECK(cAnimations[0]->FormID == 0x1A2C);
        CHECK(cAnimations[0]->EventName == "Open");
    }
    CHECK(session.SentTo<NotifyScriptAnimation>(kHost).empty());
}
