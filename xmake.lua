-- include subprojects
includes("lib/commonlibsf")

-- set project constants
set_project("StarfieldVATS")
set_version("0.1.0")
set_license("GPL-3.0")
set_languages("c++23")
set_warnings("allextra")

-- add common rules
add_rules("mode.debug", "mode.releasedbg")
add_rules("plugin.vsxmake.autoupdate")

-- Vendored ImGui (1.89.9, D3D11 + Win32 backends) and MinHook, copied from
-- SomeCrazyGuy/Starfield-Console-Replacer (BetterConsole) — public domain
-- (Unlicense) / BSD-2-Clause, and a proven-working D3D11-on-12 overlay
-- setup for this exact game, rather than an unverified guess. Kept as a
-- separate target (own toolchain settings, no forced PCH) so the game's
-- RE::/SFSE:: precompiled header — force-included on the main target —
-- never leaks into plain C/vendored C++ files that know nothing about it.
target("vats-ui-libs")
    set_kind("static")
    set_languages("c++17")

    add_files("lib/imgui/imgui.cpp")
    add_files("lib/imgui/imgui_draw.cpp")
    add_files("lib/imgui/imgui_tables.cpp")
    add_files("lib/imgui/imgui_widgets.cpp")
    add_files("lib/imgui/imgui_impl_dx11.cpp")
    add_files("lib/imgui/imgui_impl_win32.cpp")
    add_includedirs("lib/imgui", { public = true })

    add_files("lib/minhook/buffer.c")
    add_files("lib/minhook/hook.c")
    add_files("lib/minhook/trampoline.c")
    add_files("lib/minhook/hde/hde32.c")
    add_files("lib/minhook/hde/hde64.c")
    add_includedirs("lib/minhook", "lib/minhook/hde", { public = true })

-- define targets
target("StarfieldVATS")
    add_rules("commonlibsf.plugin", {
        name = "StarfieldVATS",
        author = "Alexander Johnen",
        description = "Real-time VATS-style body part targeting for Starfield",
        email = "alexander.johnen@gmail.com"
    })

    -- add src files
    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    set_pcxxheader("src/pch.h")
    add_syslinks("user32", "d3d11", "dxgi")

    add_deps("vats-ui-libs")
