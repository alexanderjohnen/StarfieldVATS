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

    -- Tripwire for the two patches this project carries inside its
    -- vendored copy of ImGui (lib/imgui). Both are invisible from our own
    -- source, both are load-bearing, and both would be silently undone by
    -- dropping a fresh ImGui release over lib/imgui - which is exactly what
    -- a future update looks like. Neither failure announces itself:
    --
    --   * Losing the buffer patch brings back a ~2GB/minute memory leak
    --     that produces no crash, no log line and nothing unusual in Task
    --     Manager - it just makes the whole machine slow after a few
    --     minutes. It cost a full evening of bisecting to find once.
    --   * Losing the imconfig.h patch stubs ImFileOpen back out, so the HUD
    --     font silently stops loading and the overlay falls back to a 13px
    --     bitmap face. That one went unnoticed for two days.
    --
    -- So the build refuses to run rather than produce a binary with either
    -- one quietly missing. If you are updating ImGui on purpose: re-apply
    -- the patches (both are commented in place, and HANDOFF.md explains
    -- them), then this check passes again on its own.
    before_build(function (target)
        local function must(path, needle, why)
            local content = io.readfile(path)
            if not content then
                raise("vendored-patch check: cannot read " .. path)
            end
            if not content:find(needle, 1, true) then
                raise("vendored-patch check FAILED in " .. path ..
                      "\n  missing: " .. needle ..
                      "\n  " .. why ..
                      "\n  Looks like lib/imgui was updated. Re-apply the patch, see HANDOFF.md.")
            end
        end
        local function must_not(path, needle, why)
            local content = io.readfile(path)
            if not content then
                raise("vendored-patch check: cannot read " .. path)
            end
            if content:find(needle, 1, true) then
                raise("vendored-patch check FAILED in " .. path ..
                      "\n  found again: " .. needle ..
                      "\n  " .. why ..
                      "\n  Looks like lib/imgui was updated. Re-apply the patch, see HANDOFF.md.")
            end
        end

        local backend = "lib/imgui/imgui_impl_dx11.cpp"
        must(backend, "D3D11_USAGE_DEFAULT",
             "the vertex/index/constant buffers must not be DYNAMIC.")
        must(backend, "UpdateSubresource(bd->pVB",
             "buffers must be filled with UpdateSubresource, not by mapping.")
        must_not(backend, "ctx->Map(",
             "Map(WRITE_DISCARD) renames an allocation per frame, and the rename pool is only recycled at Present - which this 11on12 overlay never does.")

        must_not("lib/imgui/imconfig.h", "#define IMGUI_DISABLE_FILE_FUNCTIONS",
             "this stubs ImFileOpen to return NULL, so AddFontFromFileTTF can never load the HUD font.")
    end)
