if is_plat("windows") or is_plat("mingw") then
    component("ImGuiImpl")
        add_deps("imgui")
end
