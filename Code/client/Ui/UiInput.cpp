#include <Ui/UiInput.h>

#include <Games/Skyrim/BSInput/InputEvent.h>
#include <Games/Skyrim/Interface/UI.h>

namespace
{
constexpr uint32_t cLeftShift = 0x2A;
constexpr uint32_t cRightShift = 0x36;

bool s_available = false;
bool s_open = false;
bool s_shift = false;
std::vector<UiInput::Key> s_keys;

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
bool Take(const InputEvent* apChain) noexcept
{
    bool enter = false;
    for (const InputEvent* pEvent = apChain; pEvent; pEvent = pEvent->pNext)
    {
        if (pEvent->eventType != InputEvent::kButton || pEvent->device != InputEvent::kKeyboard)
            continue;
        const auto& cButton = *reinterpret_cast<const ButtonEvent*>(pEvent);
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
    return s_open;
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
