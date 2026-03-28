set_xmakever("2.8.5")

-- Build requirements:
-- Minimum: 4GB RAM, 2 CPU cores
-- Recommended: 16GB RAM, 8+ CPU cores
-- XMake auto-detects cores for parallel jobs (-j)
-- For constrained machines: xmake -j4 (or lower)

-- Enable ccache in devbuild mode for fast rebuilds (D-04)
-- Other modes keep ccache disabled (historical XMake compatibility)
if set_policy ~= nil then
    if is_mode("devbuild") then
        set_policy("build.ccache", true)
    else
        set_policy("build.ccache", false)
    end
end

-- c code will use c99,
set_languages("c99", "cxx20")


if is_plat("linux") then
    add_cxflags("-fPIC")
end

if is_plat("mingw") then
    -- MSVC-compatible struct layout (required for Skyrim game struct ABI compatibility)
    add_cxflags("-mms-bitfields")
    -- Allow implicit function-pointer-to-void* conversions (MSVC allows this, GCC strict)
    add_cxflags("-fpermissive")
    -- On x64, all calling conventions are the same (MS x64 ABI). See
    -- Code/client/TiltedOnlinePCH.h for __fastcall/__stdcall/__cdecl undefs.
    set_arch("x86_64")
    add_defines("_WIN32_WINNT=0x0A00", "WINVER=0x0A00")
    add_defines("NOMINMAX")
    add_syslinks("kernel32")

    if is_mode("devbuild") then
        -- Devbuild: no blanket -static (shared internal libs need dynamic linking)
        -- Keep static GCC runtime to avoid libgcc/libstdc++ DLL deps
        add_cxflags("-static-libgcc", "-static-libstdc++")
        add_ldflags("-static-libgcc", "-static-libstdc++", {force = true})
        -- lld: 2-5x faster than GNU ld for PE/COFF (D-01, mold cannot produce PE/COFF)
        -- -B adds .toolchain/bin/ to GCC's search path (for ld.lld symlink)
        -- -fuse-ld=lld tells GCC to look for "ld.lld" explicitly (collect2 ignores -B for linker resolution)
        local toolbin = "-B" .. path.join(os.projectdir(), ".toolchain", "bin") .. "/"
        add_ldflags(toolbin, "-fuse-ld=lld", {force = true})
        add_shflags(toolbin, "-fuse-ld=lld", {force = true})
        -- Split DWARF: debug info in separate .dwo files, reduces archive sizes by ~96% (D-02)
        add_cxflags("-gsplit-dwarf", {force = true})
        -- Big object files: -O0 with debug info creates PE/COFF objects >65535 sections
        add_cxflags("-Wa,-mbig-obj", {force = true})
        -- Allow duplicate GCC runtime symbols when linking against devbuild shared DLLs
        add_ldflags("-Wl,--allow-multiple-definition", {force = true})
        -- Note: --gdb-index is ELF-only, not supported for PE/COFF targets
    else
        -- Non-devbuild: full static linking (no runtime DLL deps)
        add_cxflags("-static", "-static-libgcc", "-static-libstdc++")
        add_ldflags("-static", "-static-libgcc", "-static-libstdc++", {force = true})
    end
end

-- Helper: conditionally build as shared DLL in devbuild mode (D-03)
-- Uses --export-all-symbols to avoid invasive dllexport annotations
function devbuild_shared()
    if is_mode("devbuild") and is_plat("mingw") then
        set_kind("shared")
        set_prefixname("")  -- Windows DLL naming (no "lib" prefix)
        add_shflags("-static-libgcc", "-static-libstdc++", {force = true})
        add_shflags("-Wl,--export-all-symbols", {force = true})
        -- Allow duplicate GCC runtime symbols across shared DLLs
        add_shflags("-Wl,--allow-multiple-definition", {force = true})
        -- lld is picked up globally via -B.toolchain/bin/ (ld symlink -> lld-18)
    else
        set_kind("static")
    end
end

set_warnings("all")
add_vectorexts("sse", "sse2", "sse3", "ssse3")
add_vectorexts("neon")

-- build configurations
add_rules("mode.debug", "mode.releasedbg", "mode.release", "mode.devbuild")

-- Custom devbuild mode: fastest iteration speed (per D-06)
-- Usage: xmake f -p mingw -m devbuild
rule("mode.devbuild")
    on_config(function (target)
        if is_mode("devbuild") then
            -- No optimization for fastest compile (D-06)
            target:set("optimize", "none")
            -- Full debug symbols (will be split via -gsplit-dwarf)
            target:set("symbols", "debug")
            -- Assertions stay active: we simply don't add NDEBUG
            -- (other modes like releasedbg add it via their own rules)
        end
    end)
