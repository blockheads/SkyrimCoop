set_xmakever("2.8.5")

-- Force parallel compilation across all targets
if set_policy then
    set_policy("build.across_targets_in_parallel", true)
end

if is_plat("mingw") then
    add_cxxflags("-Wa,-mbig-obj")

    -- Force MSVC-compatible struct packing and alignment for game structures
    -- WARNING: MinGW binaries may still not work with Skyrim due to ABI differences
    add_cxxflags("-mms-bitfields")  -- Use MSVC bitfield layout
    add_cxxflags("-fms-extensions")  -- Enable MSVC extensions

    -- Reduce debug info verbosity in debug mode for faster builds
    if is_mode("debug") then
        -- Use -g1 instead of -g (line numbers only, no local variables)
        add_cxflags("-g1")

        -- Additional compilation speed optimizations for debug builds
        add_cxxflags("-fno-var-tracking")  -- Disable variable tracking
        add_cxxflags("-fno-var-tracking-assignments")  -- Disable assignment tracking
        add_cxxflags("-fno-diagnostics-show-option")  -- Less verbose diagnostics

        -- Disable expensive optimizations in debug mode for faster compile
        add_cxxflags("-O0")  -- No optimization, fastest compile
        add_cxxflags("-fno-inline")  -- Don't inline functions
        add_cxxflags("-fno-defer-pop")  -- Simplify stack operations

        -- AGGRESSIVE LINKER OPTIMIZATIONS for debug mode (MUCH faster linking)
        add_ldflags("-Wl,--no-keep-memory", {force = true})
        add_ldflags("-Wl,--reduce-memory-overheads", {force = true})
        add_ldflags("-Wl,--no-undefined", {force = true})  -- Fail fast on undefined symbols
        add_ldflags("-Wl,--as-needed", {force = true})  -- Only link needed libraries
        add_ldflags("-Wl,--gc-sections", {force = true})  -- Remove unused sections
        add_cxxflags("-fdata-sections", {force = true})  -- Needed for --gc-sections
        add_cxxflags("-ffunction-sections", {force = true})  -- Needed for --gc-sections

        -- Disable expensive linker features in debug
        add_ldflags("-Wl,--no-relax", {force = true})  -- Skip relaxation optimization
        add_ldflags("-Wl,--hash-style=sysv", {force = true})  -- Faster hash style
    else
        -- For release builds, use optimization
        add_cxxflags("-O2")
    end

    -- Try to use LLD linker for faster linking (fallback to default ld if not available)
    -- Note: MinGW's ld doesn't support many linker optimization flags
    add_ldflags("-fuse-ld=lld", {try = true})
end

-- -- If newer version of xmake, remove ccache until it actually works
-- if set_policy ~= nil then
--     set_policy("build.ccache", false)
-- end

-- c code will use c99,
set_languages("c99", "cxx20")

-- Speed up template-heavy C++20 compilation (GCC/Clang only)
if is_mode("debug") and (is_plat("mingw") or is_plat("linux")) then
    add_cxxflags("-ftemplate-depth=256")  -- Reduce from default 900
    add_cxxflags("-fno-math-errno")  -- Don't set errno for math functions
    add_cxxflags("-fno-semantic-interposition")  -- Allow more aggressive optimizations
end

if is_plat("windows") then
    add_cxflags("/bigobj")
    add_syslinks("kernel32")
    set_arch("x64")
    set_runtimes("MT")  -- Use static runtime library (/MT for release, /MTd for debug)
    -- Ensure full PDB path is embedded for debugger to find symbols
    add_ldflags("/PDBALTPATH:%_PDB%", {force = true})
end

if is_plat("linux") then
    add_cxflags("-fPIC")
end

-- Disable warnings in debug mode for faster compilation (enable if you want warnings lol)
set_warnings("none")

add_vectorexts("sse", "sse2", "sse3", "ssse3")
add_vectorexts("neon")

-- build configurations
add_rules("mode.debug", "mode.releasedbg", "mode.release")

if has_config("unitybuild") then
    add_rules("c.unity_build")
    add_rules("c++.unity_build", {batchsize = 12})
end

-- direct dependencies version pinning
add_requires(
    "entt v3.10.0",
    "recastnavigation v1.6.0",
    -- tiltedcore is now built from Code/TiltedCore/ instead of external package
    "cryptopp 8.9.0",
    "spdlog v1.13.0",
    "cpp-httplib 0.14.0",
    "gtest v1.14.0",
    "glm 0.9.9+8",
    -- sentry-native 0.7.1,  -- Disabled: not compatible with MinGW cross-compilation
    "zlib v1.3.1",
    -- gamenetworkingsockets v1.4.1,  -- Disabled: not compatible with MinGW, using enet6 instead
    -- Merged library dependencies (formerly in submodules)
    "snappy 1.1.10",
    "rpmalloc",
    "hopscotch-map v2.3.1",
    "enet6",
    "libuv v1.48.0",
    "minhook v1.3.3",
    "xbyak v7.06",
    "catch2 2.13.9"
)
if is_plat("windows") then
    add_requires(
        "discord 3.2.1",
        "imgui v1.89.7",
        "directxtk 21.11.0",
        "cef 100.0.24"
    )
end

-- dependencies' dependencies version pinning
add_requireconfs("*.cmake", { version = "3.30.2", override = true })
add_requireconfs("*.openssl", { version = "1.1.1-w", override = true })
add_requireconfs("*.zlib", { version = "v1.3.1", override = true })
add_requireconfs("*.protobuf*", { version = "26.1", override = true })
add_requireconfs("**.abseil*", { version = "20250127.1", override = true })
if is_plat("linux") then
    add_requireconfs("*.libcurl", { version = "8.7.1", override = true })
end

add_requireconfs("cpp-httplib", {configs = {ssl = true}})
-- add_requireconfs("sentry-native", { configs = { backend = "crashpad" } })  -- Disabled
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

if is_plat("windows") then
    add_defines("NOMINMAX")
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
