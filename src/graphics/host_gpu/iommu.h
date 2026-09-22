// SPDX-FileCopyrightText: Copyright 2026 AbdoPS5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Kyty-016: AMD IOMMU model.
//
// PS5 has an AMD IOMMU at MMIO base 0xFDD80000. The IOMMU sits between
// the GPU and host RAM — when the GPU does a DMA read/write, the IOMMU
// translates the GPU's virtual address (called "device address" or
// "IOVA") to a host physical address.
//
// Without modeling the IOMMU:
//   - Games that use GPU DMA to non-system-memory addresses would crash
//     (the GPU tries to read address 0x12345678 which doesn't exist in
//     the host address space).
//   - The PS5 kernel's IOMMU driver code (which programs the IOMMU at
//     boot to set up translation tables) would write to 0xFDD80000+ and
//     nothing would happen.
//
// With the IOMMU modeled:
//   - MMIO writes to 0xFDD80000+ are intercepted and update the
//     IOMMU's internal state (control register, command queue).
//   - When the GPU does a DMA, we consult the IOMMU's translation
//     tables to find the host physical address.
//   - The completion-wait-store command (used by the loader as a
//     privileged-write primitive) actually performs the store.
//
// Reference: PS5_hardware_reference.md §4 (AMD IOMMU)
//
// Status (this commit):
//   - MMIO register layout (control + command buffer head/tail)
//   - Command queue processing (8 KB, 16-byte entries)
//   - Completion-wait-store command (opcode 0x05)
//   - translate() function for GPU DMA (currently pass-through —
//     games that don't actually use IOMMU translation will work
//     without us modeling the device table + page tables)
//
// Still TODO (future commits):
//   - Device table (DTE) parsing
//   - Page table walk (4-level, like AMD'spec)
//   - Wiring into the GPU command processor's DMA path
//   - Wiring into a future MMIO dispatch layer

#ifndef KYTY_GRAPHICS_HOST_GPU_IOMMU_H_
#define KYTY_GRAPHICS_HOST_GPU_IOMMU_H_

#include "common/common.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

namespace Libs::Graphics {

// AMD IOMMU MMIO register offsets (from base 0xFDD80000).
// Source: PS5_hardware_reference.md §4.1
namespace IommuMmio {
    inline constexpr uint64_t BASE          = 0xFDD80000ULL;
    inline constexpr uint64_t CTRL         = 0x18;    // Control register (8 B). Bit 0 = IOMMU enable.
    inline constexpr uint64_t CB_HEAD      = 0xA000;  // Command buffer head pointer (8 B, read-only from driver)
    inline constexpr uint64_t CB_TAIL      = 0xA008;  // Command buffer tail pointer (8 B, writable)
    inline constexpr uint64_t MMIO_RANGE   = 0x10000;  // Total MMIO range to reserve
} // namespace IommuMmio

// Command queue constants.
// Source: PS5_hardware_reference.md §4.2
namespace IommuCmd {
    inline constexpr uint32_t CB_SIZE        = 0x2000;     // 8 KB
    inline constexpr uint32_t CB_MASK        = 0x1FFF;     // size - 1
    inline constexpr uint32_t ENTRY_SIZE     = 0x10;       // 16 bytes = 4 dwords

    // Opcodes (bits 0-2 of dword0).
    // Source: AMD IOMMU spec + PS5 hardware reference §4.4
    enum class Opcode : uint8_t {
        COMPLETION_WAIT       = 0x01,  // Wait for completion (no store)
        COMPLETION_WAIT_STORE  = 0x05,  // Wait for completion + store 8 bytes
        INVALIDATE_DEVTAB     = 0x02,
        INVALIDATE_IOMMU_PAGES = 0x03,
        INVALIDATE_IOTLB       = 0x04,
        PREFETCH_PAGES         = 0x06,
        COMPLETE_PPR_REQUEST   = 0x07,
        INVALIDATE_INTERRUPT_TABLE = 0x08,
    };
} // namespace IommuCmd

// Decoded completion-wait-store command.
// Source: PS5_hardware_reference.md §4.4
struct IommuCompletionWaitStore {
    uint64_t target_pa;   // Physical address to write to (must be 8-byte aligned)
    uint64_t store_value;  // 8-byte value to store at target_pa
    bool     is_store;     // true = ST0=1 (perform store); false = just wait

