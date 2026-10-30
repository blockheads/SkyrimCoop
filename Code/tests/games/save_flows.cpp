// Saving in a session (SaveService; docs/design/join-flow.md, "Saving: only the host"): a guest's game refuses every
// save and its chat says why; the host's game saves.
#include "Party.h"
#include "Fixtures.h"

#include <Events/SessionNoticeEvent.h>
#include <Services/SaveService.h>

namespace
{


struct Notices
{
    explicit Notices(World& aGame) { aGame.GetDispatcher().sink<SessionNoticeEvent>().connect<&Notices::On>(*this); }
    void On(const SessionNoticeEvent& acNotice) { Lines.push_back(acNotice.Text); }
    std::vector<std::string> Lines;
};
} // namespace

SCENARIO("A guest's game refuses to save and says why; the host's saves; the guest saves again once it left", "[save]")
{
    Party party(1, SmokeWorld);
    World& host = party.Host();
    World& guest = party.Guest();
    Notices guestNotices(guest);

    bool saved = true;
    party.As(guest, [&] { saved = Party::Engine(guest).PlayerSaves("Quicksave0"); });
    THEN("the guest's save is refused, and its chat says only the host saves")
    {
        CHECK(!saved);
        CHECK(Party::Engine(guest).Saves.empty());
        REQUIRE(guestNotices.Lines.size() == 1);
        CHECK(guestNotices.Lines[0] == SaveService::kRefusal);
    }

    party.As(host, [&] { saved = Party::Engine(host).PlayerSaves("Save1"); });
    THEN("the host's game saves") CHECK((saved && Party::Engine(host).Saves == std::vector<std::string>{"Save1"}));

    WHEN("the guest leaves")
    {
        party.Leave(guest);
        party.As(guest, [&] { saved = Party::Engine(guest).PlayerSaves("Save2"); });
        THEN("its game saves again") CHECK((saved && Party::Engine(guest).Saves == std::vector<std::string>{"Save2"}));
    }
}

SCENARIO("A guest's game refuses the engine's autosaves and says why once a session", "[save]")
{
    Party party(1, SmokeWorld);
    World& guest = party.Guest();
    Notices guestNotices(guest);

    bool saved = true;
    party.As(guest, [&] { saved = Party::Engine(guest).Autosaves("Autosave1"); });
    party.As(guest, [&] { saved = saved || Party::Engine(guest).Autosaves("Autosave2"); });
    THEN("none is written, and its chat says so once")
    {
        CHECK(!saved);
        CHECK(Party::Engine(guest).Saves.empty());
        CHECK(guestNotices.Lines == std::vector<std::string>{SaveService::kRefusal});
    }

    WHEN("it leaves and joins again")
    {
        party.Leave(guest);
        party.Join(guest);
        party.As(guest, [&] { saved = Party::Engine(guest).Autosaves("Autosave3"); });
        THEN("its chat says so again") CHECK(guestNotices.Lines.size() == 2);
    }
}
