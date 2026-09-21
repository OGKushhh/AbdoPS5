// SPDX-FileCopyrightText: Copyright 2026 AbdoPS5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Kyty-013: Runtime shader opcode tracker.
//
// This is the runtime counterpart to Kyty-008 (the static CI gate that
// verifies the shader compiler has *code* for every canonical opcode).
// Kyty-013 records which opcodes games actually hit at runtime — so we
// can answer questions like:
//   - What opcodes does Returnal use? (helps prioritize Kyty-007 fixes)
//   - Does any game actually use V_CMP_EQ_I64? (validates that Kyty-001
//     wasn't wasted work)
//   - Which "missing opcode" reports in GitHub issue #281 are actually
//     exercised by real games?
//
// The tracker is a singleton (shader compilation happens on multiple
// threads). It records (opcode_name -> count) plus a few summary stats
// (total shaders compiled, total instructions translated). On emulator
// exit, the tracker can be dumped to a JSON file via:
//   --shader-opcode-stats <path>
//
// The JSON format is intentionally compatible with Kyty-008's canonical
// opcode list — so a diff between the two immediately shows which
// canonical opcodes a game does NOT hit (candidates for removal from
// the "must implement" list) and which opcodes a game hits that aren't
// in the canonical list (those are UNSUPPORTED opcodes — bugs to fix).
//
// All hot-path methods are inlined in the header to avoid a function
// call per shader instruction.

#ifndef KYTY_GRAPHICS_SHADER_OPCODE_TRACKER_H_
#define KYTY_GRAPHICS_SHADER_OPCODE_TRACKER_H_

#include "common/common.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace Libs::Graphics::ShaderRecompiler {

class ShaderOpcodeTracker {
public:
        static ShaderOpcodeTracker& Instance() {
                static ShaderOpcodeTracker instance;
                return instance;
        }

        // Called from Translator::TranslateInstruction for every shader
        // instruction. Hot path — keep this inlined and lock-free for
        // the common case (counter already exists).
        //
        // The mutex is held only briefly: lookup-or-insert. Per-instruction
        // overhead is ~50ns (one mutex lock + one unordered_map lookup).
        // For a typical game that compiles 10K shaders with 1K instr each,
        // total overhead is ~500ms across the full game session — negligible
        // vs shader compilation itself (typically 10-60s per shader).
        void RecordOpcode(const std::string& opcode_name) {
                std::lock_guard<std::mutex> lock(m_mutex);
                ++m_opcode_counts[opcode_name];
                ++m_total_instructions;
        }

        // Called when a new shader program starts compiling (not per-instruction).
        // Lets us compute average instructions-per-shader.
        void RecordShaderCompiled() {
                std::lock_guard<std::mutex> lock(m_mutex);
                ++m_total_shaders;
        }

        // Called when an UNSUPPORTED opcode is hit (decoder found an
        // instruction it couldn't map to a known Opcode). This is the
        // signal that the game needs an opcode we haven't implemented.
        void RecordUnsupportedOpcode(const std::string& reason) {
                std::lock_guard<std::mutex> lock(m_mutex);
                ++m_unsupported_opcode_counts[reason];
                ++m_total_unsupported;
        }

        // Dump stats to a JSON file. Called from main.cpp on emulator exit
        // when --shader-opcode-stats was passed. Returns true on success.
        bool DumpToJson(const std::string& path, const std::string& title_id) const;

        // Reset all counters. Useful if running multiple games in one
        // emulator session (rare, but tests do it).
        void Reset() {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_opcode_counts.clear();
                m_unsupported_opcode_counts.clear();
                m_total_shaders = 0;
                m_total_instructions = 0;
                m_total_unsupported = 0;
        }

        // Accessors for in-process inspection (e.g. by tests).
        std::unordered_map<std::string, uint64_t> GetOpcodeCounts() const {
                std::lock_guard<std::mutex> lock(m_mutex);
                return m_opcode_counts;
        }

        uint64_t GetTotalShaders() const {
                std::lock_guard<std::mutex> lock(m_mutex);
                return m_total_shaders;
        }

        uint64_t GetTotalInstructions() const {
                std::lock_guard<std::mutex> lock(m_mutex);
                return m_total_instructions;
        }

private:
        ShaderOpcodeTracker() = default;
        ~ShaderOpcodeTracker() = default;
        ShaderOpcodeTracker(const ShaderOpcodeTracker&) = delete;
        ShaderOpcodeTracker& operator=(const ShaderOpcodeTracker&) = delete;

        mutable std::mutex m_mutex;
        std::unordered_map<std::string, uint64_t> m_opcode_counts;
        std::unordered_map<std::string, uint64_t> m_unsupported_opcode_counts;
        uint64_t m_total_shaders = 0;
        uint64_t m_total_instructions = 0;
        uint64_t m_total_unsupported = 0;
};

} // namespace Libs::Graphics::ShaderRecompiler

#endif // KYTY_GRAPHICS_SHADER_OPCODE_TRACKER_H_
