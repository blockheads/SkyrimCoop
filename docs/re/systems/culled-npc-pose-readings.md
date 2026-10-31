# Culled NPC pose readings (2026-10-30)

The combined copy-desync run repeatedly reported Lydia's owner head81–88 versus her copy122,
although both recorded JumpLand and their saved state machines agreed. This is a stale skeleton
reading on the off-screen owner, not evidence that the copy refused that action.

Native controls, with the original failures preserved:

- Owner and copy GetScale both1.00, base1.03.
- Reloading the owner's existing graph snapshot into the copy succeeds (202–222us), but its
  head remains122. Reloading alone does not correct this reading.
- Turning only the owner's camera toward Lydia changes its head88.08→124.12 and pelvis52.84→71.12.
  Its recorded JumpLand/action/idle/state flags do not change. The copy remains near122/69,
  within the existing20-unit pose tolerance.
- The owner still has `process_me=true`; the earlier frozen-owner hypothesis does not explain this case.

Evidence is ignored: `build/coop-test/pose-reload-native.log`, `pose-camera-native.log`,
`build/pose-reload-probe.json`, and the exact per-comparison samples in
`build/coop-test/desync-samples.jsonl`. Probe loads/camera changes happen only after preserving
the original failed comparison; they never convert that run into a pass.

The fake now models the distinction: an animation can finish while an off-screen skeleton
retains its last drawn pose. `[culled-pose]` reproduces the same88/124 false desync and fails
before the observer correction. Its camera-visible step exposes the current standing pose.
The comparison unit suite fails three tests first; visible pose mismatches, player body checks,
and animation differences on off-screen NPCs are explicitly retained.

The observer now compares NPC bone poses only when both are visible. It still compares all
other actor properties, including off-screen animation markers, names, pools and equipment.
Player body checks retain their previous scope. Missing visibility metadata retains the old
comparison behavior. No tolerance increases or animation allowlists are introduced.

Read-only harness metadata uses CommonLibSSE-NG's `Actor::BOOL_BITS::kWasInFrustum` (bit21),
plus the low culling nibble read by Precision's HighActorCuller hook and `lastSeenTime`.
Sources: `reference/CommonLibSSE-NG/include/RE/A/Actor.h` and
`reference/plugins/Precision/src/Hooks.cpp`. The AE actor culling field is typed at27C and
last-seen field264, both offset-asserted. The final-build calibration confirms the mapping: owner head83.24 has cull6/frustum=false,
copy head122.40 has cull7/frustum=true; the qualified comparison is empty and dialogue-peer-starts passes
(`build/pose-visibility-acceptance-sample.json`). No gameplay hook or bridge interface is added.

The gait sampler also reads skeleton nodes; its earlier off-screen results need visible-sample
controls before diagnosing sliding. This change does not declare those gait results fixed.
