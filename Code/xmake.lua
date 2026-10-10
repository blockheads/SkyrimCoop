-- Merged libraries (formerly in Libraries/)
includes("TiltedCore")
includes("libraries")

-- External vendored libraries (Tier 3 client deps)
if is_plat("windows") or is_plat("mingw") then
    -- DirectXTK (CEF overlay renderer) is not part of the SKSE plugin builds
    -- (MinGW, or clang-cl targeting windows from Linux).
    includes("external/imgui")
end

-- Client targets: SkyrimTogetherClient, ImmersiveElf
if is_plat("windows") or is_plat("mingw") then
    includes("client")
    includes("immersive_elf")
    -- CEF launcher/tp_process targets belonged to the original MSVC-on-Windows
    -- build and are not built here.
end

includes("common")
includes("components")
includes("base")
includes("admin_protocol")
-- server_runner removed (P2P host-only, per Phase 5 D-01)
includes("server")
includes("encoding")
includes("hotreload")

-- Tests need a native Linux build, not the Windows cross-compile
if is_plat("linux") then
    includes("tests")
end
