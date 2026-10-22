#pragma once

#include <NoCopyMove.h>

// What the host's code (Code/client/Services/Host/) takes from the client's precompiled header.
#include <Wire/Serialization.h>
#include <deque>
#include <list>
#include <map>
#include <memory>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <Wire/ViewBuffer.h>

#include <chrono>
#include <cstdint>
#include <optional>

#include <BuildVersion.h>
#include <network/ENetInterface.h>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <spdlog/spdlog.h>

using namespace std::chrono_literals;

using Net::ConnectionId_t;
using Wire::ViewBuffer;
