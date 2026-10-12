#pragma once

#include <nlohmann/json.hpp>

namespace TestHarness
{
struct Harness;

// What scripts/coop_test.py reads (status.json and the "status" command): this game's player, every
// loaded actor's full state for the desync checker, the copies it shows, the session and the frame.
nlohmann::json BuildStatus(const Harness& acHarness);
// The hot reload loader's reload count and last error; null without the loader (SKSE loaded us directly).
nlohmann::json ReloadStatus();
} // namespace TestHarness
