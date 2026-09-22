// SPDX-FileCopyrightText: Copyright 2026 AbdoPS5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Kyty-017: TMR (Trust Memory Range) controller model.
//
// PS5 has a Sony-custom TMR controller (PCI B0:D18:F2) that protects
// kernel/HV/firmware memory regions from unauthorized access. It sits
// at ECAM address 0xF0C2000 and uses an indexed register pair.
//
// Without modeling the TMR:
//   - Games that touch Sony-protected memory would crash (the TMR
//     blocks the access on real hardware — but in the emulator, there's
//     no TMR to block, so the access just goes through, which is fine
//     for most cases).
//   - The PS5 kernel's TMR driver code (which reads/writes TMR entries
//     during boot) would write to 0xF0C2000+ and nothing would happen.
//
// With the TMR modeled:
//   - MMIO writes to 0xF0C2000+ are intercepted and update the TMR's
//     internal state (indexed register pair + entry table).
//   - All entries default to permissive (0x3F07 = allow all access).
//     This matches what the PS5 boot loader does after HV defeat.
//   - Games that read TMR entries get correct responses.
//   - No game crashes due to TMR violations (because everything is
//     permissive).
//
// Reference: PS5_hardware_reference.md §5 (TMR Controller)

#ifndef KYTY_GRAPHICS_HOST_GPU_TMR_CONTROLLER_H_
#define KYTY_GRAPHICS_HOST_GPU_TMR_CONTROLLER_H_

#include "common/common.h"
#include "graphics/host_gpu/mmioDispatcher.h"

#include <cstdint>
#include <cstring>

namespace Libs::Graphics {

// TMR MMIO constants.
// Source: PS5_hardware_reference.md §5.1
namespace TmrMmio {
    // ECAM base: 0xF0000000 + 0x12 * 0x8000 + 0x2 * 0x1000 = 0xF0C2000
    inline constexpr uint64_t ECAM_BASE   = 0xF0C2000ULL;
    inline constexpr uint64_t MMIO_RANGE  = 0x1000;      // 4 KB (standard PCI function)

    // Indexed register pair offsets (within ECAM space).
    // Source: PS5_hardware_reference.md §5.2
    inline constexpr uint64_t INDEX_OFF   = 0x80;        // Write entry index here
    inline constexpr uint64_t DATA_OFF    = 0x84;        // Read/write entry value here
} // namespace TmrMmio

// TMR entry constants.
// Source: PS5_hardware_reference.md §5.3, §5.4
namespace TmrEntry {
    inline constexpr uint32_t MAX_ENTRIES    = 24;      // Some FWs use 24
    inline constexpr uint32_t ENTRY_SIZE     = 0x10;    // 16 bytes = 4 × 32-bit
    inline constexpr uint32_t FIELD_BASE     = 0x00;    // Low 16 bits of region base PA
    inline constexpr uint32_t FIELD_LIMIT    = 0x04;    // Low 16 bits of region limit PA
    inline constexpr uint32_t FIELD_CONFIG   = 0x08;    // Configuration / permission flags
    inline constexpr uint32_t FIELD_REQUESTORS = 0x0C;  // Bitmap of allowed requestor IDs

    // Permissive configuration: opens the range to all requestors.
    // Binary: 0011_1111_0000_0111
    // The loader writes this to relax TMR enforcement.
    inline constexpr uint32_t CFG_PERMISSIVE = 0x3F07;

    // Entries relaxed by the PS5 boot loader (FW ≥ 3.00).
    // Source: PS5_hardware_reference.md §5.4
    inline constexpr uint32_t RELAXED_ENTRIES[] = {5, 16, 17, 18};
} // namespace TmrEntry

// TMR entry (16 bytes = 4 × 32-bit fields).
struct TmrEntryData {
    uint32_t base       = 0;  // Low 16 bits of region base PA. Full base = base << 16.
    uint32_t limit      = 0;  // Low 16 bits of region limit PA. Full limit = (limit << 16) | 0xFFFF.
    uint32_t config     = 0;  // Configuration / permission flags (0 = disabled, 0x3F07 = permissive)
    uint32_t requestors = 0;  // Bitmap of allowed requestor IDs
};
static_assert(sizeof(TmrEntryData) == 16, "TmrEntryData must be 16 bytes");

class TmrController : public MmioHandler {
public:
    TmrController();
    ~TmrController() override = default;

    // MmioHandler interface — handles accesses to 0xF0C2000..0xF0C3000.
    [[nodiscard]] bool MmioRead(uint64_t pa, void* dst, size_t size) const override;
    [[nodiscard]] bool MmioWrite(uint64_t pa, const void* src, size_t size) override;

    // Register this TMR with the central MmioDispatcher.
    void RegisterWithDispatcher() {
        MmioDispatcher::Instance().Register(TmrMmio::ECAM_BASE, TmrMmio::MMIO_RANGE, this);
    }

    // Read a TMR entry field by index.
    // index: the entry index (0-23)
    // field_offset: 0x00 (base), 0x04 (limit), 0x08 (config), 0x0C (requestors)
    [[nodiscard]] uint32_t ReadEntryField(uint32_t index, uint32_t field_offset) const;

    // Write a TMR entry field by index.
    void WriteEntryField(uint32_t index, uint32_t field_offset, uint32_t value);

    // Get the current index register value (written by the guest to offset 0x80).
    [[nodiscard]] uint32_t GetIndex() const { return m_index; }

    // Check if a TMR entry is active (config != 0).
    [[nodiscard]] bool IsEntryActive(uint32_t index) const {
        if (index >= TmrEntry::MAX_ENTRIES) return false;
        return m_entries[index].config != 0;
    }

    // Check if a TMR entry is permissive (config == 0x3F07).
    [[nodiscard]] bool IsEntryPermissive(uint32_t index) const {
        if (index >= TmrEntry::MAX_ENTRIES) return false;
        return m_entries[index].config == TmrEntry::CFG_PERMISSIVE;
    }

    // Reset to power-on state: all entries permissive (matching what
    // the boot loader does after HV defeat).
    void Reset();

    // Force all entries to permissive (used after HV defeat).
    void SetAllPermissive();

    // Disable all entries (config = 0). Used by tmr_disable() post-HV.
    void DisableAll();

private:
    // Indexed register pair: guest writes an index to 0x80, then
    // reads/writes the data at 0x84. The index encodes both the
    // entry number and the field offset:
    //   index = entry_number * 0x10 + field_offset
    //   e.g. TMR_CONFIG(5) = 5 * 0x10 + 0x08 = 0x58
    uint32_t m_index = 0;

    // TMR entry table (24 entries × 16 bytes).
    TmrEntryData m_entries[TmrEntry::MAX_ENTRIES] = {};
};

} // namespace Libs::Graphics

// Global accessors (same pattern as Iommu).
namespace Libs::Graphics {
void InitializeTmr();
[[nodiscard]] TmrController* GetTmr();
} // namespace Libs::Graphics

#endif // KYTY_GRAPHICS_HOST_GPU_TMR_CONTROLLER_H_
