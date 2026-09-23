// Kyty-039: PM4 command stream dump tool — implementation.
//
// See pm4Dump.h for design rationale.

#include "graphics/guest_gpu/pm4Dump.h"

#include "common/assert.h"
#include "common/file.h"
#include "graphics/guest_gpu/pm4.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace Libs::Graphics::Pm4Dump {

namespace {

std::ofstream g_dump_file;
std::mutex    g_dump_mutex;
bool          g_enabled = false;

// Kyty-039: opcode → human-readable name table.
// Covers all 54 IT_ opcodes defined in pm4.h.
struct OpcodeEntry {
        uint32_t    opcode;
        const char* name;
};

constexpr std::array<OpcodeEntry, 54> kOpcodes = {{
    {Pm4::IT_NOP,                       "IT_NOP"},
    {Pm4::IT_SET_BASE,                  "IT_SET_BASE"},
    {Pm4::IT_CLEAR_STATE,              "IT_CLEAR_STATE"},
    {Pm4::IT_INDEX_BUFFER_SIZE,        "IT_INDEX_BUFFER_SIZE"},
    {Pm4::IT_DISPATCH_DIRECT,          "IT_DISPATCH_DIRECT"},
    {Pm4::IT_DISPATCH_INDIRECT,        "IT_DISPATCH_INDIRECT"},
    {Pm4::IT_SET_PREDICATION,          "IT_SET_PREDICATION"},
    {Pm4::IT_COND_EXEC,                "IT_COND_EXEC"},
    {Pm4::IT_DRAW_INDIRECT,            "IT_DRAW_INDIRECT"},
    {Pm4::IT_DRAW_INDEX_INDIRECT,      "IT_DRAW_INDEX_INDIRECT"},
    {Pm4::IT_INDEX_BASE,               "IT_INDEX_BASE"},
    {Pm4::IT_DRAW_INDEX_2,             "IT_DRAW_INDEX_2"},
    {Pm4::IT_CONTEXT_CONTROL,          "IT_CONTEXT_CONTROL"},
    {Pm4::IT_INDEX_TYPE,               "IT_INDEX_TYPE"},
    {Pm4::IT_DRAW_INDIRECT_MULTI,      "IT_DRAW_INDIRECT_MULTI"},
    {Pm4::IT_DRAW_INDEX_AUTO,         "IT_DRAW_INDEX_AUTO"},
    {Pm4::IT_NUM_INSTANCES,            "IT_NUM_INSTANCES"},
    {Pm4::IT_INDIRECT_BUFFER_CNST,     "IT_INDIRECT_BUFFER_CNST"},
    {Pm4::IT_DRAW_INDEX_OFFSET_2,      "IT_DRAW_INDEX_OFFSET_2"},
    {Pm4::IT_WRITE_DATA,               "IT_WRITE_DATA"},
    {Pm4::IT_MEM_SEMAPHORE,            "IT_MEM_SEMAPHORE"},
    {Pm4::IT_DRAW_INDEX_INDIRECT_MULTI,"IT_DRAW_INDEX_INDIRECT_MULTI"},
    {Pm4::IT_DISPATCH_DRAW_PREAMBLE,   "IT_DISPATCH_DRAW_PREAMBLE"},
    {Pm4::IT_WAIT_REG_MEM,             "IT_WAIT_REG_MEM"},
    {Pm4::IT_INDIRECT_BUFFER,          "IT_INDIRECT_BUFFER"},
    {Pm4::IT_COPY_DATA,                "IT_COPY_DATA"},
    {Pm4::IT_CP_DMA,                   "IT_CP_DMA"},
    {Pm4::IT_PFP_SYNC_ME,              "IT_PFP_SYNC_ME"},
    {Pm4::IT_SURFACE_SYNC,             "IT_SURFACE_SYNC"},
    {Pm4::IT_EVENT_WRITE,              "IT_EVENT_WRITE"},
    {Pm4::IT_EVENT_WRITE_EOP,          "IT_EVENT_WRITE_EOP"},
    {Pm4::IT_EVENT_WRITE_EOS,          "IT_EVENT_WRITE_EOS"},
    {Pm4::IT_RELEASE_MEM,              "IT_RELEASE_MEM"},
    {Pm4::IT_DMA_DATA,                 "IT_DMA_DATA"},
    {Pm4::IT_ACQUIRE_MEM,              "IT_ACQUIRE_MEM"},
    {Pm4::IT_REWIND,                   "IT_REWIND"},
    {Pm4::IT_SET_SH_REG_INDIRECT,     "IT_SET_SH_REG_INDIRECT"},
    {Pm4::IT_SET_UCONFIG_REG_INDIRECT,"IT_SET_UCONFIG_REG_INDIRECT"},
    {Pm4::IT_SET_CONFIG_REG,           "IT_SET_CONFIG_REG"},
    {Pm4::IT_SET_CONTEXT_REG,         "IT_SET_CONTEXT_REG"},
    {Pm4::IT_SET_SH_REG,              "IT_SET_SH_REG"},
    {Pm4::IT_SET_QUEUE_REG,            "IT_SET_QUEUE_REG"},
    {Pm4::IT_SET_UCONFIG_REG,         "IT_SET_UCONFIG_REG"},
    {Pm4::IT_SET_UCONFIG_REG_INDEX,   "IT_SET_UCONFIG_REG_INDEX"},
    {Pm4::IT_WRITE_CONST_RAM,         "IT_WRITE_CONST_RAM"},
    {Pm4::IT_DUMP_CONST_RAM,          "IT_DUMP_CONST_RAM"},
    {Pm4::IT_INCREMENT_CE_COUNTER,    "IT_INCREMENT_CE_COUNTER"},
    {Pm4::IT_INCREMENT_DE_COUNTER,    "IT_INCREMENT_DE_COUNTER"},
    {Pm4::IT_WAIT_ON_CE_COUNTER,      "IT_WAIT_ON_CE_COUNTER"},
    {Pm4::IT_WAIT_ON_DE_COUNTER_DIFF, "IT_WAIT_ON_DE_COUNTER_DIFF"},
    {Pm4::IT_DISPATCH_DRAW,           "IT_DISPATCH_DRAW"},
    {Pm4::IT_GET_LOD_STATS,           "IT_GET_LOD_STATS"},
    {Pm4::IT_WAIT_REG_MEM_64,         "IT_WAIT_REG_MEM_64"},
    {Pm4::IT_SET_CONTEXT_REG_INDIRECT,"IT_SET_CONTEXT_REG_INDIRECT"},
}};

[[nodiscard]] const char* OpcodeName(uint32_t opcode) noexcept {
        for (const auto& entry : kOpcodes) {
                if (entry.opcode == opcode) {
                        return entry.name;
                }
        }
        return "IT_UNKNOWN";
}

} // namespace

