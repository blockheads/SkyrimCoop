#pragma once

#include <cstdint>
#include <vector>

struct NiAVObject;
struct TESForm;
struct TESObjectREFR;

// The props an actor's graph shows (ANIO forms, Alvor's hammer): AnimationObjects (singleton 400328), filled by the
// graph's AnimObjLoad/AnimObjDraw triggers (42888/42889) and the save (43580). docs/re/systems/animation-objects.md.
namespace AnimationObjects
{
struct Held
{
    uint32_t AnimObject = 0; // the ANIO's form id
    bool Drawn = false;      // shown (AnimObjDraw), not only loaded
    bool operator==(const Held&) const noexcept = default;
};

// What the reference holds now.
[[nodiscard]] std::vector<Held> Of(const TESObjectREFR& acRef) noexcept;

// Holds one as the save's load does (43580): Load (43571), then Draw (43572) if drawn. No-op if already held.
void Hold(TESObjectREFR& aRef, const Held& acHeld) noexcept;
} // namespace AnimationObjects
