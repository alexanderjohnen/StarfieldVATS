#include "Settings.h"

namespace VATS
{
	namespace
	{
		// GetPrivateProfileInt only resolves relative to the process working
		// directory when the path starts with ".\" — otherwise it falls back
		// to the Windows directory. The game's working directory is the game
		// root, so this resolves next to the plugin DLL.
		constexpr auto INI_PATH = ".\\Data\\SFSE\\Plugins\\StarfieldVATS.ini";

		// REX::W32 only wraps the int/string GetPrivateProfile* variants,
		// not the float one — read as a string and parse instead of adding
		// a new dependency for one setting.
		[[nodiscard]] float GetPrivateProfileFloatA(const char* a_app, const char* a_key, float a_default, const char* a_path)
		{
			char buf[64]{};
			char defaultStr[32]{};
			std::snprintf(defaultStr, sizeof(defaultStr), "%f", a_default);
			REX::W32::GetPrivateProfileStringA(a_app, a_key, defaultStr, buf, sizeof(buf), a_path);
			return std::strtof(buf, nullptr);
		}
	}

	Settings& Settings::Get()
	{
		static Settings instance;
		return instance;
	}

	void Settings::Load()
	{
		enabled = REX::W32::GetPrivateProfileIntA("General", "bEnabled", 1, INI_PATH) != 0;
		activationKeyVK = REX::W32::GetPrivateProfileIntA("Controls", "iActivationKey", 0x51, INI_PATH);
		scannerToggleKeyVK = REX::W32::GetPrivateProfileIntA("Controls", "iScannerToggleKey", 0x51, INI_PATH);
		backKeyVK = REX::W32::GetPrivateProfileIntA("Controls", "iBackKey", 0x09, INI_PATH);
		scannerCloseMode = REX::W32::GetPrivateProfileIntA("Controls", "iScannerCloseMode", 2, INI_PATH);
		targetConeDeg = REX::W32::GetPrivateProfileIntA("Targeting", "iConeDegrees", 35, INI_PATH);
		maxTargetRange = REX::W32::GetPrivateProfileIntA("Targeting", "iMaxRange", 5000, INI_PATH);
		cameraFovDegrees = REX::W32::GetPrivateProfileIntA("Targeting", "iCameraFovDegrees", 90, INI_PATH);
		centerHitChancePercent = REX::W32::GetPrivateProfileIntA("Combat", "iCenterHitChancePercent", 95, INI_PATH);
		fullChanceRangeMeters = GetPrivateProfileFloatA("Combat", "fFullChanceRangeMeters", 12.0f, INI_PATH);
		maxEffectiveRangeMeters = GetPrivateProfileFloatA("Combat", "fMaxEffectiveRangeMeters", 45.0f, INI_PATH);
		endLockOnAds = REX::W32::GetPrivateProfileIntA("Controls", "bEndLockOnAds", 1, INI_PATH) != 0;
		adsButtonVK = REX::W32::GetPrivateProfileIntA("Controls", "iAdsButton", 0x02, INI_PATH);
		hideCrosshairWhileLocked = REX::W32::GetPrivateProfileIntA("HUD", "bHideCrosshairWhileLocked", 1, INI_PATH) != 0;
		showTargetHealth = REX::W32::GetPrivateProfileIntA("HUD", "bShowTargetHealth", 1, INI_PATH) != 0;
		lockedProjectileSpeed = GetPrivateProfileFloatA("Combat", "fLockedProjectileSpeed", 80.0f, INI_PATH);
		vatsResourceEnabled = REX::W32::GetPrivateProfileIntA("Resource", "bEnabled", 1, INI_PATH) != 0;
		vatsCapacityPerHealth = GetPrivateProfileFloatA("Resource", "fCapacityPerHealth", 1.0f, INI_PATH);
		vatsRefillPerOxygen = GetPrivateProfileFloatA("Resource", "fRefillPerOxygen", 0.5f, INI_PATH);
		vatsCostPerDamage = GetPrivateProfileFloatA("Resource", "fCostPerDamage", 2.0f, INI_PATH);
		autoAdvanceOnKill = REX::W32::GetPrivateProfileIntA("Resource", "bAutoAdvanceOnKill", 0, INI_PATH) != 0;
		autoAdvanceRangeMeters = GetPrivateProfileFloatA("Resource", "fAutoAdvanceRangeMeters", 30.0f, INI_PATH);
		autoAdvanceRequireCrosshair = REX::W32::GetPrivateProfileIntA("Resource", "bAutoAdvanceRequireCrosshair", 0, INI_PATH) != 0;
		autoAdvanceRequireEngaged = REX::W32::GetPrivateProfileIntA("Resource", "bAutoAdvanceRequireEngaged", 1, INI_PATH) != 0;
		autoAdvanceConeDeg = REX::W32::GetPrivateProfileIntA("Resource", "iAutoAdvanceConeDegrees", 60, INI_PATH);
		autoAdvanceGraceMs = REX::W32::GetPrivateProfileIntA("Resource", "iAutoAdvanceGraceMs", 1200, INI_PATH);
		aimPointRadiusFactor = GetPrivateProfileFloatA("Targeting", "fAimPointRadiusFactor", 0.25f, INI_PATH);
		aimPointMaxLiftFraction = GetPrivateProfileFloatA("Targeting", "fAimPointMaxLiftFraction", 0.5f, INI_PATH);
		ignoreFriendlyActors = REX::W32::GetPrivateProfileIntA("Targeting", "bIgnoreFriendlyActors", 1, INI_PATH) != 0;
		requireHostileTarget = REX::W32::GetPrivateProfileIntA("Targeting", "bRequireHostileTarget", 0, INI_PATH) != 0;
		hudRestoreDelayMs = REX::W32::GetPrivateProfileIntA("HUD", "iHudRestoreDelayMs", 2500, INI_PATH);
		targetBoxScale = GetPrivateProfileFloatA("HUD", "fTargetBoxScale", 0.20f, INI_PATH);
		moveCritMarker = REX::W32::GetPrivateProfileIntA("HUD", "bMoveCritMarker", 0, INI_PATH) != 0;
		critMarkerOffsetX = GetPrivateProfileFloatA("HUD", "fCritMarkerOffsetX", 0.0f, INI_PATH);
		critMarkerOffsetY = GetPrivateProfileFloatA("HUD", "fCritMarkerOffsetY", -180.0f, INI_PATH);

		REX::INFO("settings: bEnabled={}, iActivationKey=0x{:X}, iScannerToggleKey=0x{:X}, iBackKey=0x{:X}, iScannerCloseMode={}, iConeDegrees={}, iMaxRange={}, iCameraFovDegrees={}, iCenterHitChancePercent={}, fFullChanceRangeMeters={}, fMaxEffectiveRangeMeters={}, bEndLockOnAds={}, iAdsButton=0x{:X}, bHideCrosshairWhileLocked={}, bShowTargetHealth={}, fLockedProjectileSpeed={}, [Resource] bEnabled={}, fCapacityPerHealth={}, fRefillPerOxygen={}, fCostPerDamage={}, bAutoAdvanceOnKill={}, fAutoAdvanceRangeMeters={}, bAutoAdvanceRequireCrosshair={}, bAutoAdvanceRequireEngaged={}, iAutoAdvanceConeDegrees={}, iAutoAdvanceGraceMs={}, iHudRestoreDelayMs={}, fTargetBoxScale={}, fAimPointRadiusFactor={}, fAimPointMaxLiftFraction={}, bIgnoreFriendlyActors={}, bRequireHostileTarget={}, bMoveCritMarker={}, fCritMarkerOffsetX={}, fCritMarkerOffsetY={}",
			enabled, activationKeyVK, scannerToggleKeyVK, backKeyVK, scannerCloseMode, targetConeDeg, maxTargetRange, cameraFovDegrees, centerHitChancePercent, fullChanceRangeMeters, maxEffectiveRangeMeters, endLockOnAds, adsButtonVK, hideCrosshairWhileLocked, showTargetHealth, lockedProjectileSpeed, vatsResourceEnabled, vatsCapacityPerHealth, vatsRefillPerOxygen, vatsCostPerDamage, autoAdvanceOnKill, autoAdvanceRangeMeters, autoAdvanceRequireCrosshair, autoAdvanceRequireEngaged, autoAdvanceConeDeg, autoAdvanceGraceMs, hudRestoreDelayMs, targetBoxScale, aimPointRadiusFactor, aimPointMaxLiftFraction, ignoreFriendlyActors, requireHostileTarget, moveCritMarker, critMarkerOffsetX, critMarkerOffsetY);
	}
}
