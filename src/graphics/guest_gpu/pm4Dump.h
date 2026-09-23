#ifndef EMULATOR_SRC_GRAPHICS_GUEST_GPU_PM4DUMP_H_
#define EMULATOR_SRC_GRAPHICS_GUEST_GPU_PM4DUMP_H_

// Kyty-039: PM4 command stream dump tool
//
// Records the raw PM4 command stream to a file for offline analysis,
// inspired by PS5PCEM's PM4 dump tool. When the user passes
// `--dump-pm4 <path>` (or sets `pm4_dump_enabled = true` in config),
// every PM4 packet processed by CommandProcessor::ProcessPm4() is
// appended to a text file with:
//   - Packet offset (in dwords)
//   - Opcode name (e.g. IT_DRAW_INDEX_AUTO)
//   - Register slot (for SET_* packets)
//   - Packet length (in dwords)
//   - Raw dword payload (hex)
//
// The output is replayable by a future test harness and human-readable
// for debugging GPU hangs.

#include "common/abi.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <span>
#include <string_view>

namespace Libs::Graphics::Pm4Dump {

// Initialize the dump file at `path`. Creates the file (truncating if it
// already exists) and writes a header line. Returns true on success.
// Called once at startup if Config::Pm4DumpEnabled() is true.
[[nodiscard]] bool Initialize(const std::filesystem::path& path);

// Append a single PM4 packet to the dump file. Called from
// CommandProcessor::ProcessPm4() for every packet processed.
//
// Parameters:
//   offset_dw    — packet offset within the current command buffer
//   packet_header — the raw packet header dword (contains opcode + count)
//   payload      — span of the packet's payload dwords (after the header)
void DumpPacket(uint32_t offset_dw, uint32_t packet_header,
                std::span<const uint32_t> payload);

// Close the dump file. Called at shutdown.
void Shutdown();

// True if Initialize() succeeded and the dump file is open for writing.
[[nodiscard]] bool IsEnabled() noexcept;

} // namespace Libs::Graphics::Pm4Dump

#endif // EMULATOR_SRC_GRAPHICS_GUEST_GPU_PM4DUMP_H_
