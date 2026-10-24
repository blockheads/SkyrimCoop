#include "Commands.h"
#include "Harness.h"
#include "EngineCalls.h"

#include <World.h>
#include <Bridges/IActorBridge.h>

// The player's own input, through the game's input dispatch as a device sends it.
namespace TestHarness
{
namespace
{
// {"cmd": "input", "button": "right" | "left" | "ready" | "sneak" | "forward" | "shout", "down": true | false}: the player presses or
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

// {"cmd": "key", "scan": 28, "down": true}: a keyboard key, by DirectInput scan code, as the keyboard sends it.
nlohmann::json Key(Harness&, const nlohmann::json& acRequest)
{
    if (!EngineCalls::SendKey(acRequest.value("scan", 0u), acRequest.value("down", true)))
        return Error("the input system is not up");
    return Ok();
}

// {"cmd": "char", "text": "Ri"}: typed characters for a text field, one CharEvent each.
nlohmann::json Char(Harness&, const nlohmann::json& acRequest)
{
    for (const char c : acRequest.value("text", std::string()))
        if (!EngineCalls::SendChar(static_cast<unsigned char>(c)))
            return Error("the input system is not up");
    return Ok();
}

// {"cmd": "mouse", "dx": 10, "dy": -5} moves the mouse; {"cmd": "mouse", "button": 0, "down": true} presses a button.
nlohmann::json Mouse(Harness&, const nlohmann::json& acRequest)
{
    const bool cSent = acRequest.contains("button")
                           ? EngineCalls::SendMouseButton(acRequest.value("button", 0u), acRequest.value("down", true))
                           : EngineCalls::SendMouseMove(acRequest.value("dx", 0), acRequest.value("dy", 0));
    return cSent ? Ok() : Error("the input system is not up");
}

// {"cmd": "input_state"}: what the control map does with keys now (EngineCalls::GetInputState).
nlohmann::json InputStateReadout(Harness&, const nlohmann::json&)
{
    const auto cState = EngineCalls::GetInputState();
    if (!cState)
        return Error("the input system is not up");
    return {{"ok", true}, {"contexts", cState->Contexts}, {"controls", cState->Controls}, {"text_entry", cState->TextEntry},
            {"ignore_keyboard_mouse", cState->IgnoreKeyboardMouse}};
}

// {"cmd": "interrupt", "caster": id, "source": 1}: ends a held (concentration) cast.
nlohmann::json Interrupt(Harness&, const nlohmann::json& acRequest)
{
    World::Get().GetActorBridge().InterruptCast(acRequest.value("caster", 0x14u), acRequest.value("source", 1u));
    return Ok();
}

constexpr Command cCommands[] = {
    {"input", true, &Input},
    {"key", false, &Key},
    {"mouse", false, &Mouse},
    {"char", false, &Char},
    {"input_state", false, &InputStateReadout},
    {"interrupt", false, &Interrupt},
};
} // namespace

std::span<const Command> InputCommands()
{
    return cCommands;
}
} // namespace TestHarness
