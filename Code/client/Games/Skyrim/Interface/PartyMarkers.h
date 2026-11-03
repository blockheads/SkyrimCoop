#pragma once
struct PartyMarkerSet;

namespace PartyMarkerDisplay
{
// The UI thread consumes a copy; no game pointers cross from the services.
void Set(const PartyMarkerSet& acMarkers) noexcept;
}
