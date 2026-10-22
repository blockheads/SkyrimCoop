#pragma once

struct InputEvent;

/**
 * @brief The keyboard while the in-game UI takes it: the chat line.
 *
 * The game's input reaches it before the game (the input dispatch hook, BSInputDeviceManager.cpp).
 * Enter in gameplay opens the chat; while it is open the UI gets the key presses and the game gets
 * no input at all, so typing does not walk the player around. UiService turns the presses into text.
 */
namespace UiInput
{
// One frame's input chain: true when the UI took it (the game then gets none of it).
bool Take(const InputEvent* apChain) noexcept;

// For UiService, once a frame: whether Enter may open the chat (connected, the chat document up).
void SetChatAvailable(bool aAvailable) noexcept;
[[nodiscard]] bool IsChatOpen() noexcept;
void CloseChat() noexcept;

struct Key
{
    uint32_t ScanCode; // DirectInput
    bool Shift;
};
// The presses since the last call, while the chat is open.
[[nodiscard]] std::vector<Key> TakeKeys() noexcept;

// DirectInput scan codes the chat handles itself.
constexpr uint32_t cEscape = 0x01;
constexpr uint32_t cBackspace = 0x0E;
constexpr uint32_t cEnter = 0x1C;
constexpr uint32_t cNumpadEnter = 0x9C;
} // namespace UiInput
