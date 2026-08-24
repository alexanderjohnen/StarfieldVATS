#include "HealthWidgetReader.h"

#include <string>
#include <vector>

namespace VATS
{
	namespace
	{
		constexpr const char* kMcNames[] = {
			"EnemyHealthMeter_mc",
		};
		constexpr const char* kFallbackParents[] = {
			"_root",
			"_root.HUDMovieBaseInstance",
			"_root.EnemyHealthHolder_mc",
			"_root.HUDMovieBaseInstance.EnemyHealthHolder_mc",
		};
		// Property suffixes tried on each resolved MC path. Deliberately
		// does NOT include "" (the bare clip reference itself) - found
		// 2026-08-24 to be a hard, silent, log-less crash: a bare clip
		// resolves to a kDisplayObject-typed Value (a live, refcounted
		// Scaleform object reference), and when that local Value goes out
		// of scope its destructor calls
		// Value::ObjectInterface::ObjectRelease - a REL::Relocation call
		// (ID 169746) this project has never exercised before, and it
		// crashed the instant it did, with none of the usual "Failed to
		// find offset for Address Library ID" abort dialog/log line that
		// TESHitEvent/PlayerIronSightsStartEvent produced (see
		// commonlibsf-unmapped-ids memory) - a genuinely wild call, not a
		// clean unmapped-ID abort. Every remaining suffix here names an
		// actual property (AS3 widget-property guesses, then legacy AS2
		// display-object properties as a last resort - a bar-fill effect
		// via _xscale/_width is a known Bethesda HUD technique) and is
		// expected to resolve to a primitive (Number/Int/UInt/Boolean/
		// String) if it resolves at all - GetVariable simply returns false
		// for a path that doesn't exist, which is safe. Do not add "" or
		// any other suffix that could plausibly resolve to another
		// clip/object back to this list without a different mechanism for
		// handling managed Values safely.
		constexpr const char* kPropertySuffixes[] = {
			".percent", ".currentPercent", ".healthPercent", ".value",
			".currentValue", ".currHealth", "._xscale", "._width"
		};

		enum class ProbeState
		{
			kNotRun,
			kFoundCandidate,
			kNothingFound,
		};

		ProbeState  s_state = ProbeState::kNotRun;
		std::string s_candidatePath;

		[[nodiscard]] RE::Scaleform::GFx::ASMovieRootBase* GetHudMovieRoot()
		{
			auto* ui = RE::UI::GetSingleton();
			auto  movie = ui ? ui->GetMenuMovie("HUDMenu") : nullptr;
			return movie ? movie->asMovieRoot.get() : nullptr;
		}

		[[nodiscard]] bool IsNumericType(RE::Scaleform::GFx::Value::ValueType a_type)
		{
			using VT = RE::Scaleform::GFx::Value::ValueType;
			return a_type == VT::kNumber || a_type == VT::kInt || a_type == VT::kUInt;
		}

		[[nodiscard]] double ExtractNumeric(const RE::Scaleform::GFx::Value& a_val)
		{
			using VT = RE::Scaleform::GFx::Value::ValueType;
			switch (a_val.GetType()) {
			case VT::kNumber:
				return a_val.GetNumber();
			case VT::kInt:
				return static_cast<double>(a_val.GetInt());
			case VT::kUInt:
				return static_cast<double>(a_val.GetUInt());
			default:
				return 0.0;
			}
		}

		// Logs whatever type GetVariable actually returned. Deliberately
		// avoids Value::GetBoolean()/GetNumber() et al. blind - those
		// assert on a type mismatch, and this is a diagnostic probe over
		// paths whose real type we don't know yet.
		void LogValue(const std::string& a_path, const RE::Scaleform::GFx::Value& a_val)
		{
			using VT = RE::Scaleform::GFx::Value::ValueType;
			switch (a_val.GetType()) {
			case VT::kBoolean:
				REX::INFO("[VATS] health-widget: '{}' = bool {}", a_path, a_val.GetBoolean());
				break;
			case VT::kInt:
				REX::INFO("[VATS] health-widget: '{}' = int {}", a_path, a_val.GetInt());
				break;
			case VT::kUInt:
				REX::INFO("[VATS] health-widget: '{}' = uint {}", a_path, a_val.GetUInt());
				break;
			case VT::kNumber:
				REX::INFO("[VATS] health-widget: '{}' = number {:.3f}", a_path, a_val.GetNumber());
				break;
			case VT::kString:
				REX::INFO("[VATS] health-widget: '{}' = string '{}'", a_path, a_val.GetString());
				break;
			default:
				REX::INFO("[VATS] health-widget: '{}' = (type {})", a_path, static_cast<int>(a_val.GetType()));
				break;
			}
		}

