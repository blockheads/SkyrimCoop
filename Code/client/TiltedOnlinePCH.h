#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <TiltedCore/Platform.hpp>

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <intrin.h>
#include <cstdint>


// TiltedCore
#include <TiltedCore/Filesystem.hpp>
#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Outcome.hpp>
#include <TiltedCore/ViewBuffer.hpp>
#include <TiltedCore/Math.hpp>
#include <TiltedCore/TaskQueue.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Initializer.hpp>
#include <TiltedCore/Serialization.hpp>

// TiltedReverse
#include <AutoPtr.hpp>
#include <App.hpp>
#include <FunctionHook.hpp>
#include <Entry.hpp>
#include <Debug.hpp>

extern void* RipAllocateN(size_t blockLength);
#define REVERSE_ALLOC_STUB(x) RipAllocateN(x)
#include <JitAssembly.hpp>

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

using TiltedPhoques::Allocator;
using TiltedPhoques::App;
using TiltedPhoques::AutoPtr;
using TiltedPhoques::Buffer;
using TiltedPhoques::Outcome;

using namespace std::chrono_literals;

#include "Components.h"

#include <Utils.h>
#include <RTTI.h>
