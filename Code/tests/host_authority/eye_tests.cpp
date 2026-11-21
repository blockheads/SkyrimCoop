// HostEye: a guest's stealth eye is what the host's NPCs see of its player (stealth.md, "The eye is the host's").
#include "session.h"

TEST_CASE("Eye: each guest gets the host's NPCs' reading of its own player, the host none", "[HostAuthority][Eye]")
{
    Session session;
    const uint32_t cGuest = session.Character(kPeer);
    session.Character(kOtherPeer);
    session.Frame(1000);
    CHECK(session.SentTo<NotifyStealthEye>(kPeer).empty()); // no copy read yet: nothing to say

    session.Eyes[cGuest] = {.Level = 31, .Seeing = 1, .Stealth = 100.f};
    session.Eyes[session.CharacterId(kHost)] = {.Level = 80, .Seeing = 2, .Stealth = 100.f};
    session.Frame(1100);
    REQUIRE(session.SentTo<NotifyStealthEye>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyStealthEye>(kPeer)[0]->Level == 31);
    CHECK(session.SentTo<NotifyStealthEye>(kPeer)[0]->Seeing == 1);
    CHECK(session.SentTo<NotifyStealthEye>(kOtherPeer).empty()); // its copy not read: its own stays
    CHECK(session.SentTo<NotifyStealthEye>(kHost).empty());      // the host's eye is its own game's
}

TEST_CASE("Eye: sent again only when the guest's eye could read otherwise", "[HostAuthority][Eye]")
{
    Session session;
    const uint32_t cGuest = session.Character(kPeer);
    session.Eyes[cGuest] = {.Level = 31, .Seeing = 1, .Stealth = 100.f};
    session.Frame(1000);
    session.Outbox.clear();

    session.Eyes[cGuest].Level = 33; // within a step
    session.Frame(1100);
    CHECK(session.SentTo<NotifyStealthEye>(kPeer).empty());

    session.Eyes[cGuest] = {.Level = -4, .Seeing = 0, .Stealth = 100.f}; // unseen
    session.Frame(1200);
    REQUIRE(session.SentTo<NotifyStealthEye>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyStealthEye>(kPeer)[0]->Level == -4);

    session.Outbox.clear();
    session.Eyes[cGuest].Stealth = 0.f; // a fight's stealth points
    session.Frame(1300);
    REQUIRE(session.SentTo<NotifyStealthEye>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyStealthEye>(kPeer)[0]->Stealth == 0.f);

    // Its copy unloaded while seen: no NPC of the host's sees it (the guest would keep the last, lockpicking included).
    session.Outbox.clear();
    session.Eyes[cGuest] = {.Level = 31, .Seeing = 1, .Stealth = 100.f};
    session.Frame(1400);
    session.Outbox.clear();
    session.Eyes.erase(cGuest);
    session.Frame(1500);
    REQUIRE(session.SentTo<NotifyStealthEye>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyStealthEye>(kPeer)[0]->Level == NotifyStealthEye::kUnseen);
    session.Outbox.clear();
    session.Frame(1600);
    CHECK(session.SentTo<NotifyStealthEye>(kPeer).empty());
    // Loaded again and seen: sent again.
    session.Eyes[cGuest] = {.Level = 31, .Seeing = 1, .Stealth = 100.f};
    session.Frame(1700);
    REQUIRE(session.SentTo<NotifyStealthEye>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifyStealthEye>(kPeer)[0]->Level == 31);
}
