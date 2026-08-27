#include "D3DHook.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <d3d11.h>
#include <d3d11on12.h>
#include <d3d12.h>
#include <dxgi1_6.h>

#include "MinHook.h"

#include "Settings.h"

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

// One of the above pulls in a Windows macro `ERROR` (from wingdi.h,
// #define ERROR 0) that collides with VATS_ERROR(...) used below.
#ifdef ERROR
#	undef ERROR
#endif

// Ported from SomeCrazyGuy/Starfield-Console-Replacer (BetterConsole),
// released into the public domain (Unlicense). Stripped of everything
// BetterConsole-specific (its console replacer, mod-menu tab system,
// hotkeys, settings registry) and of all input hooking (WndProc,
// GetRawInputData, ClipCursor) — this is a passive always-on HUD, not an
// interactive menu, so it never needs to capture mouse/keyboard input.
// Kept: the DXGI/D3D12 hook chain and the D3D11-on-12 interop render path,
// which is the part that's actually proven to work on Starfield.

#include <atomic>

namespace VATS::UI
{
	namespace
	{
		// ImGui's built-in font is ProggyClean, a BITMAP face designed for
		// exactly 13px. The overlay was drawing the pre-lock hint at 1.6x
		// that, which upscales the atlas rather than rendering larger
		// glyphs - so it came out blocky next to the game's own HUD
		// lettering, which is what Alexander saw (2026-08-26: "sieht etwas
		// schaebig aus ... sehr kantig").
		//
		// Bahnschrift is the first choice because it is a condensed
		// technical face that sits very close to Starfield's own HUD
		// lettering, and it ships with Windows 10 and 11 so there is
		// nothing to redistribute. Segoe UI is the fallback (present on
		// every Windows), and ImGui's default is the last resort - the
		// overlay must never fail to draw because a font file moved.
		void LoadHudFont()
		{
			auto& io = ImGui::GetIO();

			// 20px at 1440p is about the cap height of the scanner's own
			// "RANGE 100M". Rendered at that size rather than scaled up to
			// it, which is the entire point of this function.
			constexpr float kFontSize = 20.0f;
			// Forward slashes, deliberately. Until 2026-08-27 these paths
			// were written with single backslashes, which C does not read
			// as path separators at all: the escapes for W and F are
			// unrecognised (MSVC warns and drops the backslash), and the
			// one in front of "bahnschrift" is a literal BACKSPACE. What
			// the font loader actually received was therefore
			// "C:WindowsFonts<0x08>ahnschrift.ttf" - so BOTH candidates
			// failed on every launch and the overlay silently fell back to
			// ImGui's built-in 13px bitmap font. That is exactly the
			// "sieht etwas schaebig aus ... sehr kantig" Alexander
			// reported on 2026-08-26, which this very function was written
			// to fix, and the log has said "no TTF found" ever since.
			// Win32 accepts forward slashes in file paths, so this form
			// has nothing left to escape.
			constexpr const char* kCandidates[] = {
				"C:/Windows/Fonts/bahnschrift.ttf",
				"C:/Windows/Fonts/segoeui.ttf",
			};

			for (const char* path : kCandidates) {
				if (io.Fonts->AddFontFromFileTTF(path, kFontSize) != nullptr) {
					VATS_LOG("[UI] HUD font: {} at {}px", path, kFontSize);
					return;
				}
			}

			io.Fonts->AddFontDefault();
			VATS_WARN("[UI] HUD font: no TTF found, falling back to the built-in bitmap font");
		}

		DrawCallback g_drawCallback = nullptr;

		// Backbuffer size, written on swapchain create/resize, read from
		// arbitrary threads via GetDisplaySize (see D3DHook.h for why this
		// exists instead of ImGui's io.DisplaySize).
		std::atomic<std::uint32_t> g_displayWidth{ 0 };
		std::atomic<std::uint32_t> g_displayHeight{ 0 };

		using FuncPtr = void (*)();

		// --- IAT patching (for the very first DXGI factory creation) ---

		using IATEntry = FuncPtr*;

		template <class T>
		[[nodiscard]] T RVA(std::uintptr_t a_offset)
		{
			static std::uintptr_t imagebase = 0;
			if (!imagebase) {
				imagebase = reinterpret_cast<std::uintptr_t>(::GetModuleHandleW(nullptr));
			}
			return reinterpret_cast<T>(imagebase + a_offset);
		}

