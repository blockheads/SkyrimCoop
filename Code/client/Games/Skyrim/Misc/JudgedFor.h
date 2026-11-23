#pragma once

struct TESObjectREFR;

// Conditions judged for another player's character, as if it were the player: on this thread only, while a Scope is
// alive (the story manager's events for a character, a dialogue line for a guest, a spell that character cast). The hook
// on TESConditionItem::IsTrue (29924) rewrites what reads the player outside the subject and target (JudgedRules).
namespace JudgedFor
{
enum class Mode
{
    kAsThePlayer, // the params' subject or target that is the player becomes the character (a line, a story event)
    kCaster,      // a spell's effect cast by another player's character: the subject and target stay (conditions.md)
};

struct Scope
{
    explicit Scope(TESObjectREFR* apCharacter, Mode aMode = Mode::kAsThePlayer) noexcept;
    ~Scope() noexcept;
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

  private:
    TESObjectREFR* m_pPrevious;
    Mode m_previousMode;
};

// The character this thread judges for, or null.
[[nodiscard]] TESObjectREFR* Get() noexcept;
} // namespace JudgedFor
