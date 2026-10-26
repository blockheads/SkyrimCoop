#include <Events/PlayerDeathEvent.h>
#include <World.h>
#include <Ui/UiInput.h>

#include <Games/Skyrim/BSInput/InputEvent.h>
#include <Games/Skyrim/Interface/FxDelegateArgs.h>
#include <Games/Skyrim/Interface/IMenu.h>
#include <Games/Skyrim/Interface/UI.h>

namespace
{
constexpr uint32_t cLeftShift = 0x2A;
constexpr uint32_t cRightShift = 0x36;
constexpr uint32_t cLeftArrow = 0xCB;
constexpr uint32_t cRightArrow = 0xCD;

constexpr uint32_t cLeftButton = 0;
constexpr uint32_t cRightButton = 1;

bool s_available = false;
bool s_spectating = false;
bool s_open = false;
bool s_shift = false;
std::vector<UiInput::Key> s_keys;
bool s_buttons = false;
std::vector<bool> s_clicks;
bool s_escape = false;
// Gameplay: nothing up but the HUD (no menu, console, dialogue or loading screen takes the keys).
bool OnlyHudOpen() noexcept
{
    UI* pUI = UI::Get();
    if (!pUI)
        return false;
    for (IMenu* pMenu : pUI->menuStack)
    {
        const BSFixedString* pName = pUI->LookupMenuNameByInstance(pMenu);
        if (!pName || !pName->Is("HUD Menu"))
            return false;
    }
    return true;
}
} // namespace

namespace UiInput
{
bool IsHudVisible() noexcept
{
    return GetHudVisibility().AllowsParty();
}

Ui::HudVisibility GetHudVisibility() noexcept
{
    Ui::HudVisibility state;
    UI* pUI = UI::Get();
    if (!pUI)
        return state;
    state.MenuSystemVisible = pUI->menuSystemVisible;
    state.HudMenuOpen = pUI->GetMenuOpen("HUD Menu");
    IMenu* pHud = state.HudMenuOpen ? pUI->FindMenuByName("HUD Menu") : nullptr;
    state.MovieReady = pHud && pHud->uiMovie;
    for (const IMenu* pMenu : pUI->menuStack)
        if (pMenu && pMenu != pHud &&
            (pMenu->uiMenuFlags &
             (IMenu::kPausesGame | IMenu::kUsesMenuContext | IMenu::kModal | IMenu::kApplicationMenu)))
            state.BlockingMenu = true;

    if (!state.MovieReady)
        return state;

    // HUDMenu.ShowElements sets Health._visible from the native HUD mode. The health meter's
    // own damage/regen animation is inside this clip, so full player health does not hide our
    // party notifications. Read primitive booleans only; no managed Scaleform values are retained.
    const auto read = [&](const char* pPath) -> std::optional<bool> {
        GFxValue value{};
        if (pHud->uiMovie->GetVariable(&value, pPath) && (value.type & GFxValue::kTypeMask) == GFxValue::kBoolean)
            return value.value.boolean;
        return std::nullopt;
    };
    state.RootVisible = read("_root._visible");
    state.HudClipVisible = read("_root.HUDMovieBaseInstance._visible");
    state.HealthClipVisible = read("_root.HUDMovieBaseInstance.Health._visible");
    // A replacement HUD without these clips still follows the engine's global/menu visibility.
    return state;
}

bool Take(const InputEvent* apChain) noexcept
{
    bool enter = false;
    bool buttons = false;
    for (const InputEvent* pEvent = apChain; pEvent; pEvent = pEvent->pNext)
    {
        if (pEvent->eventType != InputEvent::kButton)
            continue;
        const auto& cButton = *reinterpret_cast<const ButtonEvent*>(pEvent);
        if (s_spectating && !s_open && !s_buttons && OnlyHudOpen())
        {
            // A downed player's spectator cycle: a left click or the right arrow the next standing player, the others back.
            int direction = 0;
            if (pEvent->device == InputEvent::kMouse && (cButton.idCode == cLeftButton || cButton.idCode == cRightButton))
                direction = cButton.idCode == cLeftButton ? 1 : -1;
            if (pEvent->device == InputEvent::kKeyboard && (cButton.idCode == cLeftArrow || cButton.idCode == cRightArrow))
                direction = cButton.idCode == cRightArrow ? 1 : -1;
            if (direction)
            {
                if (cButton.IsPress())
                    World::Get().GetRunner().Trigger(SpectateCycleEvent{direction});
                buttons = true;
                continue;
            }
        }
        if (s_buttons && pEvent->device == InputEvent::kMouse && cButton.idCode == cLeftButton)
        {
            if (cButton.IsPress() || cButton.IsRelease())
                s_clicks.push_back(cButton.IsPress());
            buttons = true;
            continue;
        }
        if (pEvent->device != InputEvent::kKeyboard)
            continue;
        if (s_buttons && !s_open && cButton.idCode == cEscape)
        {
            s_escape |= cButton.IsPress();
            buttons = true;
            continue;
        }
        if (cButton.idCode == cLeftShift || cButton.idCode == cRightShift)
            s_shift = !cButton.IsRelease();
        if (!cButton.IsPress())
            continue;
        if (s_open)
            s_keys.push_back({cButton.idCode, s_shift});
        else if (cButton.idCode == cEnter || cButton.idCode == cNumpadEnter)
            enter = true;
    }

    // A menu taking over (a dialogue, a loading screen) closes the chat and gets its input.
    const bool cGameplay = OnlyHudOpen();
    if (s_open && (!s_available || !cGameplay))
        CloseChat();
    if (!s_open && enter && s_available && cGameplay)
        s_open = true;
    return s_open || buttons;
}

void SetButtonsShown(bool aShown) noexcept
{
    s_buttons = aShown;
    if (!aShown)
    {
        s_clicks.clear();
        s_escape = false;
    }
}

std::vector<bool> TakeClicks() noexcept
{
    return std::exchange(s_clicks, {});
}

bool TakeEscape() noexcept
{
    return std::exchange(s_escape, false);
}

void SetSpectatorAvailable(bool aAvailable) noexcept
{
    s_spectating = aAvailable;
}

void SetChatAvailable(bool aAvailable) noexcept
{
    s_available = aAvailable;
}

bool IsChatOpen() noexcept
{
    return s_open;
}

void CloseChat() noexcept
{
    s_open = false;
    s_keys.clear();
}

std::vector<Key> TakeKeys() noexcept
{
    return std::exchange(s_keys, {});
}
} // namespace UiInput