		[[nodiscard]] IATEntry SearchIAT(const char* a_dllName, const char* a_funcName)
		{
			VATS_LOG("[UI] SearchIAT('{}', '{}') start", a_dllName ? a_dllName : "(null)", a_funcName);

			auto* dosHeader = RVA<const IMAGE_DOS_HEADER*>(0);
			VATS_TRACE("[UI] SearchIAT: dosHeader=0x{:X} e_magic=0x{:X}",
				reinterpret_cast<std::uintptr_t>(dosHeader), dosHeader ? dosHeader->e_magic : 0);

			auto* ntHeaders = RVA<const IMAGE_NT_HEADERS64*>(dosHeader->e_lfanew);
			VATS_TRACE("[UI] SearchIAT: ntHeaders=0x{:X} signature=0x{:X}",
				reinterpret_cast<std::uintptr_t>(ntHeaders), ntHeaders ? ntHeaders->Signature : 0);

			const auto importsDir = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
			VATS_TRACE("[UI] SearchIAT: importsDir VA=0x{:X} size={}", importsDir.VirtualAddress, importsDir.Size);

			auto* imports = RVA<const IMAGE_IMPORT_DESCRIPTOR*>(importsDir.VirtualAddress);
			VATS_TRACE("[UI] SearchIAT: imports=0x{:X}", reinterpret_cast<std::uintptr_t>(imports));

			for (std::uint64_t i = 0; imports[i].Characteristics; ++i) {
				auto* dllName = RVA<const char*>(imports[i].Name);
				VATS_TRACE("[UI] SearchIAT: [{}] dllName ptr=0x{:X}", i, reinterpret_cast<std::uintptr_t>(dllName));
				VATS_TRACE("[UI] SearchIAT: [{}] dllName = '{}'", i, dllName ? dllName : "(null)");

				if (a_dllName && _stricmp(dllName, a_dllName) != 0) {
					continue;
				}

				VATS_TRACE("[UI] SearchIAT: matched dll '{}', OriginalFirstThunk=0x{:X} FirstThunk=0x{:X}",
					dllName, imports[i].OriginalFirstThunk, imports[i].FirstThunk);

				auto* names = RVA<const IMAGE_THUNK_DATA64*>(imports[i].OriginalFirstThunk);
				auto* thunks = RVA<IMAGE_THUNK_DATA64*>(imports[i].FirstThunk);
				for (std::uint64_t j = 0; thunks[j].u1.AddressOfData; ++j) {
					if (thunks[j].u1.AddressOfData & IMAGE_ORDINAL_FLAG64) {
						continue;  // ordinal import, no name to compare
					}
					auto* byName = RVA<const IMAGE_IMPORT_BY_NAME*>(names[j].u1.AddressOfData);
					if (_stricmp(byName->Name, a_funcName) == 0) {
						VATS_TRACE("[UI] SearchIAT: found '{}' at thunk[{}]", a_funcName, j);
						return reinterpret_cast<IATEntry>(&thunks[j].u1.AddressOfData);
					}
				}
			}
			VATS_TRACE("[UI] SearchIAT: '{}' not found in '{}'", a_funcName, a_dllName ? a_dllName : "(any)");
			return nullptr;
		}

		[[nodiscard]] FuncPtr HookFunctionIAT(const char* a_dllName, const char* a_funcName, FuncPtr a_newFunc)
		{
			auto entry = SearchIAT(a_dllName, a_funcName);
			if (!entry) {
				return nullptr;
			}
			const FuncPtr old = *entry;
			DWORD oldProtect = 0;
			if (::VirtualProtect(entry, sizeof(FuncPtr), PAGE_EXECUTE_READWRITE, &oldProtect)) {
				*entry = a_newFunc;
				::VirtualProtect(entry, sizeof(FuncPtr), oldProtect, &oldProtect);
				return old;
			}
			return nullptr;
		}

		// --- MinHook wrapper, following any pre-existing JMP-chain hook so
		//     we coexist with other overlays (Steam, Discord, RTSS, ...) ---

