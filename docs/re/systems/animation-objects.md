# Animation objects: props an idle holds (2026-11-16)

A prop an NPC holds in a furniture idle (Alvor's forge hammer, Hilde's tanning dagger, Embry's lean) is an
animation object (ANIO, `TESObjectANIO`, form type 0x53), not an equipped item. All read in IDA, 1.7.104, unless
marked measured.

## The engine path

```
idle's first clip starts (a clip generator's triggers in the behaviour, e.g. IdleBlacksmithForgeEnter)
  -> graph event AnimObjLoad, payload = the ANIO's editor id
       AnimationObjectLoadHandler::Handle (42888): TESForm::LookupByEditorID, formType 0x53
       -> AnimationObjects::Load (43571): adds entry {ref handle, ANIO, visible=false} unless that pair
          is there, queues the model (QueuedAnimationObject)
  -> graph event AnimObjDraw, same payload
       AnimationObjectDrawHandler::Handle (42889)
       -> AnimationObjects::Draw (43572): finds the pair Load made; model attached: clears its
          NiAVObject hidden bit (+0xF4 bit 0); still loading: sets visible (+0x20); no entry: nothing
idle ends: the ANIO's unloadEventName (+0x60) as a graph event
  -> TESObjectREFR::ProcessEvent (20102, its last case) -> AnimationObjects::OnUnloadEvent (43577): detach, remove
the reference's 3D goes (TESObjectREFR::Release3DRelatedData, 19302)
  -> AnimationObjects::RemoveAllFor (43573): every entry of that handle removed, no detach
```

- `AnimationObjects` (singleton 400328): `+0x08` an inline array of 0x28-byte entries (data `+0x08`, capacity
  `+0x10`, size `+0x18`), `+0x20` a 0/1 spin flag both Load and Draw take. Entry: handle `+0x00`, ANIO `+0x08`,
  attached model `+0x10`, queued load `+0x18`, visible `+0x20` (`Games/Skyrim/AnimationObjects.cpp`). CommonLibSSE-NG
  calls the array `BSTArray<BSTSmartPointer<AnimationObject>>`; Load's stride (`40 * i`) says inline entries.
- **Drawn** is not entry `+0x20` alone: Draw on an attached model clears the model's kHidden and leaves `+0x20` false.
  Save (43579) writes `visible || (attached && !(attached->flags & 1))`, and `AnimationObjects::IsDrawn` matches it.
- **Saves:** global data 0x3EA. `AnimationObjects::Save` (43579) writes ref, ANIO and drawn (above) per entry;
  `AnimationObjects::LoadGame` (43580) calls Load, then Draw if visible. A loaded game shows its props without
  replaying any trigger.
- **When the triggers fire** (measured, duo trace 2026-11-16, Riverwood): the tanning idle fires AnimObjLoad/Draw
  again at each `00NextClip`, so a copy without the dagger has it within one loop. Alvor's forge idle and Embry's
  lean fire them only as the idle starts.

## Why copies lacked them

A copy's graph replays its owner's stream from a sync point (`docs/design/graph-input-stream.md`). The triggers are the
graph's own output, so a copy already on the stream as its owner enters an idle gets them (measured: Alvor's tanning
idle, both games `[468903, drawn]`). A copy that starts later, such as after its 3D loads again (the party left and came
back, RemoveAllFor emptied it) or for a guest arriving, loads its owner's graph mid-idle through Havok's restore. No trigger
runs, so the copy has no prop. Measured on the duo: host Alvor `[[830991, loaded], [830992, drawn]]` at his forge,
his copy `[]`, for as long as he stays (`copy_holds_furniture_prop`).

## The fix

Props now come with the sync point, the way a save's load restores them. The owner's point carries
`AnimationObjects::Of(holder)` (`Payloads::GraphSyncPoint::AnimObjects`). Once the copy's replayer loads the point, it
calls `IGraphTarget::HoldAnimObjects`, which runs `AnimationObjects::Hold`: Load (43571), then Draw (43572) if drawn,
as 43580 does. Load ignores a pair that's already there. After that, the copy's stream runs the owner's later triggers
and unload events itself.

Readout: snapshot `state.anim_objects` (ANIO, drawn), compared owner against copy. The MCP's `expect(check="props")`
checks every copy against its owner.
