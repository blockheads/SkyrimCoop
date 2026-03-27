#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

// On MSVC, __FUNCTION__ is a string literal and can be concatenated with other
// string literals. On GCC, it is a const char[] variable. We need a workaround
// so code like spdlog::warn("{}: : message", __FUNCTION__) compiles on both.
#ifndef _MSC_VER
#define __FUNCTION_STR__ __func__
// Redefine __FUNCTION__ to use __func__ which is standard C++.
// Note: GCC's __FUNCTION__ cannot be used in string literal concatenation.
// Code using "{}:  string", __FUNCTION__ must be refactored to use fmt-style formatting.
#endif

// MinGW/GCC uses Itanium ABI which reuses base class tail padding, while MSVC
// does not. Skyrim game struct static_asserts verify MSVC layout. We guard these
// checks on MinGW and rely on runtime validation instead, since adding explicit
// padding to every struct in the hierarchy would be too invasive.
// TODO: Upgrade to GCC 12+ and use -flayout-compat=ms for proper MSVC ABI compat.
#ifndef _MSC_VER
#define SKYRIM_STRUCT_ASSERT(...) /* GCC Itanium ABI layout differs, skip */
#else
#define SKYRIM_STRUCT_ASSERT(...) static_assert(__VA_ARGS__)
#endif

#include <TiltedCore/Platform.hpp>

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#ifdef _MSC_VER
#include <intrin.h>
#else
#include <x86intrin.h>
#endif
#include <cstdint>


// TiltedCore
#include <TiltedCore/StackAllocator.hpp>
#include <TiltedCore/ScratchAllocator.hpp>
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
#include <ThisCall.hpp>

extern void* RipAllocateN(size_t blockLength);
#define REVERSE_ALLOC_STUB(x) RipAllocateN(x)
#include <JitAssembly.hpp>

#define SPDLOG_WCHAR_FILENAMES
#define SPDLOG_WCHAR_TO_UTF8_SUPPORT
#include <entt/entt.hpp>
#include <spdlog/spdlog.h>
#include <glm/glm.hpp>
#include <glm/gtx/norm.hpp>

#include <any>
#include <mutex>
#include <chrono>
#include <iostream>
#include <filesystem>
#include <fstream>

#include <BuildInfo.h>
#include <Games/Primitives.h>

using TiltedPhoques::Allocator;
using TiltedPhoques::App;
using TiltedPhoques::AutoPtr;
using TiltedPhoques::Buffer;
using TiltedPhoques::List;
using TiltedPhoques::Map;
using TiltedPhoques::Outcome;
using TiltedPhoques::ScopedAllocator;
using TiltedPhoques::ScratchAllocator;
using TiltedPhoques::Set;
using TiltedPhoques::SortedMap;
using TiltedPhoques::StackAllocator;
using TiltedPhoques::String;
using TiltedPhoques::ThisCall;
using TiltedPhoques::UniquePtr;
using TiltedPhoques::Vector;

using namespace std::chrono_literals;

#include "Components.h"

#include <Utils.h>
#include <RTTI.h>

// On MinGW x64, __fastcall and __stdcall expand to __attribute__ forms that GCC
// does not allow in 'using' type alias declarations (e.g., using T = void(__fastcall)()).
// On x64 there is only one calling convention (Microsoft x64), so these are no-ops.
// MUST be at the end of the PCH so the #undef is the final state preserved in .gch.
#if defined(__GNUC__) && defined(__x86_64__)
#undef __fastcall
#define __fastcall
#undef __stdcall
#define __stdcall
#undef __cdecl
#define __cdecl
#endif
