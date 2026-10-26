#pragma once

#include <ui_models/HudVisibility.h>

struct InputEvent;

/**
 * @brief The keyboard and mouse while the in-game UI takes them: the chat line, the join panel's buttons.
 *
 * The game's input reaches it before the game (the input dispatch hook, BSInputDeviceManager.cpp).
 * Enter in gameplay opens the chat; while it is open the UI gets the key presses and the game gets
 * no input at all, so typing does not walk the player around. UiService turns the presses into text.
 */
namespace UiInput
{
// Native HUD/menu visibility, including the health widget's HUD mode (not its damage fade).
[[nodiscard]] bool IsHudVisible() noexcept;
[[nodiscard]] Ui::HudVisibility GetHudVisibility() noexcept;

// One frame's input chain: true when the UI took it (the game then gets none of it).
bool Take(const InputEvent* apChain) noexcept;

// For UiService, once a frame: whether Enter may open the chat (connected, the chat document up).
void SetChatAvailable(bool aAvailable) noexcept;
void SetSpectatorAvailable(bool aAvailable) noexcept;
[[nodiscard]] bool IsChatOpen() noexcept;
void CloseChat() noexcept;

struct Key
{
    uint32_t ScanCode; // DirectInput
    bool Shift;
};
// The presses since the last call, while the chat is open.
[[nodiscard]] std::vector<Key> TakeKeys() noexcept;

// For UiService, once a frame: whether the join panel shows its buttons (Copy, Back). While it does, the left
// mouse button and Escape go to the UI and not to the game's menus.
void SetButtonsShown(bool aShown) noexcept;
// The left button's presses (true) and releases (false) since the last call, while the buttons show.
[[nodiscard]] std::vector<bool> TakeClicks() noexcept;
// Escape was pressed since the last call, while the buttons show (Back, where no cursor is up).
[[nodiscard]] bool TakeEscape() noexcept;

// DirectInput scan codes the chat handles itself.
constexpr uint32_t cEscape = 0x01;
constexpr uint32_t cBackspace = 0x0E;
constexpr uint32_t cEnter = 0x1C;
constexpr uint32_t cNumpadEnter = 0x9C;
} // namespace UiInput
