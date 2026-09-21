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
        std::string enable_hacks;
        // Kyty-005: PKG file to extract and mount.
        std::filesystem::path mount_pkg;
        // Kyty-013: if non-empty, dump shader opcode stats to JSON on exit.
        std::filesystem::path shader_opcode_stats_path;
};

void Run(const RunOptions& options);

} // namespace Emulator

#endif /* EMULATOR_INCLUDE_EMULATOR_EMULATOR_H_ */
