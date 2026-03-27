-- Merged libraries (formerly in Libraries/)
includes("TiltedCore")
includes("libraries")

-- Client targets: SkyrimTogetherClient, ImmersiveElf, SkyrimImmersiveLauncher, TPProcess
if is_plat("windows") or is_plat("mingw") then
    includes("client")
    includes("immersive_elf")
    includes("immersive_launcher")
    includes("tp_process")
end

includes("common")
includes("components")
includes("base")
includes("admin_protocol")
includes("server_runner")
includes("server")
includes("encoding")

-- Tests need native build, not MinGW cross-compile
if not is_plat("mingw") then
    includes("tests")
end
