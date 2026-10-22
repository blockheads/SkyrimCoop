-- Resident SKSE plugin that loads the client from a copy and swaps in rebuilds (Loader.cpp).
-- Test builds only, with the client's hot reload (the test_harness option, Code/client/xmake.lua).
if is_plat("windows") and has_config("test_harness") then
target("SkyrimCoopLoader")
    set_kind("shared")
    set_basename("SkyrimCoopLoader")
    set_prefixname("")
    set_languages("cxx23")
    add_files("Loader.cpp")
    add_packages("minhook")
    add_syslinks("kernel32")
target_end()
end
