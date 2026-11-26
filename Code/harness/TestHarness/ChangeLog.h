#pragma once

#include <nlohmann/json.hpp>

// Counts the engine's change records (a FormChanges watcher: TESForm::AddChange, 14607, and its forced variant
// 14608) by form type and change flags, for `coop_test.py change-rate`. Lock-free: the engine records changes
// from worker threads.
// Findings: docs/research/world-state-drift.md.
namespace TestHarness::ChangeLog
{
// Counts since the last reset: {"seconds", "buckets": [{type, flags, forced, calls, recorded, samples}],
// "forms": {type: distinct forms}, "full": the tables ran out of room}.
nlohmann::json Read();
void Reset();
} // namespace TestHarness::ChangeLog
