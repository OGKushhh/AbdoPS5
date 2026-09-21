#ifndef EMULATOR_INCLUDE_EMULATOR_EMULATOR_H_
#define EMULATOR_INCLUDE_EMULATOR_EMULATOR_H_

#include "common/emulatorConfig.h"
#include "common/stringUtils.h"

#include <filesystem>

namespace Emulator {

struct RunOptions {
	Config::ConfigOptions config;
	std::filesystem::path app0_dir;
	std::filesystem::path elf;
	std::filesystem::path game_patch;
	// Kyty-003: Comma-separated list of hack names to enable at runtime.
<<<<<<< HEAD
	// E.g. "DisableAsyncCompute,ForceDepthRangeRestricted". Applied after
	// HackFeatures::Init() loads the hardcoded map + data/game_hacks.json.
	std::string enable_hacks;
=======
	std::string enable_hacks;
	// Kyty-005: PKG file to extract and mount. When set, the PKG is
	// extracted to a temp directory and used as the app0_dir.
	std::filesystem::path mount_pkg;
>>>>>>> 1eba625 (loader: Add PKG file format support (Kyty-005))
};

void Run(const RunOptions& options);

} // namespace Emulator

#endif /* EMULATOR_INCLUDE_EMULATOR_EMULATOR_H_ */
