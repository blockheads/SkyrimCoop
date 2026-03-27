set_xmakever("2.8.5")


-- If newer version of xmake, remove ccache until it actually works
if set_policy ~= nil then
    set_policy("build.ccache", false)
end

-- c code will use c99,
set_languages("c99", "cxx20")


if is_plat("linux") then
    add_cxflags("-fPIC")
end

if is_plat("mingw") then
    -- MinGW uses GCC-style flags, not MSVC
    -- Static linking prevents missing libgcc/libstdc++ DLLs at runtime (see PITFALLS.md)
    add_cxflags("-static", "-static-libgcc", "-static-libstdc++")
    add_ldflags("-static", "-static-libgcc", "-static-libstdc++", {force = true})
    set_arch("x86_64")
    add_defines("_WIN32_WINNT=0x0A00", "WINVER=0x0A00")
    add_defines("NOMINMAX")
    add_syslinks("kernel32")
end

set_warnings("all")
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
    "minhook v1.3.3",
    "xbyak v7.06",
    "catch2 2.13.9"
)

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