		[[nodiscard]] FuncPtr FollowFunctionHook(FuncPtr a_func)
		{
			// Iterative with a hard cap, not recursive — a malformed or
			// cyclic JMP chain (e.g. from another overlay) must never be
			// able to blow the stack here. A stack overflow is exactly the
			// kind of crash that produces no crash log (no stack space
			// left to run a handler), so this is defense against silent,
			// undiagnosable crashes as much as against bad input.
			constexpr int kMaxHops = 32;
			auto*         current = a_func;
			for (int hop = 0; hop < kMaxHops; ++hop) {
				const auto* bytes = reinterpret_cast<const unsigned char*>(current);
				if (bytes[0] != 0xE9) {  // not a JMP rel32, this is the real function
					return current;
				}
				std::int32_t disp = 0;
				std::memcpy(&disp, bytes + 1, sizeof(disp));
				current = reinterpret_cast<FuncPtr>(const_cast<unsigned char*>(bytes) + 5 + disp);
			}
			VATS_ERROR("[UI] FollowFunctionHook: hop limit ({}) hit, chain did not terminate — using last-seen address", kMaxHops);
			return current;
		}

		[[nodiscard]] FuncPtr HookFunctionMH(FuncPtr a_old, FuncPtr a_new)
		{
			LPVOID trampoline = nullptr;
			const MH_STATUS createStatus = MH_CreateHook(reinterpret_cast<LPVOID>(a_old), reinterpret_cast<LPVOID>(a_new), &trampoline);
			if (createStatus != MH_OK) {
				VATS_ERROR("[UI] MH_CreateHook(0x{:X}) failed: {}", reinterpret_cast<std::uintptr_t>(a_old), MH_StatusToString(createStatus));
				return nullptr;
			}
			const MH_STATUS enableStatus = MH_EnableHook(reinterpret_cast<LPVOID>(a_old));
			if (enableStatus != MH_OK) {
				VATS_ERROR("[UI] MH_EnableHook(0x{:X}) failed: {}", reinterpret_cast<std::uintptr_t>(a_old), MH_StatusToString(enableStatus));
				return nullptr;
			}
			return reinterpret_cast<FuncPtr>(trampoline);
		}

		// Fallback when MinHook can't hook the function body the vtable
		// slot points at. Seen 2026-08-22: with BetterConsole loaded, the
		// game's swapchain vtable pointer was already swapped for
		// BetterConsole's own private vtable copy (both the vtable and its
		// Present/ResizeBuffers slots resolved into BetterConsole.dll's
		// module range) and MinHook failed on its handler — leaving our
		// Present hook silently dead for the whole session (no overlay at
		// all, and the AimAssist thread later crashed on the never-created
		// ImGui context). Patching the vtable slot itself doesn't care
		// whose code the slot points at: we store the previous value and
		// call it as "original", so whatever chain was there keeps working
		// under us. Only used when MinHook fails — MinHook remains
		// preferred because a body hook also catches callers that bypass
		// this particular vtable copy.
		[[nodiscard]] FuncPtr HookVTableSlot(FuncPtr* a_slot, FuncPtr a_new)
		{
			const FuncPtr old = *a_slot;
			DWORD oldProtect = 0;
			if (!::VirtualProtect(a_slot, sizeof(FuncPtr), PAGE_EXECUTE_READWRITE, &oldProtect)) {
				VATS_ERROR("[UI] HookVTableSlot: VirtualProtect(0x{:X}) failed", reinterpret_cast<std::uintptr_t>(a_slot));
				return nullptr;
			}
			*a_slot = a_new;
			::VirtualProtect(a_slot, sizeof(FuncPtr), oldProtect, &oldProtect);
			VATS_LOG("[UI] HookVTableSlot: patched slot 0x{:X} (0x{:X} -> 0x{:X})",
				reinterpret_cast<std::uintptr_t>(a_slot), reinterpret_cast<std::uintptr_t>(old), reinterpret_cast<std::uintptr_t>(a_new));
			return old;
		}

		// --- D3D11-on-12 interop render path ---

		struct D3DBuffer
		{
			ID3D12Resource*         d3d12RenderTarget{ nullptr };
			ID3D11Resource*         d3d11WrappedBackBuffer{ nullptr };
			ID3D11RenderTargetView* d3d11RenderTargetView{ nullptr };
		};

