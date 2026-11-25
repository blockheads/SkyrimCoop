// HostSaveTransfer: the host's saves on their way to the guests (docs/design/join-flow.md, "Save sync").
#include "session.h"

#include <Messages/DeathMessages.h>
#include <Messages/JoinMessages.h>
#include <Services/Host/HostSaveTransfer.h>

namespace
{
const std::string kOld(32, 'a');
const std::string kNew(32, 'b');

SaveHeldRequest Held(std::initializer_list<std::string> aSaves)
{
    SaveHeldRequest held;
    held.Saves = aSaves;
    return held;
}
} // namespace

TEST_CASE("A host load tells every guest, and sends the save only to those that do not hold it", "[save]")
{
    Session session;
    session.SaveImages = {kOld, kNew};
    session.Receive(kPeer, Held({kOld, kNew}));
    session.Receive(kOtherPeer, Held({kOld}));

    session.Authority().SendCheckpoint(kNew);
    for (const ConnectionId_t cGuest : {kPeer, kOtherPeer})
    {
        const auto cReloads = session.SentTo<NotifyPartyReload>(cGuest);
        REQUIRE(cReloads.size() == 1);
        CHECK(cReloads[0]->Save == kNew);
    }
    CHECK(session.SentTo<NotifyPartyReload>(kHost).empty());

    session.Frame(16);
    CHECK(session.SentTo<NotifySaveChunk>(kPeer).empty());
    const auto cPieces = session.SentTo<NotifySaveChunk>(kOtherPeer);
    REQUIRE(cPieces.size() == 1);
    CHECK(cPieces[0]->Save == kNew);
    CHECK(cPieces[0]->Base == kOld);
    CHECK_FALSE(cPieces[0]->Ahead);
    CHECK(cPieces[0]->Total == session.SaveBytes);

    // Delivered once every guest has left to load it.
    CHECK_FALSE(session.Authority().CheckpointDelivered());
    session.Leave(kPeer);
    session.Leave(kOtherPeer);
    CHECK(session.Authority().CheckpointDelivered());
}

TEST_CASE("A guest told to load a save it lost says what it holds, and gets the save", "[save]")
{
    Session session;
    session.SaveImages = {kOld, kNew};
    session.Receive(kPeer, Held({kNew}));
    session.Authority().SendCheckpoint(kNew);
    session.Frame(16);
    REQUIRE(session.SentTo<NotifySaveChunk>(kPeer).empty());

    // Everything else of a guest's is dropped until the host's game has loaded.
    session.Receive(kPeer, Held({}));
    session.Frame(32);
    const auto cPieces = session.SentTo<NotifySaveChunk>(kPeer);
    REQUIRE(cPieces.size() == 1);
    CHECK(cPieces[0]->Save == kNew);
    CHECK(cPieces[0]->Base.empty());
    session.Leave(kOtherPeer);
    CHECK_FALSE(session.Authority().CheckpointDelivered());
}

TEST_CASE("A save the host makes goes ahead a piece an update, and a newer one takes its place", "[save]")
{
    Session session;
    session.SaveImages = {kOld, kNew};
    session.SaveBytes = 3 * NotifySaveChunk::kMaxData;
    session.Receive(kOtherPeer, Held({kOld}));

    session.Authority().OfferSave(kOld);
    session.Frame(16);
    session.Frame(32);
    // The guest that holds it gets nothing; the host's own game never does.
    CHECK(session.SentTo<NotifySaveChunk>(kOtherPeer).empty());
    CHECK(session.SentTo<NotifySaveChunk>(kHost).empty());
    auto pieces = session.SentTo<NotifySaveChunk>(kPeer);
    REQUIRE(pieces.size() == 2);
    CHECK(pieces[0]->Ahead);
    CHECK(pieces[0]->Offset == 0);
    CHECK(pieces[1]->Offset == NotifySaveChunk::kMaxData);

    session.Authority().OfferSave(kNew);
    session.Frame(48);
    pieces = session.SentTo<NotifySaveChunk>(kPeer);
    REQUIRE(pieces.size() == 3);
    CHECK(pieces[2]->Save == kNew);
    CHECK(pieces[2]->Offset == 0);
    REQUIRE(session.SentTo<NotifySaveChunk>(kOtherPeer).size() == 1);
    CHECK(session.SentTo<NotifySaveChunk>(kOtherPeer)[0]->Base == kOld);

    SECTION("it says it holds the save: no more of it comes")
    {
        session.Receive(kPeer, Held({kNew}));
        session.Frame(64);
        CHECK(session.SentTo<NotifySaveChunk>(kPeer).size() == 3);
    }
    SECTION("a host load of it takes over, whole and at once")
    {
        session.Authority().SendCheckpoint(kNew);
        session.Frame(64);
        pieces = session.SentTo<NotifySaveChunk>(kPeer);
        REQUIRE(pieces.size() == 6);
        for (size_t i = 3; i < 6; ++i)
        {
            CHECK_FALSE(pieces[i]->Ahead);
            CHECK(pieces[i]->Offset == (i - 3) * NotifySaveChunk::kMaxData);
        }
    }
}

