local function istable(t) return type(t) == 'table' end

add_requires("sol2 v3.3.0", {configs = {lua = "lua"}})

local function build_server()
    set_kind("static")
    set_group("Server")
    add_includedirs(
        ".",
        "../external/",
        "../external/cpp-httplib/include")
    set_pcxxheader("Pch.h")
    add_headerfiles("**.h")
    add_files("**.cpp")
    -- Don't add server.rc here - it causes duplicate VERSION resource errors
    -- when SkyrimServerRunner executable links against this library.
    -- The runner has its own server_runner.rc
    if is_plat("linux") then
        add_cxxflags("-fvisibility=hidden")
    end
    add_deps(
        "CommonLib",
        "Console",
        "Resources",
        "ESLoader",
        "CrashHandler",
        "BaseLib",
        "AdminProtocol",
        "SkyrimCoopNetworking"
    )
    add_packages(
        "enet6",
        "spdlog",
        "hopscotch-map",
        "sqlite3",
        "lua",
        "sol2",
        "glm",
        "entt")
        -- cpp-httplib is manually included via includedirs
end

target("SkyrimTogetherServer")
    set_basename("STServer")
    add_defines("TARGET_PREFIX=\"st\"")
    add_deps("SkyrimEncoding")
    build_server()
