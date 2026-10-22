#pragma once

// Compiled once and forced into every file (Code/client/xmake.lua): third-party and standard headers, and our
// own that rarely change. Every other header of ours is included where it is used, so editing one rebuilds
// only its users; editing one listed here rebuilds the whole plugin.

#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <intrin.h>

#include <algorithm>
#include <any>
#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <numbers>
#include <optional>
#include <queue>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#define SPDLOG_WCHAR_FILENAMES
#define SPDLOG_WCHAR_TO_UTF8_SUPPORT
#include <entt/entt.hpp>
#include <spdlog/spdlog.h>
#include <glm/glm.hpp>
#include <glm/gtx/norm.hpp>

using namespace std::chrono_literals;

// Ours, rarely edited.
#include <NoCopyMove.h>
#include <BuildVersion.h>
#include <VersionDb.h>
#include <Games/Primitives.h>
#include <RTTI.h>
#include <Wire/Buffer.h>
#include <Wire/ViewBuffer.h>
#include <Wire/Serialization.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <Hooks/JitAssembly.h>
#include <TaskQueue.h>
