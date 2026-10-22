-- Merged Tilted Libraries
-- This file consolidates TiltedConnect, TiltedHooks and TiltedReverse
-- into the main SkyrimCoop repository for easier debugging and maintenance

set_languages("cxx23")

-- Networking (formerly TiltedConnect): the enet transport, the host's listener and a guest's connection
target("SkyrimCoopNetworking")
    set_kind("static")
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

    if is_plat("windows") then
        set_symbols("debug")
    end

-- The SKSE plugin's libraries (Windows only)
if is_plat("windows") then

-- Reverse Engineering Library (formerly TiltedReverse)
target("SkyrimCoopReverse")
    set_kind("static")
    set_group("Libraries")
    add_files("reverse/*.cpp")
    add_includedirs("reverse/", {public = true})
    add_headerfiles("reverse/*.hpp", "reverse/*.inl")
    add_defines("NOMINMAX")
    add_deps("TiltedCore")
    add_packages("rpmalloc", "hopscotch-map", "minhook", "xbyak")

    -- mem is header-only; use the vendored copy (the xmake package does not cross-build)
    add_includedirs("../external/mem", {public = true})

    -- Ensure debug symbols
    if is_plat("windows") then
        set_symbols("debug")
    end

-- Hooks Library (formerly TiltedHooks)
target("SkyrimCoopHooks")
    set_kind("static")
    set_group("Libraries")
    add_files("hooks/*.cpp")
    add_files("hooks/DInputHook.cpp", {unity_ignored = true})
    add_includedirs("hooks/", {public = true})
    add_headerfiles("hooks/*.hpp")
    add_syslinks("dxguid", "dinput8", "d3d11")
    add_deps("TiltedCore", "SkyrimCoopReverse")
    add_packages("rpmalloc", "hopscotch-map")

    -- Ensure debug symbols
    if is_plat("windows") then
        set_symbols("debug")
    end


end -- is_plat("windows")
