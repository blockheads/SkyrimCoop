set_xmakever("2.8.5")

-- Build requirements:
-- Minimum: 4GB RAM, 2 CPU cores
-- Recommended: 16GB RAM, 8+ CPU cores
-- XMake auto-detects cores for parallel jobs (-j)
-- For constrained machines: xmake -j4 (or lower)

-- No compile cache: xmake's ignores precompiled header contents, so editing a PCH header left
-- every object stale (verified with a probe symbol).
if set_policy ~= nil then
    set_policy("build.ccache", false)
end

-- c code will use c99,
set_languages("c99", "cxx20")


if is_plat("linux") then
    add_cxflags("-fPIC")
end

set_warnings("all")
add_vectorexts("sse", "sse2", "sse3", "ssse3")
add_vectorexts("neon")

-- build configurations
add_rules("mode.debug", "mode.releasedbg", "mode.release", "mode.dev")

if is_plat("windows") then
    -- clang-cl targeting the MSVC ABI from Linux: configure through scripts/xmake-msvc.sh,
    -- which puts build/toolchains/msvc-bin first in PATH (clang-cl, lld-link shims) and
    -- passes --sdk=build/toolchains/winsysroot (Windows SDK + MSVC CRT from xwin).
    -- Engine classes are MSVC-compiled: only an MSVC-ABI compiler gets their vtable and
    -- struct layouts right (see docs/re/systems/compiler-abi.md).
    set_arch("x64")
    add_defines("_WIN32_WINNT=0x0A00", "WINVER=0x0A00", "NOMINMAX")
    -- spdlog 1.13's bundled fmt trips newer Clang's consteval checks; disable fmt's
    -- compile-time format-string validation (formatting itself is unchanged).
    add_defines("FMT_CONSTEVAL=")
    -- Static CRT like upstream: the plugin carries no VC++ runtime DLL dependency.
    set_runtimes("MT")
    set_exceptions("cxx")
    add_cxflags("/bigobj", "/Zc:__cplusplus", {tools = {"clang_cl", "cl"}})
    -- Package CMake builds: Ninja instead of the Visual Studio generator.
    set_policy("package.cmake_generator.ninja", true)
end

-- dev mode: the client DLL's edit-build-test loop. NDEBUG like releasedbg, -O1, and debug info
-- that links fast (a PDB from lld-link; -g1 elsewhere, which keeps file:line in backtraces).
rule("mode.dev")
    on_config(function (target)
        if is_mode("dev") then
            target:set("optimize", "fast")
            if target:is_plat("windows") then
                -- clang-cl: PDB debug info; lld-link writes it quickly.
                target:set("symbols", "debug")
            else
                target:add("cxflags", "-g1")
            end
            target:add("defines", "NDEBUG")
        end
    end)
rule_end()

if has_config("unitybuild") then
    add_rules("c.unity_build")
    add_rules("c++.unity_build", {batchsize = 12})
end

-- direct dependencies version pinning
add_requires(
    "entt v3.10.0",
    "spdlog v1.13.0",
    "glm 0.9.9+8",
    "rpmalloc",
    "hopscotch-map v2.3.1",
    "snappy 1.1.10",
    "enet6",
    "libuv v1.48.0",
    "nlohmann_json v3.11.3"
)

-- The SKSE plugin's (mem is header-only, vendored in Code/external/mem).
if is_plat("windows") then
    add_requires("minhook v1.3.3", "xbyak v7.06")
    -- The in-game UI (Code/client/Ui): HTML/CSS-like documents, FreeType fonts.
    add_requires("rmlui 6.2", {configs = {freetype = true}})
    -- MSVC treats __movsb as a builtin; clang-cl only declares it in <intrin.h>.
    add_requireconfs("minhook", {configs = {cflags = "/FIintrin.h"}})
    -- Match the static CRT in every package.
    add_requireconfs("*", {configs = {runtimes = "MT"}})
end

-- The native tests.
if is_plat("linux") then
    add_requires("catch2 2.13.9")
end

-- dependencies' dependencies version pinning
add_requireconfs("*.rpmalloc", { override = true })
add_requireconfs("*.cmake", { version = "3.30.2", override = true })
add_requireconfs("*.zlib", { version = "v1.3.1", override = true })

before_build(function (target)
    import("modules.version")
    local branch, commitHash = version()
    bool_to_number={ [true]=1, [false]=0 }
    local contents = string.format([[
    #pragma once
    #define IS_MASTER %d
    #define IS_BRANCH_BETA %d
    #define IS_BRANCH_PREREL %d
    ]], 
    bool_to_number[branch == "master"], 
    bool_to_number[branch == "bluedove"], 
    bool_to_number[branch == "prerel"])

    -- fix always-compiles problem by updating the file only if content has changed.
    local filepath = "build/BranchInfo.h"
    local old_content = nil
    if os.exists(filepath) then
        old_content = io.readfile(filepath)
    end
    if old_content ~= contents then
        print("Updating file:", filepath)
        io.writefile(filepath, contents)
    end
end)

if is_mode("debug") then
    add_defines("DEBUG")
end


-- The test harness scripts/coop_test.py drives (Code/client/TestHarness/) and the hot reload
-- (Code/client/HotReload/, Code/hotreload/Loader.cpp). Off by default: a player's build has neither
-- the command socket nor the control files. scripts/xmake-msvc.sh turns it on.
option("test_harness")
    set_default(false)
    set_showmenu(true)
    set_description("Build the test harness and hot reload into the client (never for release)")
option_end()

includes("Code")