		D3DBuffer*              g_buffers = nullptr;
		unsigned                g_bufferCount = 0;
		ID3D11DeviceContext*    g_d3d11Context = nullptr;
		ID3D11On12Device*       g_d3d11On12Device = nullptr;
		bool                    g_initialized = false;
		HWND                    g_gameHwnd = nullptr;
		bool                    g_imguiCreated = false;

		void ReleaseIfInitialized()
		{
			if (g_initialized) {
				ImGui_ImplDX11_Shutdown();
				ImGui_ImplWin32_Shutdown();
			}
			if (g_buffers) {
				for (unsigned i = 0; i < g_bufferCount; ++i) {
					if (g_buffers[i].d3d11RenderTargetView) {
						g_buffers[i].d3d11RenderTargetView->Release();
					}
					if (g_buffers[i].d3d11WrappedBackBuffer) {
						g_buffers[i].d3d11WrappedBackBuffer->Release();
					}
					if (g_buffers[i].d3d12RenderTarget) {
						g_buffers[i].d3d12RenderTarget->Release();
					}
				}
				std::free(g_buffers);
				g_buffers = nullptr;
				g_bufferCount = 0;
			}
			if (g_d3d11On12Device) {
				g_d3d11On12Device->Release();
				g_d3d11On12Device = nullptr;
			}
			if (g_d3d11Context) {
				g_d3d11Context->Flush();
				g_d3d11Context->Release();
				g_d3d11Context = nullptr;
			}
			g_initialized = false;
		}

		void Initialize(IDXGISwapChain* a_swapChain, ID3D12CommandQueue* a_queue)
		{
			VATS_LOG("[UI] Initialize: swapchain=0x{:X}, queue=0x{:X}, hwnd=0x{:X}",
				reinterpret_cast<std::uintptr_t>(a_swapChain), reinterpret_cast<std::uintptr_t>(a_queue),
				reinterpret_cast<std::uintptr_t>(g_gameHwnd));
			if (!g_gameHwnd) {
				VATS_ERROR("[UI] Initialize: g_gameHwnd is null, aborting (should be impossible — set in FakeCreateSwapChainForHwnd before this can run)");
				return;
			}
			g_initialized = true;

			ID3D12Device*           d3d12Device = nullptr;
			ID3D11Device*           d3d11Device = nullptr;
			ID3D12DescriptorHeap*   rtvHeap = nullptr;
			bool                    ok = true;

			if (FAILED(a_swapChain->GetDevice(IID_PPV_ARGS(&d3d12Device)))) {
				VATS_ERROR("[UI] Initialize: GetDevice failed");
				ok = false;
			}

			DXGI_SWAP_CHAIN_DESC desc{};
			if (ok && FAILED(a_swapChain->GetDesc(&desc))) {
				VATS_ERROR("[UI] Initialize: GetDesc failed");
				ok = false;
			}
			if (ok && desc.BufferDesc.Width && desc.BufferDesc.Height) {
				g_displayWidth.store(desc.BufferDesc.Width, std::memory_order_relaxed);
				g_displayHeight.store(desc.BufferDesc.Height, std::memory_order_relaxed);
			}

			if (ok) {
				g_bufferCount = desc.BufferCount;
				const auto featureLevel = D3D_FEATURE_LEVEL_11_0;
				const auto hr = D3D11On12CreateDevice(d3d12Device, 0, &featureLevel, 1,
					reinterpret_cast<IUnknown* const*>(&a_queue), 1, 0,
					&d3d11Device, &g_d3d11Context, nullptr);
				if (FAILED(hr)) {
					VATS_ERROR("[UI] Initialize: D3D11On12CreateDevice failed, hr=0x{:X}", static_cast<std::uint32_t>(hr));
					ok = false;
				}
			}

			if (ok && FAILED(d3d11Device->QueryInterface(IID_PPV_ARGS(&g_d3d11On12Device)))) {
				VATS_ERROR("[UI] Initialize: QueryInterface(ID3D11On12Device) failed");
				ok = false;
			}

			if (ok) {
				g_buffers = static_cast<D3DBuffer*>(std::calloc(g_bufferCount, sizeof(D3DBuffer)));

				D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
				rtvDesc.NumDescriptors = g_bufferCount;
				rtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
				rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
				if (FAILED(d3d12Device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&rtvHeap)))) {
					ok = false;
				}
			}

