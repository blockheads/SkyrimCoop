# Lockpicking

Skyrim 1.7.104. Read in IDA unless marked measured.

## The path

```
Activate (19796 ActivateRef) a locked door or chest, as the player
  -> TESObjectDOOR::Activate (17922) / TESObjectCONT::Activate (17887)
       locked (REFR_LOCK flags & 1), not an owner, no key in the inventory
  -> LockpickingMenu::OpenMenu (51959)
       refused silently while UI::numDisablePauseMenu (UI+0x168, read by UI::IsPauseMenuDisabled 82076) > 0
       lock level 5 (requires key) or broken (12402): "DRSLocked" HUD message
       no lockpick (0xA) or skeleton key (default object 5): sOutOfLockpicks
       else target (406271) and difficulty set, kShow queued for "Lockpicking Menu"
  -> LockpickingMenu::ProcessMessage (51950, vtable slot 4)
       kShow (1): 51964
       kUpdate (0): once (+0x11C), and only when GetCurrentThreadId() == Main+0x28 (the main thread):
         Build3D (51960: RequestModel2 for the lock and pick, UI3DSceneManager), 51963, 51969, 51967, 51977
       kUserEvent (7) with the cancel event: kHide
  -> the pick succeeds: LockpickingMenu::Unlock (51968) unlocks (AddLockChange) and activates the target
```

An NPC with a lockpick unlocks the door outright (17922, after the player branch).

## Who sends kUpdate on which thread

`Main::Update` (36564 at +0xA9C) calls `UI::ProcessMessages` (82082) itself only in job mode 0, the paused game
(`coop_menuMode` 35406). Otherwise `Job_UI` (39042) runs `ProcessMessages`, `AdvanceMovies` and the console's queued
commands on a job thread. So a lockpicking menu with its pause flags cleared (`MenuPauseRules`) gets every kUpdate
off the main thread and never builds its lock: the menu is on the stack, the background blurred, nothing drawn and no
input bar (measured on the duo, host and guest, 2026-11-22 playtest).

Skyrim Souls RE (`MenuProcessing.cpp`) turns `Job_UI` off (+0xB) and calls `ProcessMessages`/`AdvanceMovies` from
`Main::Update`+0xAF1 for every menu. We do it for this menu only: `LiveMenuHooks.cpp` hooks 51950, and when a kUpdate
leaves the menu unbuilt it queues one kUpdate for `MainLoopTasks` (between frames on the main thread, no frame jobs
running), which builds it the way the paused game's kUpdate would.

## Our hooks on this path

- 51959 `HookLockOpen` (CrimeBridge): counts lock attempts for the guest's crime request.
- 51981 `HookLockCrime` (CrimeBridge): the guest's caught check goes to the host.
- 51968 `HookLockpickUnlock`: `ScopedPlayerLockOverride`, the lock sync tells the player's unlock from the world's.
- 51950 `LockpickProcess` (LiveMenuHooks): the main-thread build above.
