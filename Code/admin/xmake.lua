
-- Admin GUI tool - Client-only
target("Admin")
    set_kind("binary")
    set_group("Client")
    set_basename("TiltedAdmin")
    set_symbols("debug", "hidden")

    -- Disable on Wine MSVC (client-only GUI tool)
    if get_config("sdk") == "/opt/msvc" then
        set_enabled(false)
    end
    add_includedirs(
        ".",
        "../",
        "../external/")
    add_headerfiles("**.h")
    add_files(
        "**.cpp",
        "admin.rc")
    add_deps("CommonLib", "AdminProtocol", "SkyrimCoopNetworking")

    add_deps("SkyrimEncoding")

    if is_plat("windows") then
        add_syslinks("opengl32", "Shell32", "Gdi32", "Winmm", "Ole32", "version", "OleAut32", "Setupapi")
    end

    add_packages(
        "spdlog",
        "hopscotch-map",
        "glm",
        "magnum",
        "magnum-integration",
        "enet6")
