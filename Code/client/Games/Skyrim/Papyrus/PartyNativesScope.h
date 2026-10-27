#pragma once

#include <Events/RoutedCallEvent.h>
#include <Misc/BSScript.h>
#include <Papyrus/PartyNatives.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace PartyNatives
{
// A controls call running on a thread: whose holds it makes, and what it asked ControlMap for.
struct ControlsCall
{
    uint32_t Holder = 0;
    uint32_t Asked = 0;
};

// Around a native's call in a stack run for aCause (GuestScripts' cause: a guest, this game's player 0x14, a mirrored
// tag, 0 for none) and maybe the story: where it runs (PartyRules::RouteOf, docs/design/party-calls.md). On the host's
// game, a guest's world change; on the game of the guest whose action ran it, a native on the player; or here and on
// every other game (RoutedCallEvent), after it ran. A controls call is held for its game's cutscene: giving back gives
// back only what no other game's cutscene still holds here.
class Scope
{
  public:
    Scope(BSScript::IFunction* apFunction, BSScript::Stack* apStack, BSScript::IVirtualMachine* apVm, uint32_t aCause,
          bool aStory) noexcept;
    ~Scope() noexcept;

    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

    // The native ran here with aResult in aStack: a latent party call's original keys its copies by its stack (the
    // host's box), and another game's latent call here starts waiting for its original's result.
    void Ran(BSScript::CallResult aResult, uint32_t aStackId) noexcept;

    // A question with no player's action is put to these players, in order (rule 3; PartyRules::PlayersToAsk): a
    // yes/no one until one says yes. Empty: asked as called.
    [[nodiscard]] const std::vector<uint32_t>& Asks() const noexcept
    {
        return m_asks;
    }

    // The native does not run here: it went to another game, or runs on this game's player instead.
    [[nodiscard]] std::optional<BSScript::CallResult> Result() const noexcept
    {
        return m_result;
    }

  private:
    uint32_t m_cause = 0;
    bool m_story = false;
    std::optional<ControlsCall> m_controls;
    ControlsCall* m_pOuter = nullptr;
    std::optional<RoutedCallEvent> m_event;
    std::optional<LocalCall> m_onThisPlayer;
    std::optional<BSScript::CallResult> m_result;
    bool m_progressed = false; // another game's progression call runs here: this player's record changes
    std::vector<uint32_t> m_asks;

    [[nodiscard]] PartyRules::CallFacts FactsOf(BSScript::IFunction* apFunction, const BSScript::Stack& acStack) const noexcept;
};
} // namespace PartyNatives
