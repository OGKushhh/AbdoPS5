#ifndef KYTY_LOADER_HACK_FEATURES_H_
#define KYTY_LOADER_HACK_FEATURES_H_

#include "common/common.h"

#include <string>
#include <string_view>
#include <vector>

namespace Loader {

// Kyty-003: Per-game hack flags framework.
//
// Inspired by fpPS4's CLI hack flags (-h DEPTH_DISABLE_HACK etc.) and the
// shadPS4 Shadlix fork's HackFeatures class. Provides named feature flags
// that gate render paths at runtime, allowing users to work around
// emulator bugs that block specific games.
//
// Two layers of configuration:
// 1. Hardcoded map in hack_features.cpp (quick fixes for known broken games)
// 2. Data-driven overrides from data/game_hacks.json (community-editable)
//
// Per-game .ini config (if implemented later) overrides both.
//
// Usage:
//   HackFeatures::Init(title_id);  // Call once after param.sfo parse
//   if (HackFeatures::HasHack(GameHack::DisableAsyncCompute)) { ... }

enum class GameHack : uint32_t {
	None = 0,

	// Render hacks
	DepthDisable           = 1u << 0u,  // Disable depth buffer (workaround for depth-texture crashes)
	ComputeDisable         = 1u << 1u,  // Disable compute shaders (debugging)
	DisableAsyncCompute    = 1u << 2u,  // Force synchronous compute (Kyty-006 workaround)
	DisableSRGB            = 1u << 3u,  // Disable sRGB display
	DisableFMV             = 1u << 4u,  // Disable full-motion video playback
	SkipUnknownTiling      = 1u << 5u,  // Skip unknown texture tiling types
	ForceDepthRangeRestricted = 1u << 6u,  // Clamp depth ranges to [0,1] (Kyty-006 workaround)
	UseColorImageForComparison = 1u << 7u,  // Fall back to color images for comparison textures (lossy)

	// Shader hacks
	SkipShaderAssert       = 1u << 8u,  // Skip shader ASSERT failures (Kyty-001 workaround for unverified games)

	// Memory hacks
	ImageLoadNoReload      = 1u << 9u,  // Never reload textures (performance hack)
	MemoryBound            = 1u << 10u, // Limit GPU-allocated memory (iGPU workaround)

	// Console variant hacks
	ForcePs4ProMode        = 1u << 11u, // Force PS4 Pro mode (already exists as a setting)
	ForceDevKitMode        = 1u << 12u, // Force DevKit mode
};

class HackFeatures {
public:
	// Must be called exactly once during emulator startup, after param.sfo
	// is parsed and the title_id is available. Loads the hardcoded map and
	// merges any overrides from data/game_hacks.json.
	static void Init(std::string_view game_serial);

	// O(1) lookup. Returns false if Init() has not been called.
	static bool HasHack(GameHack hack);

	// Returns the full bitmask (for debugging / logging).
	static uint32_t GetMask() { return s_active_hacks; }

	// Returns the game serial that was passed to Init().
	static std::string_view GetGameSerial() { return std::string_view(s_game_serial); }

	// Parse a hack name string ("DisableAsyncCompute") to a GameHack enum.
	// Returns GameHack::None if the name is unknown.
	static GameHack ParseHackName(std::string_view name);

	// Parse a comma-separated list of hack names into a bitmask.
	static uint32_t ParseHackList(std::string_view list);

	// Apply additional hacks at runtime (e.g. from --enable-hack CLI flag).
	// OR's into the active mask.
	static void EnableHacks(uint32_t hack_mask);

private:
	static uint32_t      s_active_hacks;
	static std::string   s_game_serial;
	static bool          s_initialized;
};

} // namespace Loader

#endif // KYTY_LOADER_HACK_FEATURES_H_
