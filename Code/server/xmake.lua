local function istable(t) return type(t) == 'table' end

add_requires("sol2 v3.3.0", {configs = {lua = "lua"}})

local function build_server()
    devbuild_shared()
    set_group("Server")
    add_includedirs(
        ".",
        "../external/",
        "../external/cpp-httplib/include")
    set_pcxxheader("Pch.h")
    add_headerfiles("**.h")
    add_files("**.cpp")
    -- server_runner removed (P2P host-only); server builds as embedded library only
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
        "entt",
        "nlohmann_json")
        -- cpp-httplib is manually included via includedirs
end

target("SkyrimTogetherServer")
    set_basename("STServer")
    add_defines("TARGET_PREFIX=\"st\"")
    add_deps("SkyrimEncoding")
    build_server()
