#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct BShkbAnimationGraph;

namespace TestHarness
{
// Records what watched actors' animations do, frame by frame, for coop_test.py's animation trace: every
// event their behavior graphs send, the actions their graphs take, life state, kill moves, health and
// where they stand. Stamped with the wall clock (both games on one machine line up) and this game's frame.
// Game thread, except the graph events (sent where the graphs update).
struct AnimationTrace
{
    // Starts recording these actors (adds them when already recording), and these graph booleans of each.
    void Watch(const std::vector<uint32_t>& acActors, const std::vector<std::string>& acGraphBools);
    // What was recorded since the last take, in order. Keeps recording.
    nlohmann::json Take();
    // Stops, takes what is left, and lets go of the graphs.
    nlohmann::json Stop();
    // Once a frame.
    void Poll();
    bool Recording() const noexcept { return !m_actors.empty(); }

private:
    struct Watched
    {
        nlohmann::json State;
        std::string Action;
    };

    void Update();
    void ListenToGraphs(uint32_t aFormId);
    void Sample(uint32_t aFormId, Watched& aWatched);

    std::map<uint32_t, Watched> m_actors;
    std::vector<std::string> m_graphBools;
    nlohmann::json m_game;
    // The graphs listened to, each held (its reference) until let go, so it outlives our sink on it.
    std::vector<BShkbAnimationGraph*> m_graphs;
};
} // namespace TestHarness
