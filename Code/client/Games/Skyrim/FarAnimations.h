#pragma once

struct TESObjectREFR;

// A host's trap beside a guest, out of the host's camera's sight: its graph runs on (docs/design/traps.md).
namespace FarAnimations
{
// The host's script animated aObject: its graph is updated for a while whatever the camera sees.
void Hold(TESObjectREFR& aObject) noexcept;
} // namespace FarAnimations
