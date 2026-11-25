includes("network")
includes("common")
includes("components")
includes("base")
includes("encoding")
includes("hotreload")
includes("vanilla_harness")

-- The SKSE plugin: clang-cl targeting Windows (scripts/xmake-msvc.sh).
if is_plat("windows") then
    includes("external/imgui")
    includes("client")
end

-- The host's rules and the message encoding, natively (scripts/xmake-linux.sh).
if is_plat("linux") then
    includes("tests")
end

includes("ui_models")
if is_plat("linux") then
    includes("ui_workbench")
end
