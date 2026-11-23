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

- Read: `Main::Update` (36564 at +0xA9C) calls `UI::ProcessMessages` (82082) itself only when `coop_menuMode(0)`
  (35406) is false; `Job_UI` (39042) also runs `ProcessMessages`, `AdvanceMovies` and the console's queued commands.
  51950 builds the lock only when `GetCurrentThreadId() == Main+0x28` (`cmp [rdi+11Ch]`; 51960 sets it).
- Inferred: an unpaused game (our cleared pause flags, `MenuPauseRules`) is the nonzero job mode, so the menu's
  kUpdate comes from `Job_UI` on a job thread. `lock-order.md` ties the render step's event-2 wait to job mode 1 only;
  the other modes are unread.
- Measured (duo, host and guest, 2026-11-22 playtest): with the pause flags cleared the menu is on the stack with
  `built3D` false, the background blurred, nothing drawn and no input bar; with one main-thread kUpdate it builds and draws.

Skyrim Souls RE (`MenuProcessing.cpp`) turns `Job_UI` off (+0xB) and calls `ProcessMessages`/`AdvanceMovies` from
`Main::Update`+0xAF1 for every menu. We do it for this menu only: `LiveMenuHooks.cpp` hooks 51950 (1.7.104 builds
only: +0x11C was read there), and when a kUpdate leaves the menu unbuilt it queues one kUpdate for `MainLoopTasks`.
Those run from `HookMainLoop` before `Main::Update` (the same place a journal save runs, `pausing-menus.md`); that no
UI job runs then is inferred from `Main::Update` waiting for its frame jobs (35409) before it returns.

## Our hooks on this path

- 51959 `HookLockOpen` (CrimeBridge): counts lock attempts for the guest's crime request.
- 51981 `HookLockCrime` (CrimeBridge): the guest's caught check goes to the host.
- 51968 `HookLockpickUnlock`: `ScopedPlayerLockOverride`, the lock sync tells the player's unlock from the world's.
- 51950 `LockpickProcess` (LiveMenuHooks): the main-thread build above.
