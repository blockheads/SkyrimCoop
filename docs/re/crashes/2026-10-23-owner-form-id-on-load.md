# 2026-10-23: host crash in HookSetOwnerForm while a reference loads (fixed)

**Symptom:** the host crashed in `dialogue-ends-on-travel` (a `coc` into the Sleeping Giant Inn), in
`GuestScripts::HookSetOwnerForm` reading `apOwner->formID` with `apOwner = 0x7`. Crash log
`prefix-host/.../SKSE/crash-2026-10-23-09-15-45.log`; stack: `ExtraDataList::SetOwnerForm` (11609) from 11595+0x14B7,
under `TESObjectREFR::Load` (19506) and `PlayerCharacter::CenterOnCell_Impl` (40437).

**Cause (IDA):** 11595 reads a reference's owner from its plugin (`TESFile::ReadData`) and passes the form id it read
to 11609 as the owner, unresolved. Here it was 0x7, the player's base: a bed owned by the player. The hook's quick
filter (`apOwner->formID >= 0xFF000000`) read through it.

**Fix:** the hook only compares `apOwner` with the guests' bases (a pointer comparison) and never reads through it.
Not re-run against the same `coc` yet: the scenarios after it passed on the next run, and `dialogue-ends-on-travel`
was among them.
