# Party markers

Every other player in the session appears on the native compass and maps. The party roster supplies names and
identities; the displayed player copies supply positions. No extra network traffic is needed. The shared destination
waypoint remains independent.

`PartyMarkerService` publishes a complete UI snapshot on each game update. Only roster members with valid characters
are included; this game's player and NPCs are excluded. Departure, disconnect and teardown remove markers. The UI
thread takes a copy under a short mutex and releases it before calling the engine.

## Native display

- The compass keeps its normal quest, enemy, location and destination markers. Immediately before its native
  `SetCompassMarkers` call, party locations are appended to its Scaleform array as blue destination-style icons.
  The following native angle update positions them. Vanilla rebuilds the array next frame, so departed markers
  disappear without persisting anything in the game.
- The world and local maps have a separate child movie containing blue, named party pins. Native camera projection
  places them as the map pans and zooms. Pins are disabled for interaction; they neither fast travel nor replace the
  player's destination. The child movie is rebuilt only when membership or its native parent changes.
- Names remain visible without hovering. Vanilla keeps an unhovered marker's `TextClip` at zero opacity even when
  its text field is visible; the label renderer makes that container opaque. The custom group leaves the native
  reserved hover depth free, so hovering an ordinary location cannot replace the party group.
- Compass/local-map coordinates are compared only within the same worldspace, or the same interior cell. World-map
  positions apply the native child-world scale and offset. Party members sharing an interior use its location's
  exterior marker on the world map; the local map uses their actual positions. Unavailable projections are hidden.
- Native menu and HUD visibility applies to these movies; the feature creates no new window or input handler.

## References and bindings

Community sources were inspected before reversing:

- [Compass Navigation Overhaul](https://github.com/alexsylex/CompassNavigationOverhaul), `include/Hooks.h` and
  `source/Hooks.cpp`: `Compass::Update` 51668 and the native Scaleform marker pipeline.
- [CoMAP](https://github.com/Exit-9B/MapMarkerFramework): native map/compass icon assets and menu integration.
- [Vanilla UI source](https://github.com/Mardoxx/skyrimui), `Map/MapMenu.as`, `Map/MapMarker.as` and `HUDMenu.as`:
  marker arrays, creation, positioning and labels.
- CommonLibSSE-NG: `GFxMovie`, `MapMenu`, `LocalMapMenu`, `TESWorldSpace` and `BGSLocation` layouts.

The actual 1.7.104 executable and legacy IDA database agree on `Compass::Update+0x128`: a direct call to
`GFxValue::ObjectInterface::Invoke` 82256 for `SetCompassMarkers`. The patch checks the opcode and destination before
installing. The map update is 53111. Camera/world/location fields have typed offset assertions; world-map coordinate
conversion uses 20496. Scaleform values obtained from the engine release their managed references with 82270.

## Verification

`GameTests [party-markers]` covers three players, movement, departure/rejoin, local-player exclusion and coexistence
with the shared destination. All three flows fail with the marker service omitted. `ClientTests [party-markers]`
checks bearings, north wrap, undefined directions and separation of unrelated coordinate spaces.

An additional GameTests flow renders each game's received names through the real Scaleform label code on
`FakeMapMovie`, which reproduces the native transparent text container. It failed four visibility assertions before
the fix. The native map scenarios check the actual text and its ancestor's opacity, and wait for local-map readiness.

Native flows cover compass, outdoor/interior world and local maps, movement with the map open, and departure/rejoin
on both games. [Acceptance results and screenshots](../../playtests/2026-11-02-party-markers.md) record the runtime
checks alongside the existing shared-waypoint regression.
