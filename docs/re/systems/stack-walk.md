# Stack walks: frames with no unwind data

Our tools capture call stacks in the running game (heap history, the lock-order checker, the harness traces, the
abort log). They all go through `CrashHandler::CaptureStack`, never `RtlCaptureStackBackTrace`. This says why.

How sure: **read** (in IDA, SKSE's source or Wine's), **measured** (a run), **inferred**.

## What went wrong

With `SKYRIMCOOP_HEAP_HISTORY` set, the host crashed 5 of 5 at the kill move of `spectator-watches-kill-move`
(4 on 2026-11-12, 1 on 2026-11-22): `c0000005` inside ntdll, under `HeapHistory::Capture`'s
`RtlCaptureStackBackTrace`. Measured.

```text
SKSE's camera stub        no module, no unwind data (0x6ffffc26003c in that run)
 └ TESCamera::SetState 33026 +0x1f
    the old state's End() (vfunc 2); FirstPersonState::OnExitState tail-jumps on
    └ PlayerControls::UnregisterHandler 42357
       PlayerControls is walking its handlers (+0xA0): the removal is queued (array at +0x88)
       └ BSTArrayBase::PreparePush 68163, the array's first push
          └ BSTArrayHeapAllocator::Allocate 68168
             └ MemoryManager::Allocate 68115 -> HookFormAllocate -> the capture
```

Read in IDA (1.7.104; names and comments are in the database).

## SKSE's stubs

SKSE replaces the first instruction of `TESCamera::SetState` (33026, 5 bytes) with a branch to code it writes at run
time in its local trampoline, to send `SKSECameraEvent` (`skse64/Hooks_Camera.cpp`, read):

```text
push r12; push r13; sub rsp,28h
mov r12,rcx; mov r13,[rcx+28h]
mov [rsp],rbx
call [SetState+5]          returns at stub+0x19
...InstallCameraHook(old, new)...
add rsp,28h; pop r13; pop r12; ret
```

- It has a frame of its own (two pushes and 0x28 bytes) and no unwind data.
- It is the second stub in that trampoline: the first (the Papyrus delay-functor queue, `Hooks_Papyrus.cpp`) is 0x23
  bytes, so a walk under `SetState` sees `trampoline + 0x23 + 0x19 = +0x3c`. The run's frame was `...26003c`. Read
  (the sizes) and measured (the address).
- So every camera state change has this frame on the stack, not only a kill move.
- SKSE writes five more stubs that call out: the delay-functor queue and four Scaleform menu hooks
  (`Hooks_Scaleform.cpp`). Read, not seen on a stack.

## Wine's walk

`RtlCaptureStackBackTrace` is `RtlWalkFrameChain` (Wine 10, `dlls/ntdll/signal_x86_64.c`; the duo runs Proton 10).
Read:

- A frame with no function entry is taken for a leaf: `Rip = *Rsp; Rsp += 8` (`RtlVirtualUnwind2`,
  `dlls/ntdll/unwind.c`).
- Nothing checks `Rsp` before the read, and nothing guards it. The frame is checked against the thread's stack only
  after the unwind.
- Under SKSE's stub `*Rsp` is the saved `rbx`, not a return address. The walk goes on from junk: a value that lands
  in some function's unwind data restores `Rsp` from a junk register, and the next read faults. Inferred (the fault
  address is in ntdll's unwinder; which junk value did it is unread).

Windows' own walk was not read.

## Our walk

`CrashHandler::CaptureStack(frames, max, skip)` (`Code/client/CrashHandler.cpp`): `RtlCaptureContext`, then the walk
the crash handler and the stack sampler already used.

- A frame with no unwind data ends the walk, and is the last frame kept (only the innermost frame may be a leaf).
- Each unwind runs under `__try`; the vectored handler lets those faults through.
- So a stack under a camera change ends at SKSE's stub: who changed the camera is not in it. Registering unwind data
  for the stub (`RtlAddFunctionTable`) would give every walker the rest (docs/ideas.md).

Checked by the harness's `stack_walk_check` (a stub of the same shape, `stack-walk-stops-at-stub`): the walk returns
its caller and the stub's return address, nothing more. With `"ntdll": true` it runs the system's walk instead.
Measured 2026-11-22 on the host, the same call both ways:

```text
ours    0x6ffff8a71c19  0x1b0010 (the stub's return)
ntdll   0x6ffff8a71bfe  0x1b0010  0x8000000000000000  0x11e8e0  0x6ffff937f2a0  0x11dfff  0x10000000011dcd0
```

ntdll's five frames past the stub are guesses (stack addresses, a non-address): it did not fault that time, as it
did under the kill move.

After the change, with heap history on: `spectator-watches-kill-move` passed 4 of 4 (it crashed 5 of 5).
