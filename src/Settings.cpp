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
		assistRadius = GetPrivateProfileFloatA("Combat", "fAssistRadius", 0.15f, INI_PATH);

		REX::INFO("settings: bEnabled={}, iActivationKey=0x{:X}, iScannerToggleKey=0x{:X}, iBackKey=0x{:X}, iScannerCloseMode={}, iConeDegrees={}, iMaxRange={}, iCameraFovDegrees={}, iCenterHitChancePercent={}, fAssistRadius={}",
			enabled, activationKeyVK, scannerToggleKeyVK, backKeyVK, scannerCloseMode, targetConeDeg, maxTargetRange, cameraFovDegrees, centerHitChancePercent, assistRadius);
	}
}
