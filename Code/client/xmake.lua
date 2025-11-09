
local function build_client(name)
target(name)
    -- Use object library for Wine MSVC to bypass broken lib.exe archiver
    if is_plat("windows") and get_config("sdk") == "/opt/msvc" then
        set_kind("object")
    else
        set_kind("static")
    end
    set_group("Client")
    add_includedirs(".","../external/")
    -- Add server include directory for embedded GameServer (P2P hosting)
    add_includedirs("../server", {public = false})
    set_pcxxheader("TiltedOnlinePCH.h")

    -- Define WINE_MSVC_BUILD for Wine MSVC builds to disable UI features
    if is_plat("windows") and get_config("sdk") == "/opt/msvc" then
        add_defines("WINE_MSVC_BUILD")
    end

    -- exclude game specific stuff and Vivox
    add_headerfiles("**.h|Games/Skyrim/**|Services/Vivox/**")
    add_files("**.cpp|Games/Skyrim/**|Services/Vivox/**")

    after_install(function(target)
        -- copy dlls (works on both native Windows and Wine MSVC)
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
        "ImGuiImpl",
        "SkyrimCoopNetworking",
        "SkyrimCoopReverse",
        "SkyrimCoopHooks",
        {inherit = true}
    )

    -- UI dependencies
    add_deps("SkyrimCoopUIProcess", "SkyrimCoopUI")

    -- Core packages
    add_packages(
        "mimalloc",  -- Needed for Memory.cpp direct include
        "spdlog",
        "hopscotch-map",
        "cryptopp",
        "enet6",
        "minhook",
        "entt",
        "glm",
        "mem",
        "xbyak")

    -- UI packages (Discord, ImGui, CEF all work on Wine MSVC)
    add_packages("discord", "imgui", "cef")

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
