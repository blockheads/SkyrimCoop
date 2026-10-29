-- Production Steam launcher: preserves invite arguments across SKSE's argument-free CreateProcess.
if is_plat("windows") then
target("SkyrimCoopLauncher")
    set_kind("binary")
    set_languages("cxx23")
    add_files("Main.cpp")
    add_ldflags("/SUBSYSTEM:WINDOWS", {force = true})
    add_syslinks("kernel32", "user32")
target_end()
end
