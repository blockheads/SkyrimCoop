-- The enet transport: the host's listener (Server, under HostNetwork) and a guest's connection (Client, under
-- TransportService). Messages travel in the encoding's Wire buffers.
target("Network")
    set_kind("static")
    set_group("Libraries")
    add_files("*.cpp")
    add_includedirs(".", "..", "../common", "../encoding", {public = true})
    add_headerfiles("*.h")
    add_packages("enet6", "snappy", "libuv", "spdlog")
    if is_plat("linux") then
        add_cxflags("-fPIC")
    end
