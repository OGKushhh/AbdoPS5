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
	// E.g. "DisableAsyncCompute,ForceDepthRangeRestricted". Applied after
	// HackFeatures::Init() loads the hardcoded map + data/game_hacks.json.
	std::string enable_hacks;
};

void Run(const RunOptions& options);

} // namespace Emulator

#endif /* EMULATOR_INCLUDE_EMULATOR_EMULATOR_H_ */
