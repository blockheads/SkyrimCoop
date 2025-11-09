-- Merged libraries (formerly in Libraries/)
includes("TiltedCore")
includes("external/DirectXTK")
-- protobuf handled via XMake package system (protoc used for .proto generation in gamenetworkingsockets)
includes("external/gamenetworkingsockets")
includes("external/imgui")
includes("libraries")

-- Skip client targets on Wine MSVC builds (core.tools.lib missing for linking)
-- Client targets: SkyrimTogetherClient, ImmersiveElf, SkyrimImmersiveLauncher, TPProcess
if is_plat("windows") and get_config("sdk") ~= "/opt/msvc" then
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