TEST_CASE("A joining game gets the host's next save against one it holds", "[save][join]")
{
    Session session;
    session.SaveImages = {kOld, kNew};
    auto join = JoinRequest("Serana");
    join.PlayerKey = "id:0123456789abcdef";
    join.WantsWorld = true;
    join.HasCharacter = true;
    join.HeldSaves = {kOld};
    session.Receive(4, join);

    // The host's game has not made the save yet.
    session.Frame(16);
    CHECK(session.SentTo<NotifySaveChunk>(4).empty());
    session.WorldSave = kNew;
    session.Frame(32);
    const auto cPieces = session.SentTo<NotifySaveChunk>(4);
    REQUIRE(cPieces.size() == 1);
    CHECK(cPieces[0]->Save == kNew);
    CHECK(cPieces[0]->Base == kOld);
    CHECK_FALSE(cPieces[0]->Ahead);

    SECTION("a save the host's game cannot send refuses the join")
    {
        session.Outbox.clear();
        session.Receive(5, join);
        session.WorldSave = std::string{};
        session.Frame(48);
        const auto cResponses = session.SentTo<AuthenticationResponse>(5);
        REQUIRE_FALSE(cResponses.empty());
        CHECK(cResponses.back()->Type == AuthenticationResponse::ResponseType::kTransferFailed);
    }
}

TEST_CASE("A game still joining when the host loads says it lost the save, and gets it", "[save][join]")
{
    Session session;
    session.SaveImages = {kOld, kNew};
    session.WorldSave = kOld;
    auto join = JoinRequest("Serana");
    join.PlayerKey = "id:0123456789abcdef";
    join.WantsWorld = true;
    join.HasCharacter = true;
    join.HeldSaves = {kNew};
    session.Receive(4, join);
    session.Frame(16);
    REQUIRE(session.SentTo<NotifySaveChunk>(4).size() == 1);

    // Told to load kNew from its store; it is gone there.
    session.Authority().SendCheckpoint(kNew);
    const auto cReloads = session.SentTo<NotifyPartyReload>(4);
    REQUIRE(cReloads.size() == 1);
    session.Frame(32);
    REQUIRE(session.SentTo<NotifySaveChunk>(4).size() == 1);
    session.Receive(4, Held({}));
    session.Frame(48);
    const auto cPieces = session.SentTo<NotifySaveChunk>(4);
    REQUIRE(cPieces.size() == 2);
    CHECK(cPieces[1]->Save == kNew);
    CHECK(cPieces[1]->Base.empty());

    // The host loads only once that game left to load it.
    session.Leave(kPeer);
    session.Leave(kOtherPeer);
    CHECK_FALSE(session.Authority().CheckpointDelivered());
    session.Leave(4);
    CHECK(session.Authority().CheckpointDelivered());
}

TEST_CASE("A game sent a save against a base it lost meanwhile gets it against what it holds", "[save]")
{
    Session session;
    session.SaveImages = {kOld, kNew};
    session.Receive(kPeer, Held({kOld}));
    session.Authority().SendCheckpoint(kNew);
    session.Frame(16);
    REQUIRE(session.SentTo<NotifySaveChunk>(kPeer).size() == 1);
    CHECK(session.SentTo<NotifySaveChunk>(kPeer)[0]->Base == kOld);

    session.Receive(kPeer, Held({}));
    session.Frame(32);
    const auto cPieces = session.SentTo<NotifySaveChunk>(kPeer);
    REQUIRE(cPieces.size() == 2);
    CHECK(cPieces[1]->Base.empty());
}

TEST_CASE("A joining game waiting for a save the host's game cannot make yet is told, and again while it waits", "[save][join]")
{
    Session session;
    session.SaveImages = {kNew};
    auto join = JoinRequest("Serana");
    join.PlayerKey = "id:0123456789abcdef";
    join.WantsWorld = true;
    join.HasCharacter = true;
    session.Receive(4, join);

    // A save that works is made in a try or two: no notice for those.
    session.Frame(16);
    session.Frame(32);
    CHECK(session.SentTo<NotifySaveWaiting>(4).empty());
    uint64_t tick = 32;
    for (uint32_t i = 2; i < HostSaveTransfer::kWaitNoticeTries; ++i)
        session.Frame(tick += 16);
    CHECK(session.SentTo<NotifySaveWaiting>(4).size() == 1);
    for (uint32_t i = 0; i < HostSaveTransfer::kWaitNoticeEvery; ++i)
        session.Frame(tick += 16);
    CHECK(session.SentTo<NotifySaveWaiting>(4).size() == 2);

    // A slow host (a frame a second): told again within kWaitNoticeSeconds, before the guest's own wait runs out.
    for (uint32_t i = 0; i < HostSaveTransfer::kWaitNoticeSeconds; ++i)
        session.Frame(tick += 1000);
    CHECK(session.SentTo<NotifySaveWaiting>(4).size() == 3);

    session.WorldSave = kNew; // the conversation ended: the save comes
    session.Frame(tick += 16);
    CHECK(session.SentTo<NotifySaveChunk>(4).size() == 1);
}