		// One-time (per process) broad probe across parent paths x property
		// suffixes, logging every resolving combination - see
		// HealthWidgetReader.h for why this can't just be one confirmed
		// path yet. Picks the first numeric-typed hit as a best-effort
		// candidate, in probe order (real HUDMenu root path first, then the
		// static fallbacks) - unconfirmed, see header.
		void Probe()
		{
			s_state = ProbeState::kNothingFound;

			auto* root = GetHudMovieRoot();
			if (!root) {
				REX::WARN("[VATS] health-widget: HUDMenu movie unavailable, probe skipped");
				return;
			}

			std::vector<std::string> parents;
			auto*                    ui = RE::UI::GetSingleton();
			auto                     menu = ui ? ui->GetMenu("HUDMenu") : nullptr;
			if (menu) {
				if (const char* realRoot = menu->GetRootPath(); realRoot && realRoot[0] != '\0') {
					REX::INFO("[VATS] health-widget: HUDMenu root path = '{}'", realRoot);
					parents.push_back(realRoot);
					parents.push_back(std::string(realRoot) + ".HUDMovieBaseInstance");
					parents.push_back(std::string(realRoot) + ".EnemyHealthHolder_mc");
					parents.push_back(std::string(realRoot) + ".HUDMovieBaseInstance.EnemyHealthHolder_mc");
				}
			}
			for (const char* fallback : kFallbackParents) {
				parents.push_back(fallback);
			}

			bool foundAny = false;
			for (const auto& parent : parents) {
				for (const char* mc : kMcNames) {
					const std::string mcPath = parent + "." + mc;
					for (const char* suffix : kPropertySuffixes) {
						const std::string           path = mcPath + suffix;
						RE::Scaleform::GFx::Value val;
						if (!root->GetVariable(&val, path.c_str())) {
							continue;
						}
						foundAny = true;
						LogValue(path, val);
						if (s_state != ProbeState::kFoundCandidate && IsNumericType(val.GetType())) {
							s_candidatePath = path;
							s_state = ProbeState::kFoundCandidate;
							REX::INFO("[VATS] health-widget: using '{}' as best-effort candidate (UNCONFIRMED - verify against a real hit)", path);
						}
					}
				}
			}
			if (!foundAny) {
				REX::WARN("[VATS] health-widget: no candidate path resolved at all - EnemyHealthMeter_mc may be nested differently than guessed, or not populated without an active native health display");
			}
		}
	}

	bool HealthWidgetReader::GetLiveHealthPercent(float& a_outPercent)
	{
		if (s_state == ProbeState::kNotRun) {
			Probe();
		}
		if (s_state != ProbeState::kFoundCandidate) {
			return false;
		}
		auto* root = GetHudMovieRoot();
		if (!root) {
			return false;
		}
		RE::Scaleform::GFx::Value val;
		if (!root->GetVariable(&val, s_candidatePath.c_str()) || !IsNumericType(val.GetType())) {
			return false;
		}
		const double raw = ExtractNumeric(val);
		// Normalizes a plausible 0..1 or 0..100 reading to a 0..100
		// percent - percent-style widgets are usually one or the other.
		// Anything outside both ranges is rejected rather than drawing a
		// nonsense bar (e.g. an _xscale/_width read that isn't actually a
		// percentage at all).
		double percent;
		if (raw >= 0.0 && raw <= 1.0) {
			percent = raw * 100.0;
		} else if (raw > 1.0 && raw <= 100.0) {
			percent = raw;
		} else {
			return false;
		}
		a_outPercent = static_cast<float>(percent);
		return true;
	}
}
