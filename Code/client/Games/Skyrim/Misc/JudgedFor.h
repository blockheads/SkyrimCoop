#pragma once

struct TESObjectREFR;

// Conditions judged for a guest's character, as if it were the player: on this thread only, while a Scope is alive
// (the story manager's events for a character, a dialogue line for a guest). The hook on TESConditionItem::IsTrue
// (29924) rewrites what reads the player outside the subject and target (JudgedRules).
namespace JudgedFor
{
struct Scope
{
    explicit Scope(TESObjectREFR* apCharacter) noexcept;
    ~Scope() noexcept;
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

  private:
    TESObjectREFR* m_pPrevious;
};

// The character this thread judges for, or null.
[[nodiscard]] TESObjectREFR* Get() noexcept;
} // namespace JudgedFor
