#include <TiltedOnlinePCH.h>
#include "Commands.h"
#include "Harness.h"

#include <World.h>
#include <Bridges/IActorBridge.h>

// The player's own input, through the game's input dispatch as a device sends it.
namespace TestHarness
{
namespace
{
// {"cmd": "input", "button": "right" | "left" | "ready" | "sneak" | "forward", "down": true | false}: the player presses or
// releases the button. While it is down it is sent again every frame with the time held; the release
// carries the total.
nlohmann::json Input(Harness& aHarness, const nlohmann::json& acRequest)
{
    const auto cButton = PlayerInput::ButtonByName(acRequest.value("button", ""));
    if (!cButton)
        return Error("unknown button");
    const auto cHeld = aHarness.Input.Set(*cButton, acRequest.value("down", true));
    if (!cHeld)
        return Error("the input system is not up");
    return {{"ok", true}, {"held", *cHeld}};
}

// {"cmd": "interrupt", "caster": id, "source": 1}: ends a held (concentration) cast.
nlohmann::json Interrupt(Harness&, const nlohmann::json& acRequest)
{
    World::Get().GetActorBridge().InterruptCast(acRequest.value("caster", 0x14u), acRequest.value("source", 1u));
    return Ok();
}

constexpr Command cCommands[] = {
    {"input", true, &Input},
    {"interrupt", false, &Interrupt},
};
} // namespace

std::span<const Command> InputCommands()
{
    return cCommands;
}
} // namespace TestHarness
