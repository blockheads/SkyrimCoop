#pragma once

#include <cstdint>

struct TESForm;

// Every change the save system is told of: TESForm::AddChange (14607, vfunc 0x0A; overrides tail-call it) and its
// forced form (14608), the only callers of the change funnel, and the change map's remove (35511), a record losing
// flags (RemoveChange 14609: a door closed again; a reference demoted on a long wait). docs/design/world-state-sync.md, "As built".
namespace FormChanges
{
// Runs on the engine's threads, after the change: aRecorded is false while a load runs (unless forced). Must not block.
using Watcher = void (*)(const TESForm& acForm, uint32_t aFlags, bool aForced, bool aRecorded) noexcept;

// False: every slot is taken.
bool Watch(Watcher aWatcher) noexcept;
void Unwatch(Watcher aWatcher) noexcept;
} // namespace FormChanges
