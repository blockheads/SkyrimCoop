-- The test harness without SkyrimCoop (VanillaHarness.cpp): `coop_test.py repro --vanilla`.
if is_plat("windows") then
target("SkyrimCoopVanillaHarness")
    set_kind("shared")
    set_basename("SkyrimCoopVanillaHarness")
    set_prefixname("")
    set_languages("cxx23")
    add_files("VanillaHarness.cpp")
    add_packages("minhook")
    add_syslinks("kernel32")
target_end()
end
