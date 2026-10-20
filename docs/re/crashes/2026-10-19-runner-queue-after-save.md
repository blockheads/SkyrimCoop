# 2026-10-19: host crash draining the Runner queue, 2 s after a save (open)

**Symptom:** the host crashed at 20:22:38, 2 s after the second of two console saves 5.6 s apart
(`coop_test.py save-diff --noise 5`, the first run of the world-state drift research). Access violation
in `TiltedPhoques::TaskQueue::Drain` (`std::queue::front` on the local queue it swapped out of
`RunnerService::m_runner`), from `RunnerService::OnUpdate` / `World::Update` / `HookVMUpdate`, on the main
thread. The local queue's offset was garbage that reads like text (`rcx = 0x006320222020026A`), so the
`TaskQueue` the swap took it from was most likely overwritten before the swap.

Crash log: `build/coop-test/prefix-host/.../SKSE/crash-2026-10-19-20-22-38.log`. Breadcrumbs: our save
buffer hooks (`BGSSaveFormBuffer_WriteFormId`/`WriteId`, 36048/36047) ran last 1.9 s before, which is the
save itself; they only change behaviour inside our own `ScopedSaveLoadOverride`.

## What is ruled out

- Not a lock race on the queue: `Drain` swaps under the mutex, every `Add` takes it, nothing calls the
  `Unsafe*` variants.
- Not saving as such: six more host saves in a row (fresh duo) did not crash, and no earlier crash log
  in either prefix has this signature.

## Open

- What wrote over `m_runner`. Next time: `repro` with `--heap-history` and `--watch` on the
  RunnerService's address, with saves as the recipe.
