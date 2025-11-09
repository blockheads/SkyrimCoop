-- Merged libraries (formerly in Libraries/)
includes("TiltedCore")
includes("external/DirectXTK")
includes("external/imgui")
includes("libraries")

-- Client targets now work on Wine MSVC with object library workaround
-- Note: UI libraries (CEF-based) are still disabled in libraries/xmake.lua for Wine MSVC
-- Client targets: SkyrimTogetherClient, ImmersiveElf, SkyrimImmersiveLauncher, TPProcess
if is_plat("windows") then
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
includes("tests")
