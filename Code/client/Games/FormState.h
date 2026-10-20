#pragma once

#include <cstdint>

struct TESForm;

// A form's state as a save holds it: what TESForm::SaveGame writes for some change flags, with form ids as
// network ids so another game with the same mods reads it. Engine side: docs/re/systems/form-state.md.
namespace FormState
{
// What acForm would write to a save for aChangeFlags. False while a save or load runs.
bool Save(const TESForm& acForm, uint32_t aChangeFlags, String& aData) noexcept;
// Loads acData into aForm as the engine loads a reference's changes when its cell attaches, under its
// loading bit. False while a save or load runs.
bool Load(TESForm& aForm, uint32_t aChangeFlags, const String& acData) noexcept;
} // namespace FormState
