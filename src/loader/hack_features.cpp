#include "loader/hack_features.h"

#include "common/assert.h"
#include "common/file.h"
#include "common/logging/log.h"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <unordered_map>

namespace Loader {

// Static state
uint32_t    HackFeatures::s_active_hacks  = 0;
std::string HackFeatures::s_game_serial;
bool        HackFeatures::s_initialized   = false;

namespace {

using Json = nlohmann::json;

// Hardcoded map: PPSA serial -> comma-separated hack names.
// Each entry is a known-broken game with a documented workaround.
//
// These workarounds are derived from:
// - KytyPS5 revert hunt (5 depth-texture reverts in 10 days → DisableAsyncCompute)
// - KytyPS5 issue tracker (Sifu #739, Returnal #742, Spider-Man #701, Demon's Souls #697)
// - shadPS4 Shadlix fork patterns (The Order: 1886, RE3 Remake)
//
// Users can override these via data/game_hacks.json (see LoadOverrides()).
const std::unordered_map<std::string, std::string> kHardcodedGameHacks = {
	// Kyty-006 workaround: depth/comparison-texture revert cluster.
	// These 4 AAA titles are blocked by the 5 depth-texture reverts (Aug 25 - Sep 7).
	// DisableAsyncCompute forces synchronous compute, sidestepping the async-completion
	// interrupt path that the reverted PRs destabilized.
	// ForceDepthRangeRestricted clamps depth ranges to [0,1] — the safe path that
	// doesn't require VK_EXT_depth_range_unrestricted.
	{"PPSA01491", "DisableAsyncCompute,ForceDepthRangeRestricted"}, // Sifu (#739)
	{"PPSA01256", "DisableAsyncCompute,ForceDepthRangeRestricted"}, // Returnal (#742)
	{"PPSA01323", "DisableAsyncCompute,ForceDepthRangeRestricted"}, // Spider-Man Remastered (#701)
	{"PPSA01256", "DisableAsyncCompute,ForceDepthRangeRestricted"}, // Demon's Souls (#697) — same PPSA as Returnal? verify

	// Kyty-001 workaround: shader ASSERT failures on unverified games.
	// SkipShaderAssert lets users boot past the ASSERT with visual artifacts
	// while Kyty-001's full opcode matrix is being verified on real hardware.
	{"PPSA01325", "SkipShaderAssert"}, // Ghost of Tsushima (#726) — likely hits a missing opcode
	{"PPSA01521", "SkipShaderAssert"}, // Teardown (#707)

	// Additional known-broken games from the DoesntBoot list
	{"PPSA01341", "SkipShaderAssert"}, // 2 reports, multi-region

	// Bloodborne (CUSA00900 US, CUSA00231 EU, CUSA03173 GOTY)
	// FromSoftware engine: uses older GCN features, 32-bit integer
	// compares, standard tiling. Should work with minimal hacks.
	// Known issues from shadPS4 Shadlix fork:
	// - Audio loss (needs BloodborneAudioFix)
	// - PM4 Type 0 packets (needs Pm4Type0Fix)
	// - High memory (recommend MemoryBound on 8GB hosts)
	{"CUSA00900", "BloodborneAudioFix,Pm4Type0Fix,MemoryBound"}, // Bloodborne US
	{"CUSA00231", "BloodborneAudioFix,Pm4Type0Fix,MemoryBound"}, // Bloodborne EU
	{"CUSA03173", "BloodborneAudioFix,Pm4Type0Fix,MemoryBound"}, // Bloodborne GOTY (Old Hunters)
};

// Map hack name strings to GameHack enum values.
const std::unordered_map<std::string, GameHack> kHackNameMap = {
	{"DepthDisable",              GameHack::DepthDisable},
	{"ComputeDisable",            GameHack::ComputeDisable},
	{"DisableAsyncCompute",       GameHack::DisableAsyncCompute},
	{"DisableSRGB",               GameHack::DisableSRGB},
	{"DisableFMV",                GameHack::DisableFMV},
	{"SkipUnknownTiling",         GameHack::SkipUnknownTiling},
	{"ForceDepthRangeRestricted", GameHack::ForceDepthRangeRestricted},
	{"UseColorImageForComparison", GameHack::UseColorImageForComparison},
	{"SkipShaderAssert",          GameHack::SkipShaderAssert},
	{"ImageLoadNoReload",          GameHack::ImageLoadNoReload},
	{"MemoryBound",               GameHack::MemoryBound},
	{"ForcePs4ProMode",           GameHack::ForcePs4ProMode},
	{"ForceDevKitMode",           GameHack::ForceDevKitMode},
	{"BloodborneAudioFix",        GameHack::BloodborneAudioFix},
	{"Pm4Type0Fix",              GameHack::Pm4Type0Fix},
};

// Load data/game_hacks.json if it exists. Format:
// {
//   "PPSA01491": ["DisableAsyncCompute", "ForceDepthRangeRestricted"],
//   "PPSA01325": ["SkipShaderAssert"]
// }
//
// These overrides are merged on top of the hardcoded map (OR'd together),
// so users can ADD hacks to a game without removing the hardcoded ones.
//
// Search order:
// 1. data/game_hacks.json (relative to working directory)
// 2. game_hacks.json (relative to working directory, for portable installs)
bool LoadOverrides(uint32_t* out_mask, std::string_view game_serial) {
	std::filesystem::path hacks_path;

	// Try data/game_hacks.json first
	const auto data_path = std::filesystem::path("data") / "game_hacks.json";
	if (Common::File::IsFileExisting(data_path)) {
		hacks_path = data_path;
	} else if (Common::File::IsFileExisting("game_hacks.json")) {
		// Fall back to working directory
		hacks_path = "game_hacks.json";
	} else {
		return false; // Not an error — file is optional
	}

	std::ifstream file(hacks_path, std::ios::binary);
	if (!file) {
		return false;
	}

	const auto root = Json::parse(file, nullptr, false);
	if (!root.is_object()) {
		LOGF("game_hacks.json: invalid JSON (expected object at root)\n");
		return false;
	}

	const auto serial_str = std::string(game_serial);
	if (!root.contains(serial_str)) {
		return false; // No overrides for this game
	}

	const auto& hacks = root[serial_str];
	if (!hacks.is_array()) {
		LOGF("game_hacks.json: expected array for %s\n", serial_str.c_str());
		return false;
	}

	uint32_t mask = 0;
	for (const auto& hack_name : hacks) {
		if (!hack_name.is_string()) {
			continue;
		}
		const auto name = hack_name.get<std::string>();
		const auto it = kHackNameMap.find(name);
		if (it == kHackNameMap.end()) {
			LOGF("game_hacks.json: unknown hack name '%s' for %s\n",
			     name.c_str(), serial_str.c_str());
			continue;
		}
		mask |= static_cast<uint32_t>(it->second);
	}

	*out_mask = mask;
	return true;
}

} // namespace

void HackFeatures::Init(std::string_view game_serial) {
	EXIT_IF(s_initialized);
	s_initialized = true;
	s_game_serial = std::string(game_serial);

	// 1. Apply hardcoded hacks
	uint32_t mask = 0;
	const auto hardcoded_it = kHardcodedGameHacks.find(std::string(game_serial));
	if (hardcoded_it != kHardcodedGameHacks.end()) {
		mask |= ParseHackList(hardcoded_it->second);
	}

	// 2. Merge overrides from data/game_hacks.json (if present)
	uint32_t override_mask = 0;
	if (LoadOverrides(&override_mask, game_serial)) {
		LOGF("HackFeatures: loaded overrides for %s (mask=0x%08x)\n",
		     std::string(game_serial).c_str(), override_mask);
		mask |= override_mask;
	}

	s_active_hacks = mask;

	if (mask != 0) {
		LOGF("HackFeatures: %s active hacks: 0x%08x\n",
		     std::string(game_serial).c_str(), mask);
	}
}

bool HackFeatures::HasHack(GameHack hack) {
	return (s_active_hacks & static_cast<uint32_t>(hack)) != 0;
}

GameHack HackFeatures::ParseHackName(std::string_view name) {
	const auto it = kHackNameMap.find(std::string(name));
	if (it == kHackNameMap.end()) {
		return GameHack::None;
	}
	return it->second;
}

uint32_t HackFeatures::ParseHackList(std::string_view list) {
	uint32_t mask = 0;
	// Split on commas
	size_t start = 0;
	while (start < list.size()) {
		// Skip leading whitespace
		while (start < list.size() && (list[start] == ' ' || list[start] == ',')) {
			start++;
		}
		if (start >= list.size()) {
			break;
		}
		size_t end = start;
		while (end < list.size() && list[end] != ',' && list[end] != ' ') {
			end++;
		}
		const auto name = std::string(list.substr(start, end - start));
		const auto hack = ParseHackName(name);
		if (hack != GameHack::None) {
			mask |= static_cast<uint32_t>(hack);
		} else {
			LOGF("HackFeatures: unknown hack name '%s' in list\n", name.c_str());
		}
		start = end;
	}
	return mask;
}

void HackFeatures::EnableHacks(uint32_t hack_mask) {
	s_active_hacks |= hack_mask;
	LOGF("HackFeatures: enabled additional hacks (mask=0x%08x, total=0x%08x)\n",
	     hack_mask, s_active_hacks);
}

} // namespace Loader
