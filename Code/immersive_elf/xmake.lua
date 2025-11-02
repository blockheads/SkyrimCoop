
target("ImmersiveElf")
    set_basename("EarlyLoad")
    set_kind("shared")
    set_group("Client")
    add_includedirs(
        ".",
        "../external/")
    add_headerfiles("**.h")
    add_files(
        "**.cpp")