			if (ok) {
				auto rtvHandle = rtvHeap->GetCPUDescriptorHandleForHeapStart();
				const auto rtvSize = d3d12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

				for (unsigned i = 0; ok && i < g_bufferCount; ++i) {
					if (FAILED(a_swapChain->GetBuffer(i, IID_PPV_ARGS(&g_buffers[i].d3d12RenderTarget)))) {
						ok = false;
						break;
					}
					d3d12Device->CreateRenderTargetView(g_buffers[i].d3d12RenderTarget, nullptr, rtvHandle);

					D3D11_RESOURCE_FLAGS flags{ D3D11_BIND_RENDER_TARGET };
					if (FAILED(g_d3d11On12Device->CreateWrappedResource(g_buffers[i].d3d12RenderTarget, &flags,
							D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_PRESENT,
							IID_PPV_ARGS(&g_buffers[i].d3d11WrappedBackBuffer)))) {
						ok = false;
						break;
					}
					if (FAILED(d3d11Device->CreateRenderTargetView(g_buffers[i].d3d11WrappedBackBuffer, nullptr,
							&g_buffers[i].d3d11RenderTargetView))) {
						ok = false;
						break;
					}
					rtvHandle.ptr += rtvSize;
				}
			}

			if (ok) {
				if (!g_imguiCreated) {

					IMGUI_CHECKVERSION();
					ImGui::CreateContext();
					ImGui::GetIO().MouseDrawCursor = false;
					ImGui::StyleColorsDark();
					LoadHudFont();
					g_imguiCreated = true;
				}
				// BUG FIXED 2026-08-22: this call was missing entirely. The
				// win32 backend was never initialized (its one-time Init()
				// lives in BetterConsole's WndProc hook, which we stripped
				// out since we don't need input capture) — but the render
				// loop still called ImGui_ImplWin32_NewFrame() every frame,
				// dereferencing backend state that was never set up. That's
				// exactly the crash: happened the instant the first real
				// frame tried to render, no exception a normal handler can
				// catch cleanly, no crash log. `g_gameHwnd` is captured in
				// FakeCreateSwapChainForHwnd.
				ImGui_ImplWin32_Init(g_gameHwnd);
				ImGui_ImplDX11_Init(d3d11Device, g_d3d11Context);
			} else {
				g_initialized = false;
			}

