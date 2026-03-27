-- Merged Tilted Libraries
-- This file consolidates TiltedConnect, TiltedHooks, TiltedReverse, and TiltedUI
-- into the main SkyrimCoop repository for easier debugging and maintenance

set_languages("cxx20")

-- Networking Library (formerly TiltedConnect)
target("SkyrimCoopNetworking")
    -- Use object library for Wine MSVC to bypass broken lib.exe archiver
    if is_plat("windows") and get_config("sdk") == "/opt/msvc" then
        set_kind("object")
    else
        set_kind("static")
    end
    set_group("Libraries")
    add_files("networking/*.cpp")
    add_includedirs("networking/", {public = true})
    add_headerfiles("networking/*.hpp")
    add_deps("TiltedCore")
    add_packages("enet6", "hopscotch-map", "snappy", "libuv", "spdlog")

    if is_plat("linux") then
        add_cxflags("-fPIC")
        set_symbols("debug")
        add_cxflags("-g")
    end

    -- For Windows, only add debug symbols if NOT using Wine MSVC
    -- Wine MSVC has issues with /DEBUG:FULL linker flags
    if is_plat("windows") and get_config("sdk") ~= "/opt/msvc" then
        set_symbols("debug")
        add_cxflags("/Zi")
        add_ldflags("/DEBUG:FULL")
    end

-- Reverse Engineering Library (formerly TiltedReverse)
target("SkyrimCoopReverse")
    -- Use object library for Wine MSVC to bypass broken lib.exe archiver
    if is_plat("windows") and get_config("sdk") == "/opt/msvc" then
        set_kind("object")
    else
        set_kind("static")
    end
    set_group("Libraries")
    add_files("reverse/*.cpp")
    add_includedirs("reverse/", {public = true})
    add_headerfiles("reverse/*.hpp", "reverse/*.inl")
    add_defines("NOMINMAX")
    add_deps("TiltedCore")
    add_packages("mimalloc", "hopscotch-map", "minhook", "mem", "xbyak")

    -- Ensure debug symbols
    if is_plat("windows") then
        set_symbols("debug")
    end

-- Hooks Library (formerly TiltedHooks)
target("SkyrimCoopHooks")
    -- Use object library for Wine MSVC to bypass broken lib.exe archiver
    if is_plat("windows") and get_config("sdk") == "/opt/msvc" then
        set_kind("object")
    else
        set_kind("static")
    end
    set_group("Libraries")
    add_files("hooks/*.cpp")
    add_files("hooks/DInputHook.cpp", {unity_ignored = true})
    add_includedirs("hooks/", {public = true})
    add_headerfiles("hooks/*.hpp")
    add_syslinks("dxguid", "dinput8", "d3d11")
    add_deps("TiltedCore", "SkyrimCoopReverse")
    add_packages("mimalloc", "hopscotch-map", "mem")

    -- Ensure debug symbols
    if is_plat("windows") then
        set_symbols("debug")
    end

-- CEF-dependent UI targets: Windows MSVC only, not MinGW (per D-03)
if not is_plat("mingw") then

-- UI Library (formerly TiltedUI) - Client-only, requires CEF
target("SkyrimCoopUI")
    -- Use object library for Wine MSVC to bypass broken lib.exe archiver
    if is_plat("windows") and get_config("sdk") == "/opt/msvc" then
        set_kind("object")
    else
        set_kind("static")
    end
    set_group("Libraries")

    -- Force MT runtime to match CEF
    if is_mode("releasedbg") or is_mode("release") then
        set_runtimes("MT")
        add_cxflags("/MT", {tools = {"cl"}, force = true})
    end

    add_files("ui/*.cpp")
    add_includedirs("ui/", {public = true})
    add_headerfiles("ui/*.hpp")
    add_syslinks("dxguid", "d3d11")
    add_deps("TiltedCore", "DirectXTK")
    add_packages("cef", "mimalloc", "hopscotch-map")
    add_defines("NOMINMAX")

    -- Ensure debug symbols
    if is_plat("windows") then
        set_symbols("debug")
    end

    -- Override any inherited runtime settings from dependencies
    after_load(function (target)
        if is_mode("releasedbg") or is_mode("release") then
            target:set("runtimes", "MT")
        end
    end)

-- UI Process Library (CEF render process) - Client-only, requires CEF
target("SkyrimCoopUIProcess")
    -- Use object library for Wine MSVC to bypass broken lib.exe archiver
    if is_plat("windows") and get_config("sdk") == "/opt/msvc" then
        set_kind("object")
    else
        set_kind("static")
    end
    set_group("Libraries")

    -- CEF is built with static runtime (/MT), so UiProcess must match
    if is_mode("releasedbg") or is_mode("release") then
        set_runtimes("MT")
        add_cxflags("/MT", {tools = {"cl"}, force = true})
    end

    add_files("ui_process/*.cpp")
    add_includedirs("ui_process/", {public = true})
    add_headerfiles("ui_process/*.hpp")
    add_deps("TiltedCore")
    add_packages("cef", "mimalloc", "hopscotch-map")
    add_defines("NOMINMAX")

    -- Ensure debug symbols
    if is_plat("windows") then
        set_symbols("debug")
    end

    -- Override any inherited runtime settings from dependencies
    after_load(function (target)
        if is_mode("releasedbg") or is_mode("release") then
            target:set("runtimes", "MT")
        end
    end)

end -- not is_plat("mingw")