rule_end()

if has_config("unitybuild") then
    add_rules("c.unity_build")
    if is_mode("devbuild") then
        -- Smaller batches in devbuild: one file change recompiles fewer files (D-05)
        add_rules("c++.unity_build", {batchsize = 4})
    else
        -- Large batches for CI/release: faster clean builds
        add_rules("c++.unity_build", {batchsize = 12})
    end
end

-- direct dependencies version pinning
add_requires(
    "entt v3.10.0",
    "recastnavigation v1.6.0",
    -- tiltedcore is now built from Code/TiltedCore/ instead of external package
    "cryptopp 8.9.0",
    "spdlog v1.13.0",
    -- cpp-httplib is manually included from Code/external/cpp-httplib (bypasses xmake package check)
    "gtest v1.14.0",
    "glm 0.9.9+8",
    "zlib v1.3.1",
    -- Merged library dependencies (formerly in submodules)
    "rpmalloc",
    "hopscotch-map v2.3.1",
    "snappy 1.1.10",
    -- enet6: Replaced GameNetworkingSockets with enet6 for simpler networking
    "enet6",
    "libuv v1.48.0",
    "catch2 2.13.9"
)

-- Windows/MinGW-only packages (Tier 3 client dependencies)
if is_plat("windows") or is_plat("mingw") then
    add_requires("minhook v1.3.3", "xbyak v7.06")
    -- mem is header-only; unsupported on mingw in xmake-repo, so only require for MSVC
    if not is_plat("mingw") then
        add_requires("mem 1.0.0")
    end
end

-- dependencies' dependencies version pinning
add_requireconfs("*.rpmalloc", { override = true })
add_requireconfs("*.cmake", { version = "3.30.2", override = true })
add_requireconfs("*.openssl", { version = "1.1.1-w", override = true })
add_requireconfs("*.zlib", { version = "v1.3.1", override = true })
if is_plat("linux") then
    add_requireconfs("*.libcurl", { version = "8.7.1", override = true })
end

-- cpp-httplib is manually vendored in Code/external/cpp-httplib to bypass xmake package issues
--[[
add_requireconfs("magnum", { configs = { sdl2 = true }})
add_requireconfs("magnum-integration",  { configs = { imgui = true }})
add_requireconfs("magnum-integration.magnum",  { configs = { sdl2 = true }})
add_requireconfs("magnum-integration.imgui", { override = true })
--]]

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


-- add projects
-- Libraries are now merged into Code/libraries/
includes("Code")

task("upload-symbols")
    on_run(function ()
        import("core.base.option")

        local key = option.get('key')
        local linux = option.get('linux')

        if key ~= nil then
            import("net.http")
            import("core.project.config")

            config.load()

            local sentrybin = path.join(os.projectdir(), "build", "sentry-cli.exe")
            if not os.exists(sentrybin) then 
                http.download("https://github.com/getsentry/sentry-cli/releases/download/2.0.2/sentry-cli-Windows-x86_64.exe", sentrybin)
            end

            if linux then
                -- linux server bins
                local file_path = path.join(os.projectdir(), "build", "linux", "x64", "SkyrimTogetherServer.debug")
                os.execv(sentrybin, {"--auth-token", key, "upload-dif", "-o", "together-team", "-p", "st-server", file_path})

                file_path = path.join(os.projectdir(), "build", "linux", "x64", "libSTServer.debug")
                os.execv(sentrybin, {"--auth-token", key, "upload-dif", "-o", "together-team", "-p", "st-server", file_path})
            end

            -- windows bins
            if not linux then
                local file_path = path.join(os.projectdir(), "build", config.get("plat"), config.get("arch"), config.get("mode"), "SkyrimTogether.pdb")
                os.execv(sentrybin, {"--auth-token", key, "upload-dif", "-o", "together-team", "-p", "st-reborn", file_path})

                file_path = path.join(os.projectdir(), "build", config.get("plat"), config.get("arch"), config.get("mode"), "SkyrimTogetherServer.pdb")
                os.execv(sentrybin, {"--auth-token", key, "upload-dif", "-o", "together-team", "-p", "st-server", file_path})

                file_path = path.join(os.projectdir(), "build", config.get("plat"), config.get("arch"), config.get("mode"), "STServer.pdb")
                os.execv(sentrybin, {"--auth-token", key, "upload-dif", "-o", "together-team", "-p", "st-server", file_path})
            end

        else
            print("An API key is required to proceed!")
        end
    end)

    set_menu {
        usage = "xmake upload-symbols",
        description = "Upload symbols to sentry",
        options = {
            {'k', "key", "kv", nil, "The API key to use." },
            {'l', "linux", "v", false, "Upload linux symbols that were manually copied." },
        }
    }
