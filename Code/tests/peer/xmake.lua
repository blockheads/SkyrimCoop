-- coop_peer: a game stand-in for scenarios (scripts/coop_test.py), over the real network: a host
-- that refuses every join, or a game that asks to join. Run: build/linux/x86_64/<mode>/coop_peer
target("coop_peer")
    set_kind("binary")
    set_group("Tests")
    set_languages("cxx23")
    add_files("coop_peer.cpp")
    add_deps("SkyrimEncoding", "Network", "CommonLib")
    add_packages("glm", "spdlog", "rpmalloc", "enet6", "snappy", "libuv")
    add_syslinks("pthread")
target_end()
