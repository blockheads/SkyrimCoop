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