bool Initialize(const std::filesystem::path& path) {
        std::scoped_lock lock(g_dump_mutex);
        if (g_enabled) {
                return true; // already initialized
        }

        // Create parent directories if needed
        Common::File::CreateDirectories(path.parent_path());

        g_dump_file.open(path, std::ios::out | std::ios::trunc);
        if (!g_dump_file.is_open()) {
                return false;
        }

        g_dump_file << "# Kyty-039 PM4 command stream dump\n";
        g_dump_file << "# Format: offset_dw | opcode_name | r_slot | len_dw | payload_dwords_hex\n";
        g_dump_file << "# r_slot: register space (0=CONFIG, 1=CONTEXT, 2=SH, 3=UCONFIG, 4=QUEUE)\n";
        g_dump_file << "# len_dw: number of payload dwords (excluding header)\n";
        g_dump_file << "# payload: hex dwords, space-separated\n";
        g_dump_file << "---\n";
        g_dump_file.flush();

        g_enabled = true;
        return true;
}

void DumpPacket(uint32_t offset_dw, uint32_t packet_header,
                std::span<const uint32_t> payload) {
        if (!g_enabled) {
                return;
        }

        const auto opcode   = (packet_header >> 8u) & 0xffu;
        const auto r_slot   = KYTY_PM4_R(packet_header);
        const auto len_dw   = KYTY_PM4_LEN(packet_header);
        const auto* name    = OpcodeName(opcode);

        std::scoped_lock lock(g_dump_mutex);
        if (!g_dump_file.is_open()) {
                return;
        }

        // Format: offset=NNNNNN | NAME | r=NN | len=NN | dw0 dw1 dw2 ...
        g_dump_file << "offset=" << offset_dw
                    << " | " << name
                    << " | r=" << r_slot
                    << " | len=" << len_dw
                    << " |";
        for (const auto& dw : payload) {
                g_dump_file << ' ' << std::hex << dw << std::dec;
        }
        g_dump_file << '\n';
        g_dump_file.flush();
}

void Shutdown() {
        std::scoped_lock lock(g_dump_mutex);
        if (g_dump_file.is_open()) {
                g_dump_file << "---\n";
                g_dump_file << "# End of PM4 dump\n";
                g_dump_file.close();
        }
        g_enabled = false;
}

bool IsEnabled() noexcept {
        return g_enabled;
}

} // namespace Libs::Graphics::Pm4Dump
