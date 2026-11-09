# A copy's face tints after its 3D is built again (2026-11-08)

Playtest 2026-11-08: players' copies had black faces after loading into a new area.

## The engine path (1.7.104, IDA names recorded)

1. A new area (or `Reset3D`) builds the actor's 3D again: `AIProcess::Update3DModel_Impl` (39395) calls
   `TESNPC::BuildHead` (24731; skee64's `PreprocessedHeads`), which makes a new `BSFaceGenNiNode` and with it new
   head parts and a new face material (`BSLightingShaderMaterialFacegen`).
2. `BuildHead` loads the preprocessed head (`TESNPC::LoadPreprocessedHead`, 24727: `GetFaceGeomPath` 24726, the
   FaceGeom nif) if `bUseFaceGenPreprocessedHeads` holds and the file exists, else builds the head at runtime from
   the race's head parts (`TESRace` 25366).
3. The face part's tint texture (`tintTexture`, material `+0xA0`) comes from the base's FaceTint .dds
   (`TESNPC::ApplyFaceTintFileToHeadPart` 24728, `TESNPC::GetFaceTintPath` 24708). The path is built from the root
   `faceNPC`'s plugin file: a temporary base (a player's copy, `FF` id) has none, so the texture is null: a black
   face, the vanilla "dark face" look.

`FaceGenSystem::Paint` renders the owner's tints into a texture (`CreateSourceTexture` 70717,
`ApplyMasksToRenderTarget` 27040) and sets it as that material's `tintTexture`. The material belongs to the head
it was painted on; a head built again has a new material without it.

## How the engine renders a face tint at runtime

A copy's head is always built at runtime (`TESRace::BuildRuntimeHead` 25366): there is no FaceGeom file for its
temporary base. Each head part then goes through `BSFaceGenManager::PrepareHeadPartForShaders` (26838). For the face
part with a Facegen material it makes a new render texture, renders tint masks into it (`ApplyMasksToRenderTarget`
27040), and sets it as the material's `tintTexture` (`+0xA0`). The masks it renders:

1. the player's base (`7`): `PlayerCharacter`'s tint masks (`+0xB20`);
2. a base with `TESNPC+0x244` set: the player's overlay tint masks (`+0xB60`, the vampire skin);
3. any other NPC, only with `bUseFaceGenPreprocessedHeads` off: `TESNPC::GetTintMasks` (`1403C8870`), the base's
   tint layers (`tintLayers`, `+0x260`) over its race's tint assets.

With the setting on (the default) an NPC is expected to have its FaceTint file, so step 3 is skipped and the texture
stays unrendered: the vanilla dark face. A copy lands there on every head build.

The engine's own way for a copy would be step 3: the owner's tints as the copy base's tint layers, with that one
check passed for copies' bases.

Measured on the duo (2026-11-09, `guest-changes-looks-in-session` with the snapshot's `base_layers`): the guest's own
player base has no tint layers (`tintLayers` null) after the race menu, while its `PlayerCharacter` holds 34 tint
masks; the host's copy's base has none either. The player's tints live only on `PlayerCharacter`, so the NPC record
coop sends cannot carry them. Step 3 would need coop to build the layers: one per race tint asset (`GetTintMasks`
gives an asset with no layer its default colour and alpha), matched by texture path, and a patch of the check inside
26838, which can run on a queued head's thread.

## Which head part is the face

`BuildHead` builds from the base's own head parts, unless `TESNPC::HasOverlays` (24790) holds: for an NPC,
`overlayRace` (`+0x1E8`) set and not its race (`+0x158`). Then it builds from the base overlays
(`GetBaseOverlays` 24791 / `GetNumBaseOverlays` 24792, a global map keyed by the base). A vampire keeps its old face
this way. The face node names its geometry after the part it was built with, so the face is looked up through
`TESNPC::GetCurrentHeadPart`, as RaceMenu (skee64 `GetHeadGeometry`) and CommonLibSSE-NG's
`GetCurrentHeadPartByType` do. Looking it up by the own part found no geometry on a vampire copy:
`guest-changes-looks-in-session` showed `geometry: False` after the vampire change. It had passed before only because
`Generated` stayed true from a paint of the old head.

## What coop does

Every head attach ends in `TESNPC::UpdateHeadState` (24724): `TESNPC::AttachHead` (24732: `Update3DModel_Impl`,
`QueuedHead`, `QueuedCharacter`, `ModifyFaceGen`) and 24740 call it after attaching the face node; worn changes
reach it through 24725. RaceMenu hooks the same calls to put its face overlays back (skee64
`UpdateHeadState_Enable_Hooked`). Coop hooks the function (`Games/Skyrim/HeadEvents.cpp`) and queues an
`ActorHeadUpdateEvent` (a queued head can finish off the main thread). `CharacterService::OnActorHeadUpdate` paints a
copy with a `FaceGenComponent` whose head is not painted; new tints are painted when they arrive (spawn, looks
change). A copy spawned before its 3D has no head to paint yet: its first head's update paints it.

The painted texture is named `SkyrimCoop face tints` (`NiTexture::name`, `+0x20`). `FaceGenSystem::IsPainted` reads
the current head's face material: painted when its tint texture carries that name, so a worn change leaves a painted
head alone. The harness snapshot's `face` reads the head (`painted`, and `head`: geometry, material, tint name) and
the paint count.

Before this, `FaceGenComponent::Generated` was set once and never cleared by a 3D rebuild, so the copy kept the
first head's paint only.

Tests: GameTests "A player's copy keeps its painted face when its 3D is built again" (`[copy-looks]`): the fake's
actor builds its head the first frame its 3D is there and raises the event (`FakeEngine::BuildHeads`); its head and
tints go with its 3D (`DropHead`: `Unload3D`, a move to another cell, a disable, its cell unloading). Without the
event's listener the flow fails.
