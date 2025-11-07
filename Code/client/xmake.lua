
local function build_client(name)
target(name)
    set_kind("static")
    set_group("Client")
    add_includedirs(".","../external/")
    -- Add server include directory for embedded GameServer (P2P hosting)
    add_includedirs("../server", {public = false})
    set_pcxxheader("TiltedOnlinePCH.h")

    -- exclude game specifc stuff
    if is_plat("mingw") then
        -- For MinGW, also exclude overlay/UI files (CEF not available)
        add_headerfiles("**.h|Games/Skyrim/**|Services/Vivox/**|Services/Generic/Overlay*|Systems/RenderSystemD3D11.h")
        add_files("**.cpp|Games/Skyrim/**|Services/Vivox/**|Services/Generic/Overlay*|Systems/RenderSystemD3D11.cpp")
    else
        add_headerfiles("**.h|Games/Skyrim/**|Services/Vivox/**")
        add_files("**.cpp|Games/Skyrim/**|Services/Vivox/**")
    end

    after_install(function(target)
        -- copy dlls
        for _, pkg_with_dlls in ipairs({"cef", "discord"}) do
            local linkdir = target:pkg(pkg_with_dlls):get("linkdirs")
            local bindir = path.join(linkdir, "..", "bin")
            os.cp(bindir, target:installdir())
        end
        -- copy ui
        local uidir = path.join(target:scriptdir(), "..", "skyrim_ui", "src")
        os.cp(path.join(uidir, "assets", "images", "cursor.dds"), path.join(target:installdir(), "bin", "assets", "images", "cursor.dds"))
        os.cp(path.join(uidir, "assets", "images", "cursor.png"), path.join(target:installdir(), "bin", "assets", "images", "cursor.png"))
        os.rm(path.join(target:installdir(), "bin", "**Tests.exe"))
    end)

    add_files("Games/Skyrim/**.cpp")
    add_headerfiles("Games/Skyrim/**.h")
    -- rather hacky:
    add_includedirs("Games/Skyrim")
    add_deps("SkyrimEncoding")
    -- Add server dependency for embedded GameServer (P2P hosting)
    add_deps("SkyrimTogetherServer")

    -- Core dependencies (always needed)
    add_deps(
        "CommonLib",
        "BaseLib",
        "SkyrimCoopNetworking",
        "SkyrimCoopReverse",
        "SkyrimCoopHooks",
        {inherit = true}
    )

    -- UI dependencies (Windows only, not for MinGW cross-compile)
    if is_plat("windows") and not is_plat("mingw") then
        add_deps("SkyrimCoopUIProcess", "SkyrimCoopUI", "ImGuiImpl")
    end

    add_packages(
        "rpmalloc",  -- Needed for Memory.cpp direct include
        "spdlog",
        "hopscotch-map",
        "cryptopp",
        "enet6",
        "entt",
        "glm",
        "xbyak")

    -- UI packages (Windows only, not for MinGW cross-compile)
    if is_plat("windows") and not is_plat("mingw") then
        add_packages("discord", "imgui", "cef")
        add_defines("TP_WITH_OVERLAY=1")
    else
        add_defines("TP_WITH_OVERLAY=0")
    end

    if has_config("vivox") then
        add_files("Services/Vivox/**.cpp")
        add_headerfiles("Services/Vivox/**.h")
        add_includedirs("Services/Vivox")
        add_deps("Vivox")
        add_defines("TP_VIVOX=1")
    else
        add_defines("TP_VIVOX=0")
    end

    add_syslinks(
        "version",
        "dbghelp",
        "kernel32")
end


build_client("SkyrimTogetherClient")
