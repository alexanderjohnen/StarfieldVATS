#pragma once

namespace VATS::UI
{
	// Draws VATS overlay content. Called once per frame by D3DHook, between
	// ImGui::NewFrame() and ImGui::Render(). Always draws a "VATS: OFF/
	// AIMING/LOCKED" status readout; in Aiming mode continuously highlights
	// whatever actor is under the crosshair (dimmer box, re-evaluated at
	// kAimScanInterval), in Locked mode tracks the actor frozen in at the
	// moment of the second hotkey press (full-brightness box, follows the
	// target's position regardless of camera direction). Body-part labels
	// and hit-chance numbers come next.
	void Draw();
}
