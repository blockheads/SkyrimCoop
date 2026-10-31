#include "Party.h"
#include "Fixtures.h"
#include <Events/AiStateChangeEvent.h>

SCENARIO("A native scene clears after its end event and the gameplay copies follow", "[ai][scene-clear]")
{
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    uint32_t merchant = 0;
    party.As(party.Host(), [&] { merchant = host.LoadNpc(0x13BA1, 0x31D2E, {}); });
    party.Until([&] { return guest.CopiesOf(0x13BA1).size() == 1; }, "the guest has Belethor");
    const uint32_t cCopy = guest.CopiesOf(0x13BA1)[0];
    party.As(party.Host(), [&] {
        host.Actors.at(merchant).Ai.SceneId = 0xC791A;
        party.Host().GetDispatcher().trigger(AiStateChangeEvent{merchant});
    });
    party.Until([&] { return guest.GetActorAiFields(cCopy).SceneId == 0xC791A; },
                "the owner's active scene is the copy's gameplay answer");
    CHECK(guest.Actors.at(cCopy).LocalScene == 0);
    party.As(party.Host(), [&] { host.EndScene(merchant); });
    CHECK(host.GetActorAiFields(merchant).SceneId == 0);
    party.Until([&] { return guest.GetActorAiFields(cCopy).SceneId == 0; },
                "the copy's effective scene follows the owner after the native event");
    CHECK(guest.Actors.at(cCopy).LocalScene == 0);
    party.Agree("the merchants agree after the scene ends");
}
