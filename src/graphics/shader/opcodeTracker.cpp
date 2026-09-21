// SPDX-FileCopyrightText: Copyright 2026 AbdoPS5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Kyty-013: Runtime shader opcode tracker — implementation.

#include "graphics/shader/opcodeTracker.h"

#include "common/logging/log.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace Libs::Graphics::ShaderRecompiler {

bool ShaderOpcodeTracker::DumpToJson(const std::string& path, const std::string& title_id) const {
        std::unordered_map<std::string, uint64_t> opcode_counts_copy;
        std::unordered_map<std::string, uint64_t> unsupported_counts_copy;
        uint64_t total_shaders;
        uint64_t total_instructions;
        uint64_t total_unsupported;
        {
                std::lock_guard<std::mutex> lock(m_mutex);
                opcode_counts_copy = m_opcode_counts;
                unsupported_counts_copy = m_unsupported_opcode_counts;
                total_shaders = m_total_shaders;
                total_instructions = m_total_instructions;
                total_unsupported = m_total_unsupported;
        }

        std::vector<std::pair<std::string, uint64_t>> sorted_opcodes(
            opcode_counts_copy.begin(), opcode_counts_copy.end());
        std::sort(sorted_opcodes.begin(), sorted_opcodes.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });

        std::vector<std::pair<std::string, uint64_t>> sorted_unsupported(
            unsupported_counts_copy.begin(), unsupported_counts_copy.end());
        std::sort(sorted_unsupported.begin(), sorted_unsupported.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });

        nlohmann::json root;
        root["title_id"] = title_id;
        root["total_shaders"] = total_shaders;
        root["total_instructions"] = total_instructions;
        root["total_unsupported"] = total_unsupported;

        nlohmann::json opcode_counts_json = nlohmann::json::object();
        for (const auto& [name, count] : sorted_opcodes) {
                opcode_counts_json[name] = count;
        }
        root["opcode_counts"] = opcode_counts_json;

        nlohmann::json unsupported_json = nlohmann::json::object();
        for (const auto& [name, count] : sorted_unsupported) {
                unsupported_json[name] = count;
        }
        root["unsupported_opcode_counts"] = unsupported_json;

        std::filesystem::path file_path(path);
        if (file_path.has_parent_path()) {
                std::error_code ec;
                std::filesystem::create_directories(file_path.parent_path(), ec);
        }

        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) {
                LOGF("ShaderOpcodeTracker: cannot open %s for writing\n", path.c_str());
                return false;
        }

        out << root.dump(2) << std::endl;
        if (!out) {
                LOGF("ShaderOpcodeTracker: failed to write to %s\n", path.c_str());
                return false;
        }

        LOGF("ShaderOpcodeTracker: wrote %zu opcodes (%zu unsupported) across %llu shaders "
             "(%llu instructions) to %s\n",
             opcode_counts_copy.size(), unsupported_counts_copy.size(),
             static_cast<unsigned long long>(total_shaders),
             static_cast<unsigned long long>(total_instructions), path.c_str());
        return true;
}

} // namespace Libs::Graphics::ShaderRecompiler
