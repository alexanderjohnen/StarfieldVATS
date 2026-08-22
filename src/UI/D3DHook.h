#pragma once

namespace VATS::UI
{
	using DrawCallback = void (*)();

	// Installs the DXGI/D3D12 hook chain (IAT hook on CreateDXGIFactory2 ->
	// vtable hook on CreateSwapChainForHwnd -> vtable hooks on Present and
	// ResizeBuffers) needed to draw an ImGui overlay. Call once, early
	// (from SFSE_PLUGIN_LOAD) — the actual swapchain/device aren't created
	// yet at that point, so this just arms the hooks for later.
	//
	// Adapted from SomeCrazyGuy/Starfield-Console-Replacer (BetterConsole),
	// Unlicense — a proven-working D3D11-on-12 ImGui overlay technique for
	// this exact game, not a from-scratch guess. Unlike BetterConsole we
	// don't hook window input (WndProc/GetRawInputData/ClipCursor) since
	// this is a passive HUD, not an interactive menu — no mouse/keyboard
	// capture needed, which also means fewer hooks that could go wrong.
	void Install();

	// Registers the function called once per frame, after ImGui::NewFrame()
	// and before ImGui::Render(), to draw overlay content. Only one
	// callback is supported; call again to replace it.
	void SetDrawCallback(DrawCallback a_callback);

	// Current backbuffer size in pixels, readable from ANY thread at any
	// time. Returns false until the game has created its swapchain.
	// Exists because WorldToScreen used to read ImGui's io.DisplaySize
	// for the aspect ratio — but ImGui's context only exists once the
	// Present hook has run at least once, and callers off the render
	// thread (AimAssist's steering loop) have no way to know that.
	// 2026-08-22: a session where the Present hook failed to install left
	// the ImGui context permanently null, and the first locked-target
	// shot crashed in ImGui::GetIO() (Crashlog 18-14-27). Swapchain
	// geometry is captured here at creation/resize instead, independent
	// of ImGui entirely.
	bool GetDisplaySize(float& a_outWidth, float& a_outHeight);
}
