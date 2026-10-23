#pragma once

#include <NoCopyMove.h>

// What the client's services take from the client's precompiled header (Code/client/ClientPch.h, Windows only).
#include <Wire/Buffer.h>
#include <Wire/Serialization.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <format>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <BuildVersion.h>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <network/ENetInterface.h>
#include <spdlog/spdlog.h>

using namespace std::chrono_literals;

using Net::ConnectionId_t;
