-- Standalone build for feasibility test DLLs
-- Invoked independently: cd Code/tests/feasibility && xmake f -p mingw --mingw=<path> && xmake

set_xmakever("2.8.5")
set_languages("c99", "cxx20")

if is_plat("mingw") then
    add_cxflags("-static", "-static-libgcc", "-static-libstdc++")
    add_ldflags("-static", "-static-libgcc", "-static-libstdc++", {force = true})
    add_defines("_WIN32_WINNT=0x0A00", "WINVER=0x0A00")
    add_defines("NOMINMAX")
end

add_requires("minhook v1.3.3")

-- GATE-01: Minimal SKSE plugin proving MinGW ABI compatibility
target("gate01_skse_plugin")
    set_kind("shared")
    set_basename("gate01_test")
    set_prefixname("")
    add_files("gate01_skse_plugin/main.cpp")

-- GATE-02: MinHook function hooking under MinGW
target("gate02_minhook")
    set_kind("shared")
    set_basename("gate02_test")
    set_prefixname("")
    add_files("gate02_minhook/main.cpp")
    add_packages("minhook")
