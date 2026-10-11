target("SkyrimCoopMovementCore")
    set_kind("shared")
    set_basename("SkyrimCoopMovementCore")
    set_prefixname("")
    set_languages("cxx17")
    add_files("MovementCore.cpp")
    if is_plat("mingw") then
        add_shflags("-static-libgcc", "-static-libstdc++", {force = true})
    end
target_end()

-- Resident SKSE plugin that loads the client from a copy and swaps in rebuilds (Loader.cpp).
if is_plat("windows") then
target("SkyrimCoopLoader")
    set_kind("shared")
    set_basename("SkyrimCoopLoader")
    set_prefixname("")
    set_languages("cxx20")
    add_files("Loader.cpp")
    add_packages("minhook")
    add_syslinks("kernel32")
target_end()
end
