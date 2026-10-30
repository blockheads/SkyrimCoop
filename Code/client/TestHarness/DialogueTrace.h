#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>

namespace TestHarness
{
// Records this game's conversation each time it changes (menu, speaker's session id, list shown, options, highlight,
// line said), stamped with the wall clock: coop_test.py compares both games' records after every scenario
// (shared-dialogue.md, every menu shows the party's list). Always on. Game thread.
struct DialogueTrace
{
    // What was recorded since the last take, in order.
    nlohmann::json Take();
    // Once a frame.
    void Poll();

private:
    nlohmann::json m_last;
    nlohmann::json m_entries = nlohmann::json::array();
};
} // namespace TestHarness
