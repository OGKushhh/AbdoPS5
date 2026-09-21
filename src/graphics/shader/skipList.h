#ifndef KYTY_GRAPHICS_SHADER_SKIP_LIST_H_
#define KYTY_GRAPHICS_SHADER_SKIP_LIST_H_

#include "common/common.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_set>

namespace Libs::Graphics {

// Kyty-015: Per-game shader skip list.
//
// Loads a JSON file (data/shader_skips/<title_id>.json) containing shader
// hashes that should be skipped during compilation. When a shader hash is
// in the skip list, the shader recompiler logs a warning and returns a
// no-op shader instead of compiling it.
//
// This is useful for:
// - Known-broken shaders that crash the emulator
// - Shaders that use unimplemented opcodes (until Kyty-001 is verified)
// - Debugging specific shader issues without recompiling
//
// JSON format:
// {
//   "skips": [
//     "0x1234567890ABCDEF",
//     "0xFEDCBA0987654321"
//   ],
//   "skip_all_compute": false,
//   "skip_all_graphics": false
// }
//
// Usage:
//   ShaderSkipList::Init(title_id);
//   if (ShaderSkipList::ShouldSkip(hash)) { ... skip ... }

class ShaderSkipList {
public:
	// Load the skip list for the given title ID.
	// Searches for data/shader_skips/<title_id>.json, then
	// data/shader_skips.json (global skip list).
	static void Init(const std::string& title_id);

	// Check if a shader hash should be skipped.
	static bool ShouldSkip(uint64_t hash);

	// Check if all compute shaders should be skipped.
	static bool SkipAllCompute() { return s_skip_all_compute; }

	// Check if all graphics shaders should be skipped.
	static bool SkipAllGraphics() { return s_skip_all_graphics; }

	// Get the number of shaders in the skip list.
	static size_t GetSkipCount() { return s_skip_hashes.size(); }

	// Add a hash to the skip list at runtime (e.g. when a shader crashes).
	static void AddSkip(uint64_t hash);

	// Save the current skip list to disk.
	static bool Save();

private:
	friend class ShaderSkipListAccess;
	static std::unordered_set<uint64_t> s_skip_hashes;
	static bool                         s_skip_all_compute;
	static bool                         s_skip_all_graphics;
	static std::filesystem::path        s_loaded_path;
	static bool                         s_initialized;
};

} // namespace Libs::Graphics

#endif // KYTY_GRAPHICS_SHADER_SKIP_LIST_H_
