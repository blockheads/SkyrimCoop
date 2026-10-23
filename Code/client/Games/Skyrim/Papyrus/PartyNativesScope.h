#pragma once

#include <Events/PartyNativeEvent.h>
#include <Misc/BSScript.h>
#include <Papyrus/PartyNatives.h>

#include <cstdint>
#include <optional>

namespace PartyNatives
{
// A controls call running on a thread: whose holds it makes, and what it asked ControlMap for.
struct ControlsCall
{
    uint32_t Holder = 0;
    uint32_t Asked = 0;
};

// Around a native's call in a stack run for aPlayer (GuestScripts' cause: a guest, this game's player 0x14, a mirrored
// tag, 0 for none) and maybe the story: a party call runs here, and after it goes to the other games
// (PartyNativeEvent). A controls call is held for its game's cutscene: giving back gives back only what no other
// game's cutscene still holds here.
class Scope
{
  public:
    Scope(BSScript::IFunction* apFunction, BSScript::Stack* apStack, uint32_t aPlayer, bool aStory) noexcept;
    ~Scope() noexcept;

    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

    [[nodiscard]] bool IsParty() const noexcept
    {
        return m_event.has_value();
    }

  private:
    uint32_t m_cause = 0;
    bool m_story = false;
    std::optional<ControlsCall> m_controls;
    ControlsCall* m_pOuter = nullptr;
    std::optional<PartyNativeEvent> m_event;
};
} // namespace PartyNatives
