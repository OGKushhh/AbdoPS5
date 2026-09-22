// SPDX-FileCopyrightText: Copyright 2026 AbdoPS5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Kyty-016: AMD IOMMU model — implementation.

#include "graphics/host_gpu/iommu.h"

#include "common/assert.h"
#include "common/logging/log.h"

#include <memory>

namespace Libs::Graphics {

Iommu::Iommu() {
    m_command_buffer.resize(IommuCmd::CB_SIZE, 0);
    Reset();
}

Iommu::~Iommu() = default;

void Iommu::Reset() {
    std::lock_guard lock(m_buffer_mutex);
    m_ctrl.store(0);
    m_cb_head.store(0);
    m_cb_tail.store(0);
    std::fill(m_command_buffer.begin(), m_command_buffer.end(), 0);
}

bool Iommu::MmioRead(uint64_t pa, void* dst, size_t size) const {
    // Check if the address falls within our MMIO range.
    if (pa < IommuMmio::BASE || pa + size > IommuMmio::BASE + IommuMmio::MMIO_RANGE) {
        return false;
    }

    const uint64_t offset = pa - IommuMmio::BASE;

    // We only support 8-byte (uint64_t) reads for the registers we track.
    // Unknown registers return 0.
    if (size != sizeof(uint64_t)) {
        LOGF("IOMMU: unhandled MMIO read size %zu at offset 0x%llx\n", size,
             static_cast<unsigned long long>(offset));
        std::memset(dst, 0, size);
        return true;
    }

    uint64_t value = 0;
    switch (offset) {
        case IommuMmio::CTRL:
            value = m_ctrl.load();
            break;
        case IommuMmio::CB_HEAD:
            value = m_cb_head.load();
            break;
        case IommuMmio::CB_TAIL:
            value = m_cb_tail.load();
            break;
        default:
            // Unknown register — return 0. Real hardware would return
            // the actual register value or 0xFFFFFFFF if unimplemented.
            // For the emulator, returning 0 is safe — the guest's IOMMU
            // driver only reads registers it knows about.
            LOGF("IOMMU: read from unknown MMIO offset 0x%llx (returning 0)\n",
                 static_cast<unsigned long long>(offset));
            value = 0;
            break;
    }

    *static_cast<uint64_t*>(dst) = value;
    return true;
}

bool Iommu::MmioWrite(uint64_t pa, const void* src, size_t size) {
    if (pa < IommuMmio::BASE || pa + size > IommuMmio::BASE + IommuMmio::MMIO_RANGE) {
        return false;
    }

    const uint64_t offset = pa - IommuMmio::BASE;

    if (size != sizeof(uint64_t)) {
        LOGF("IOMMU: unhandled MMIO write size %zu at offset 0x%llx\n", size,
             static_cast<unsigned long long>(offset));
        return true;
    }

    const uint64_t value = *static_cast<const uint64_t*>(src);

    switch (offset) {
        case IommuMmio::CTRL: {
            const uint64_t old_ctrl = m_ctrl.load();
            m_ctrl.store(value);
            const bool was_enabled = (old_ctrl & 1) != 0;
            const bool now_enabled = (value & 1) != 0;
            if (was_enabled && !now_enabled) {
                LOGF("IOMMU: disabled (control register bit 0 cleared)\n");
            } else if (!was_enabled && now_enabled) {
                LOGF("IOMMU: enabled (control register bit 0 set)\n");
            }
            break;
        }
        case IommuMmio::CB_HEAD:
            // Head is read-only from the driver side — the IOMMU advances
            // it after processing commands. Guest writes are ignored.
            LOGF("IOMMU: write to read-only CB_HEAD register (ignored)\n");
            break;
        case IommuMmio::CB_TAIL:
            // Tail advances trigger command processing.
            m_cb_tail.store(value & IommuCmd::CB_MASK);
            LOGF("IOMMU: CB_TAIL <- 0x%llx (head=0x%llx, processing commands)\n",
                 static_cast<unsigned long long>(value & IommuCmd::CB_MASK),
                 static_cast<unsigned long long>(m_cb_head.load()));
            ProcessCommands();
            break;
        default:
            LOGF("IOMMU: write to unknown MMIO offset 0x%llx (value=0x%llx, ignored)\n",
                 static_cast<unsigned long long>(offset),
                 static_cast<unsigned long long>(value));
            break;
    }
    return true;
}

void Iommu::ProcessCommands() {
    // Process commands until head == tail.
    // Each command is 16 bytes; head/tail are byte offsets into the
    // 8 KB ring buffer.
    uint64_t local_head = m_cb_head.load();
    uint64_t local_tail = m_cb_tail.load();

    while (local_head != local_tail) {
        // Read a 16-byte command from the ring buffer.
        uint32_t cmd[4] = {};
        {
            std::lock_guard lock(m_buffer_mutex);
            const uint8_t* entry = m_command_buffer.data() + local_head;
            std::memcpy(cmd, entry, IommuCmd::ENTRY_SIZE);
        }

        // Execute the command.
        if (!ExecuteCommand(cmd)) {
            LOGF("IOMMU: unknown opcode 0x%02x at offset 0x%llx (skipping)\n",
                 cmd[0] & 0x7, static_cast<unsigned long long>(local_head));
        }

        // Advance head.
        local_head = (local_head + IommuCmd::ENTRY_SIZE) & IommuCmd::CB_MASK;
        m_cb_head.store(local_head);
    }
}

bool Iommu::ExecuteCommand(const uint32_t cmd[4]) {
    const auto opcode = static_cast<IommuCmd::Opcode>(cmd[0] & 0x7);

    switch (opcode) {
        case IommuCmd::Opcode::COMPLETION_WAIT:
        case IommuCmd::Opcode::COMPLETION_WAIT_STORE: {
            const auto cws = IommuCompletionWaitStore::Decode(cmd);
            ExecuteCompletionWaitStore(cws);
            return true;
        }
        case IommuCmd::Opcode::INVALIDATE_DEVTAB:
            LOGF("IOMMU: INVALIDATE_DEVTAB command (stubbed)\n");
            return true;
        case IommuCmd::Opcode::INVALIDATE_IOMMU_PAGES:
            LOGF("IOMMU: INVALIDATE_IOMMU_PAGES command (stubbed)\n");
            return true;
        case IommuCmd::Opcode::INVALIDATE_IOTLB:
            LOGF("IOMMU: INVALIDATE_IOTLB command (stubbed)\n");
            return true;
        case IommuCmd::Opcode::PREFETCH_PAGES:
            LOGF("IOMMU: PREFETCH_PAGES command (stubbed)\n");
            return true;
        case IommuCmd::Opcode::COMPLETE_PPR_REQUEST:
            LOGF("IOMMU: COMPLETE_PPR_REQUEST command (stubbed)\n");
            return true;
        case IommuCmd::Opcode::INVALIDATE_INTERRUPT_TABLE:
            LOGF("IOMMU: INVALIDATE_INTERRUPT_TABLE command (stubbed)\n");
            return true;
        default:
            return false;
    }
}

void Iommu::ExecuteCompletionWaitStore(const IommuCompletionWaitStore& cmd) {
    if (!cmd.is_store) {
        // Pure wait — just return. The caller is already blocked on
        // the head==tail check in iommu_submit_cmd(), so there's nothing
        // to do here.
        LOGF("IOMMU: COMPLETION_WAIT (no store)\n");
        return;
    }

    // Perform the store: write 8 bytes to the target physical address.
    // The PS5 kernel uses this as a privileged-write primitive — e.g.,
    // to clear NESTED_CTRL bits in VMCBs after HV escape.
    LOGF("IOMMU: COMPLETION_WAIT_STORE pa=0x%llx val=0x%llx\n",
         static_cast<unsigned long long>(cmd.target_pa),
         static_cast<unsigned long long>(cmd.store_value));

    if (m_store_callback != nullptr) {
        if (!m_store_callback(cmd.target_pa, cmd.store_value, m_store_callback_user_data)) {
            LOGF("IOMMU: store callback returned false (PA 0x%llx outside RAM?)\n",
                 static_cast<unsigned long long>(cmd.target_pa));
        }
    } else {
        // No store callback registered — the kernel hasn't wired this up yet.
        // The store is logged but not performed. Safe for retail games
        // (they don't use the HV-escape IOMMU primitive).
        LOGF("IOMMU: no store callback registered — store skipped "
             "(retail games don't need this)\n");
    }
}

uint64_t Iommu::Translate(uint64_t iova) const {
    // Currently pass-through. When the IOMMU is enabled (bit 0 of CTRL
    // set), this would walk the device table + page tables. But the PS5
    // boot loader disables the IOMMU early in boot, so most games run
    // with translation effectively off.
    //
    // When we start modeling games that DO use IOMMU translation (e.g.
    // some PS5 exclusives that DMA to non-system-memory addresses),
    // we'll implement the actual page walk here.
    if (IsEnabled()) {
        LOGF("IOMMU: Translate called while IOMMU is enabled — returning IOVA "
             "unchanged (page walk not implemented yet)\n");
    }
    return iova;
}

// Global singleton accessor.
namespace {
std::unique_ptr<Iommu> g_iommu;
}

Iommu* GetIommu() {
    return g_iommu.get();
}

// Initialize the IOMMU singleton and register it with the MMIO dispatcher.
// Called from Emulator::Run() during graphics subsystem startup.
// Declared here (not in iommu.h) to keep the header clean — this is
// an internal lifecycle function, not a public API.
//
// Note: defined in this file so it has access to the Iommu constructor.
void InitializeIommu() {
    if (g_iommu != nullptr) {
        return;  // already initialized
    }
    g_iommu = std::make_unique<Iommu>();
    g_iommu->RegisterWithDispatcher();
    LOGF("IOMMU: initialized and registered with MMIO dispatcher at 0x%llx\n",
         static_cast<unsigned long long>(IommuMmio::BASE));
}

} // namespace Libs::Graphics
