#pragma once

#include <NoCopyMove.h>

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
#include <cstdint>


// Standard library
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
#include <numbers>
#include <TaskQueue.h>
#include <Wire/Buffer.h>
#include <Hooks/Initializer.h>
#include <Wire/Serialization.h>

// Hooks
#include <Hooks/FunctionHook.h>

extern void* RipAllocateN(size_t blockLength);
#define REVERSE_ALLOC_STUB(x) RipAllocateN(x)
#include <Hooks/JitAssembly.h>

#define SPDLOG_WCHAR_FILENAMES
#define SPDLOG_WCHAR_TO_UTF8_SUPPORT
#include <entt/entt.hpp>
#include <spdlog/spdlog.h>
#include <glm/glm.hpp>
#include <glm/gtx/norm.hpp>

#include <algorithm>
#include <any>
#include <array>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <mutex>
#include <chrono>
#include <iostream>
#include <filesystem>
#include <fstream>

#include <BuildVersion.h>
#include <Games/Primitives.h>

using Wire::Buffer;

using namespace std::chrono_literals;

#include "Components.h"

#include <Utils.h>
#include <RTTI.h>