			if (rtvHeap) {
				rtvHeap->Release();
			}
			if (d3d11Device) {
				d3d11Device->Release();
			}
			if (d3d12Device) {
				d3d12Device->Release();
			}
			if (!g_initialized) {
				ReleaseIfInitialized();
			}
			VATS_LOG("[UI] Initialize: {}", g_initialized ? "success" : "FAILED");
		}

		void InitializeOrRender(IDXGISwapChain* a_swapChain, ID3D12CommandQueue* a_queue)
		{
			if (!g_initialized) {
				Initialize(a_swapChain, a_queue);
				if (!g_initialized) {
					return;
				}
			}

			auto* swapChain3 = static_cast<IDXGISwapChain3*>(a_swapChain);
			const auto  index = swapChain3->GetCurrentBackBufferIndex();
			if (index >= g_bufferCount) {
				ReleaseIfInitialized();
				return;
			}

			auto* buf = &g_buffers[index];

			// Staged for the leak hunt of 2026-08-27 ([Debug] iOverlayStage).
			// Measured: with this whole function skipped the process is flat
			// to within half a megabyte over two minutes; with it running it
			// grows ~518MB per 15s, about half a megabyte per presented
			// frame, while handles and threads stay put. So the leak is in
			// here, and these two lines are where it gets split further -
			// stage 1 runs the ImGui half and stops before the D3D half.
			//
			// Overlay::Draw is NOT the suspect: the measurements were taken
			// sitting in the star map, where it returns almost immediately.
			// Everything below runs regardless of what it decides.
			const int stage = Settings::Get().overlayStage;

			ImGui_ImplDX11_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();

			if (g_drawCallback) {
				g_drawCallback();
			}

			// Always paired with NewFrame, including in stage 1: leaving a
			// frame open would grow ImGui's own state every frame and
			// manufacture a second leak on top of the one being measured.
			ImGui::Render();

			if (stage < 2) {
				return;
			}

			g_d3d11On12Device->AcquireWrappedResources(&buf->d3d11WrappedBackBuffer, 1);
			g_d3d11Context->OMSetRenderTargets(1, &buf->d3d11RenderTargetView, nullptr);
			ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
			g_d3d11On12Device->ReleaseWrappedResources(&buf->d3d11WrappedBackBuffer, 1);
			g_d3d11Context->Flush();
		}

		// --- Swap chain tracking + hooks ---

		constexpr unsigned kMaxQueues = 4;
		struct SwapChainQueue
		{
			ID3D12CommandQueue* queue{ nullptr };
			IDXGISwapChain*      swapChain{ nullptr };
			std::uint64_t        age{ 0 };
		};
		SwapChainQueue g_queues[kMaxQueues]{};

		FuncPtr* g_swapChainVTable = nullptr;
		bool     g_swapChainHooksInstalled = false;

		enum SwapChainVTableIndex : unsigned
		{
			kQueryInterface,
			kAddRef,
			kRelease,
			kGetPrivateData,
			kSetPrivateData,
			kSetPrivateDataInterface,
			kGetParent,
			kGetDevice,
			kPresent,
			kGetBuffer,
			kSetFullscreenState,
			kGetFullscreenState,
			kGetDesc,
			kResizeBuffers,
		};

		using ResizeBuffers_t = HRESULT(__stdcall*)(IDXGISwapChain3*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
		using Present_t = HRESULT(__stdcall*)(IDXGISwapChain3*, UINT, UINT);

		ResizeBuffers_t g_oldResizeBuffers = nullptr;
		Present_t       g_oldPresent = nullptr;

		HRESULT __stdcall FakeResizeBuffers(IDXGISwapChain3* a_this, UINT a_bufferCount, UINT a_width, UINT a_height, DXGI_FORMAT a_format, UINT a_flags)
		{
			// Width/height of 0 mean "size from the window" — the real
			// values are then picked up by Initialize's GetDesc on the
			// next Present, so don't clobber the stored size with zeros.
			if (a_width && a_height) {
				g_displayWidth.store(a_width, std::memory_order_relaxed);
				g_displayHeight.store(a_height, std::memory_order_relaxed);
			}
			ReleaseIfInitialized();
			return g_oldResizeBuffers(a_this, a_bufferCount, a_width, a_height, a_format, a_flags);
		}

		HRESULT __stdcall FakePresent(IDXGISwapChain3* a_this, UINT a_syncInterval, UINT a_flags)
		{
			static bool firstCall = true;
			if (firstCall) {
				firstCall = false;
				VATS_LOG("[UI] FakePresent: first call reached, swapchain=0x{:X}", reinterpret_cast<std::uintptr_t>(a_this));
			}

			static IDXGISwapChain3*   lastSwapChain = nullptr;
			static ID3D12CommandQueue* commandQueue = nullptr;

			SwapChainQueue* match = nullptr;
			for (auto& q : g_queues) {
				if (q.swapChain == a_this && (!match || q.age < match->age)) {
					match = &q;
				}
			}

			if (match) {
				if (commandQueue != match->queue) {
					commandQueue = match->queue;
					ReleaseIfInitialized();
				}
				if (lastSwapChain != a_this) {
					lastSwapChain = a_this;
					ReleaseIfInitialized();
				}
				// Bisect switch ([Debug] iOverlayStage), 2026-08-27.
				// At stage 0 this hook is a pure pass-through: no ImGui
				// frame, no D3D11-on-12 acquire/render/release, no Flush -
				// the swapchain call below is all that happens. Measured
				// flat that way; stage 2 (the normal HUD) grows ~2GB a
				// minute. Stage 1 splits the difference, see
				// InitializeOrRender.
				//
				// This exists to split one question in two. Everything
				// under InitializeOrRender runs on EVERY presented frame no
				// matter what the mod is doing, because Overlay::Draw is
				// only a callback inside it - so when the player is sitting
				// in a menu and Draw() returns immediately, this is still
				// the mod code left running. With the switch on, a run that
				// still leaks rules the render path out; a run that goes
				// flat pins the leak to it.
				if (Settings::Get().overlayStage > 0) {
					InitializeOrRender(a_this, commandQueue);
				}
			}

			return g_oldPresent(a_this, a_syncInterval, a_flags);
		}

		void InstallSwapChainHooks(FuncPtr* a_vtable)
		{
			if (g_swapChainHooksInstalled) {
				return;
			}
			g_swapChainHooksInstalled = true;

			VATS_LOG("[UI] installing swapchain hooks, vtable=0x{:X}, Present=0x{:X}, ResizeBuffers=0x{:X}",
				reinterpret_cast<std::uintptr_t>(a_vtable),
				reinterpret_cast<std::uintptr_t>(a_vtable[kPresent]),
				reinterpret_cast<std::uintptr_t>(a_vtable[kResizeBuffers]));

			g_oldPresent = reinterpret_cast<Present_t>(
				HookFunctionMH(FollowFunctionHook(a_vtable[kPresent]), reinterpret_cast<FuncPtr>(&FakePresent)));
			if (!g_oldPresent) {
				g_oldPresent = reinterpret_cast<Present_t>(
					HookVTableSlot(&a_vtable[kPresent], reinterpret_cast<FuncPtr>(&FakePresent)));
			}
			g_oldResizeBuffers = reinterpret_cast<ResizeBuffers_t>(
				HookFunctionMH(FollowFunctionHook(a_vtable[kResizeBuffers]), reinterpret_cast<FuncPtr>(&FakeResizeBuffers)));
			if (!g_oldResizeBuffers) {
				g_oldResizeBuffers = reinterpret_cast<ResizeBuffers_t>(
					HookVTableSlot(&a_vtable[kResizeBuffers], reinterpret_cast<FuncPtr>(&FakeResizeBuffers)));
			}

			VATS_LOG("[UI] swapchain hooks installed: oldPresent={} oldResizeBuffers={}",
				g_oldPresent ? "ok" : "FAILED", g_oldResizeBuffers ? "ok" : "FAILED");
		}

		using CreateSwapChainForHwnd_t = HRESULT(__stdcall*)(
			IDXGIFactory2*, ID3D12Device*, HWND, const DXGI_SWAP_CHAIN_DESC1*,
			const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**);

		CreateSwapChainForHwnd_t g_oldCreateSwapChainForHwnd = nullptr;

		HRESULT __stdcall FakeCreateSwapChainForHwnd(
			IDXGIFactory2* a_this, ID3D12Device* a_device, HWND a_hwnd,
			const DXGI_SWAP_CHAIN_DESC1* a_desc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* a_fsDesc,
			IDXGIOutput* a_output, IDXGISwapChain1** a_swapChainOut)
		{
			VATS_LOG("[UI] FakeCreateSwapChainForHwnd called, factory=0x{:X}, queueOrDevice=0x{:X}, hwnd=0x{:X}",
				reinterpret_cast<std::uintptr_t>(a_this), reinterpret_cast<std::uintptr_t>(a_device),
				reinterpret_cast<std::uintptr_t>(a_hwnd));

			if (a_hwnd) {
				g_gameHwnd = a_hwnd;
			}
			if (a_desc && a_desc->Width && a_desc->Height) {
				g_displayWidth.store(a_desc->Width, std::memory_order_relaxed);
				g_displayHeight.store(a_desc->Height, std::memory_order_relaxed);
			}

			const auto ret = g_oldCreateSwapChainForHwnd(a_this, a_device, a_hwnd, a_desc, a_fsDesc, a_output, a_swapChainOut);
			if (ret != S_OK || !a_swapChainOut || !*a_swapChainOut) {
				VATS_ERROR("[UI] FakeCreateSwapChainForHwnd: original call failed or returned null, hr=0x{:X}", static_cast<std::uint32_t>(ret));
				return ret;
			}

			VATS_LOG("[UI] FakeCreateSwapChainForHwnd succeeded, swapchain=0x{:X}",
				reinterpret_cast<std::uintptr_t>(*a_swapChainOut));

			// The "device" parameter for a D3D12 swapchain is actually the command queue.
			auto* queue = reinterpret_cast<ID3D12CommandQueue*>(a_device);
			auto* swapChain = *a_swapChainOut;

			bool inserted = false;
			for (auto& q : g_queues) {
				if (q.queue == queue) {
					q.swapChain = swapChain;
					q.age = 0;
					inserted = true;
					break;
				}
			}
			if (!inserted) {
				for (auto& q : g_queues) {
					if (!q.queue) {
						q.queue = queue;
						q.swapChain = swapChain;
						q.age = 0;
						inserted = true;
						break;
					}
					++q.age;
				}
			}
			if (!inserted) {
				auto* oldest = &g_queues[0];
				for (auto& q : g_queues) {
					if (q.age > oldest->age) {
						oldest = &q;
					}
				}
				oldest->queue = queue;
				oldest->swapChain = swapChain;
				oldest->age = 0;
			}

			if (!g_swapChainVTable) {
				g_swapChainVTable = *reinterpret_cast<FuncPtr**>(swapChain);
				InstallSwapChainHooks(g_swapChainVTable);
			}

			return ret;
		}

		using CreateDXGIFactory2_t = HRESULT(__stdcall*)(UINT, REFIID, void**);
		CreateDXGIFactory2_t g_oldCreateDXGIFactory2 = nullptr;

		HRESULT __stdcall FakeCreateDXGIFactory2(UINT a_flags, REFIID a_riid, void** a_factoryOut)
		{
			VATS_LOG("[UI] FakeCreateDXGIFactory2 called, flags=0x{:X}", a_flags);

			const auto ret = g_oldCreateDXGIFactory2(a_flags, a_riid, a_factoryOut);
			VATS_LOG("[UI] FakeCreateDXGIFactory2: original returned hr=0x{:X}, *factoryOut=0x{:X}",
				static_cast<std::uint32_t>(ret),
				(a_factoryOut && *a_factoryOut) ? reinterpret_cast<std::uintptr_t>(*a_factoryOut) : 0);

			static bool once = true;
			if (once && ret == S_OK && a_factoryOut && *a_factoryOut) {
				once = false;

				MH_Initialize();

				auto* vtable = *reinterpret_cast<FuncPtr**>(*a_factoryOut);
				constexpr unsigned kCreateSwapChainForHwnd = 15;
				VATS_LOG("[UI] factory vtable=0x{:X}, CreateSwapChainForHwnd slot=0x{:X}",
					reinterpret_cast<std::uintptr_t>(vtable), reinterpret_cast<std::uintptr_t>(vtable[kCreateSwapChainForHwnd]));

				g_oldCreateSwapChainForHwnd = reinterpret_cast<CreateSwapChainForHwnd_t>(
					HookFunctionMH(vtable[kCreateSwapChainForHwnd], reinterpret_cast<FuncPtr>(&FakeCreateSwapChainForHwnd)));

				VATS_LOG("[UI] CreateSwapChainForHwnd hook: {}", g_oldCreateSwapChainForHwnd ? "ok" : "FAILED");
			}

			return ret;
		}
	}

	void Install()
	{
		g_oldCreateDXGIFactory2 = reinterpret_cast<CreateDXGIFactory2_t>(
			HookFunctionIAT("sl.interposer.dll", "CreateDXGIFactory2", reinterpret_cast<FuncPtr>(&FakeCreateDXGIFactory2)));
		if (g_oldCreateDXGIFactory2) {
			VATS_LOG("[UI] hooked CreateDXGIFactory2 via sl.interposer.dll IAT entry (Nvidia Streamline present)");
		} else {
			g_oldCreateDXGIFactory2 = reinterpret_cast<CreateDXGIFactory2_t>(
				HookFunctionIAT("dxgi.dll", "CreateDXGIFactory2", reinterpret_cast<FuncPtr>(&FakeCreateDXGIFactory2)));
			VATS_LOG("[UI] hooked CreateDXGIFactory2 via dxgi.dll IAT entry: {}", g_oldCreateDXGIFactory2 ? "ok" : "NOT FOUND");
		}
	}

	void SetDrawCallback(DrawCallback a_callback)
	{
		g_drawCallback = a_callback;
	}

	bool GetDisplaySize(float& a_outWidth, float& a_outHeight)
	{
		const std::uint32_t w = g_displayWidth.load(std::memory_order_relaxed);
		const std::uint32_t h = g_displayHeight.load(std::memory_order_relaxed);
		if (!w || !h) {
			return false;
		}
		a_outWidth = static_cast<float>(w);
		a_outHeight = static_cast<float>(h);
		return true;
	}
}