    // Decode from a 16-byte command buffer entry (4 dwords).
    static IommuCompletionWaitStore Decode(const uint32_t cmd[4]) {
        IommuCompletionWaitStore result{};
        // cmd[0] bits 0-2: opcode
        // cmd[0] bits 3-31: bits 3-31 of target PA (so PA must be 8-byte aligned)
        // cmd[1] bits 0-19: bits 32-51 of target PA
        // cmd[1] bit 28: ST0 (store flag)
        // cmd[2]: low 32 bits of value
        // cmd[3]: high 32 bits of value
        const uint64_t pa_lo = static_cast<uint64_t>(cmd[0] & 0xFFFFFFF8);
        const uint64_t pa_hi = static_cast<uint64_t>(cmd[1] & 0x000FFFFF) << 32;
        result.target_pa   = pa_lo | pa_hi;
        result.is_store    = (cmd[1] & 0x10000000) != 0;
        result.store_value = static_cast<uint64_t>(cmd[2]) | (static_cast<uint64_t>(cmd[3]) << 32);
        return result;
    }
};

class Iommu {
public:
    // Callback type for performing a physical-address store.
    // The kernel registers this so the IOMMU can write to physical RAM
    // (used by the completion-wait-store command, which is the PS5
    // kernel's privileged-write primitive).
    using StoreCallback = bool (*)(uint64_t pa, uint64_t value, void* user_data);

    Iommu();
    ~Iommu();

    // Register the store callback (called by the kernel during init).
    // Without a registered callback, COMPLETION_WAIT_STORE commands are
    // logged but the store is not performed — safe for games that don't
    // use the HV-escape IOMMU primitive.
    void RegisterStoreCallback(StoreCallback cb, void* user_data) {
        m_store_callback = cb;
        m_store_callback_user_data = user_data;
    }

    // MMIO read/write — called when the guest CPU accesses the IOMMU's
    // MMIO range (0xFDD80000 .. 0xFDD90000).
    //
    // Returns true if the access was handled (the address is in the
    // IOMMU's MMIO range). Returns false if the address is outside the
    // range — caller should try the next MMIO handler or fault.
    [[nodiscard]] bool MmioRead(uint64_t pa, void* dst, size_t size) const;
    [[nodiscard]] bool MmioWrite(uint64_t pa, const void* src, size_t size);

    // Translate a GPU device address (IOVA) to a host physical address.
    //
    // Currently a pass-through — returns the IOVA unchanged. This is
    // correct for the PS5 boot path (the loader disables the IOMMU by
    // clearing bit 0 of the control register at AMDIOMMU_CTRL).
    //
    // When the IOMMU is enabled (bit 0 set), this function will walk
    // the device table + page tables to translate. That's a future
    // commit — for now, we just need to NOT break games that don't
    // use IOMMU translation.
    //
    // Returns the host physical address (== IOVA when disabled).
    [[nodiscard]] uint64_t Translate(uint64_t iova) const;

    // Is the IOMMU enabled? (bit 0 of AMDIOMMU_CTRL)
    [[nodiscard]] bool IsEnabled() const { return (m_ctrl.load() & 1) != 0; }

    // For tests / inspection.
    [[nodiscard]] uint64_t GetControlRegister() const { return m_ctrl.load(); }
    [[nodiscard]] uint64_t GetCommandBufferHead() const { return m_cb_head.load(); }
    [[nodiscard]] uint64_t GetCommandBufferTail() const { return m_cb_tail.load(); }

    // Reset to power-on state. Useful for tests.
    void Reset();

private:
    // Process all commands in the command queue (head advances to tail).
    // Called when the guest writes to CB_TAIL.
    void ProcessCommands();

    // Execute a single 16-byte command. Returns true if the command was
    // recognized (false = unknown opcode, logged + skipped).
    bool ExecuteCommand(const uint32_t cmd[4]);

    // Execute the completion-wait-store command (opcode 0x05).
    // Writes 8 bytes to the target physical address.
    void ExecuteCompletionWaitStore(const IommuCompletionWaitStore& cmd);

    // Control register (8 B, bit 0 = enable).
    std::atomic<uint64_t> m_ctrl{0};

    // Command buffer head/tail pointers (8 B each).
    std::atomic<uint64_t> m_cb_head{0};
    std::atomic<uint64_t> m_cb_tail{0};

    // The command buffer itself (8 KB).
    // Stored as bytes; commands are 16-byte aligned.
    std::vector<uint8_t> m_command_buffer;

    // Mutex protects m_command_buffer during concurrent read/write.
    // m_cb_head and m_cb_tail are atomic because they're updated by
    // the guest CPU thread (writes to CB_TAIL) and read by the IOMMU's
    // command processing thread (advances CB_HEAD).
    mutable std::mutex m_buffer_mutex;

    // Store callback for the COMPLETION_WAIT_STORE command.
    // Set by the kernel during init via RegisterStoreCallback().
    StoreCallback m_store_callback = nullptr;
    void*         m_store_callback_user_data = nullptr;
};

} // namespace Libs::Graphics

#endif // KYTY_GRAPHICS_HOST_GPU_IOMMU_H_
