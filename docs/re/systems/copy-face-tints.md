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

The painted texture is named `SkyrimCoop face tints` (`NiTexture::name`, `+0x20`). `FaceGenSystem::IsPainted` reads
the current head's face material: painted when its tint texture carries that name. `CharacterService` paints a copy
with a `FaceGenComponent` when its tints are new (`FaceGenComponent::Painted`, cleared by a looks change) or its
head is not painted (built again). The harness snapshot's `face.painted` reads the head, not the flag.

Before this, `FaceGenComponent::Generated` was set once and never cleared by a 3D rebuild, so the copy kept the
first head's paint only.

Tests: GameTests "A player's copy keeps its painted face when its 3D is built again" (`[copy-looks]`): the fake's
actor drops its head tints with its 3D (`FakeEngine::DropHead`: `Unload3D`, a move to another cell, a disable, its
cell unloading).
