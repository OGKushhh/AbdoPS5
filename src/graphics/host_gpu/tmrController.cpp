// SPDX-FileCopyrightText: Copyright 2026 AbdoPS5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Kyty-017: TMR (Trust Memory Range) controller — implementation.

#include "graphics/host_gpu/tmrController.h"

#include "common/assert.h"
#include "common/logging/log.h"

#include <memory>

namespace Libs::Graphics {

TmrController::TmrController() {
    Reset();
}

void TmrController::Reset() {
    m_index = 0;
    // Default: all entries permissive (matching what the boot loader
    // does after HV defeat). This means the emulator never blocks
    // any memory access due to TMR — which is correct for an emulator
    // (the host OS already enforces memory protection).
    SetAllPermissive();
}

void TmrController::SetAllPermissive() {
    for (uint32_t i = 0; i < TmrEntry::MAX_ENTRIES; ++i) {
        m_entries[i].config = TmrEntry::CFG_PERMISSIVE;
        m_entries[i].requestors = 0xFFFFFFFF; // all requestors allowed
        m_entries[i].base = 0;
        m_entries[i].limit = 0xFFFF;
    }
}

void TmrController::DisableAll() {
    for (uint32_t i = 0; i < TmrEntry::MAX_ENTRIES; ++i) {
        m_entries[i].config = 0;
        m_entries[i].requestors = 0;
        m_entries[i].base = 0;
        m_entries[i].limit = 0;
    }
}

uint32_t TmrController::ReadEntryField(uint32_t index, uint32_t field_offset) const {
    // The index register encodes: entry_number * 0x10 + field_offset
    // We extract the entry number and field from the raw index.
    const uint32_t entry_num = index / TmrEntry::ENTRY_SIZE;
    const uint32_t field = index % TmrEntry::ENTRY_SIZE;

    if (entry_num >= TmrEntry::MAX_ENTRIES) {
        LOGF("TMR: ReadEntryField: entry %u out of range (max %u)\n",
             entry_num, TmrEntry::MAX_ENTRIES);
        return 0;
    }

    switch (field) {
        case TmrEntry::FIELD_BASE:       return m_entries[entry_num].base;
        case TmrEntry::FIELD_LIMIT:      return m_entries[entry_num].limit;
        case TmrEntry::FIELD_CONFIG:     return m_entries[entry_num].config;
        case TmrEntry::FIELD_REQUESTORS: return m_entries[entry_num].requestors;
        default:
            LOGF("TMR: ReadEntryField: unknown field offset 0x%02x (entry %u)\n",
                 field, entry_num);
            return 0;
    }
}

void TmrController::WriteEntryField(uint32_t index, uint32_t field_offset, uint32_t value) {
    const uint32_t entry_num = index / TmrEntry::ENTRY_SIZE;
    const uint32_t field = index % TmrEntry::ENTRY_SIZE;

    if (entry_num >= TmrEntry::MAX_ENTRIES) {
        LOGF("TMR: WriteEntryField: entry %u out of range (max %u)\n",
             entry_num, TmrEntry::MAX_ENTRIES);
        return;
    }

    switch (field) {
        case TmrEntry::FIELD_BASE:
            m_entries[entry_num].base = value;
            break;
        case TmrEntry::FIELD_LIMIT:
            m_entries[entry_num].limit = value;
            break;
        case TmrEntry::FIELD_CONFIG:
            m_entries[entry_num].config = value;
            LOGF("TMR: entry %u config <- 0x%04x %s\n", entry_num, value,
                 value == TmrEntry::CFG_PERMISSIVE ? "(permissive)" :
                 value == 0 ? "(disabled)" : "(custom)");
            break;
        case TmrEntry::FIELD_REQUESTORS:
            m_entries[entry_num].requestors = value;
            break;
        default:
            LOGF("TMR: WriteEntryField: unknown field offset 0x%02x (entry %u)\n",
                 field, entry_num);
            break;
    }
}

bool TmrController::MmioRead(uint64_t pa, void* dst, size_t size) const {
    if (pa < TmrMmio::ECAM_BASE || pa + size > TmrMmio::ECAM_BASE + TmrMmio::MMIO_RANGE) {
        return false;
    }

    const uint64_t offset = pa - TmrMmio::ECAM_BASE;

    if (size != sizeof(uint32_t)) {
        // TMR registers are 32-bit. Other sizes are not supported.
        LOGF("TMR: unhandled MMIO read size %zu at offset 0x%llx\n",
             size, static_cast<unsigned long long>(offset));
        std::memset(dst, 0, size);
        return true;
    }

    uint32_t value = 0;
    switch (offset) {
        case TmrMmio::INDEX_OFF:
            // Reading the index register returns the current index.
            value = m_index;
            break;
        case TmrMmio::DATA_OFF:
            // Reading the data register returns the value at the current index.
            value = ReadEntryField(m_index, m_index % TmrEntry::ENTRY_SIZE);
            break;
        default:
            // Other ECAM offsets (PCI config space headers, etc.) return 0.
            // The TMR controller only uses offsets 0x80 and 0x84.
            value = 0;
            break;
    }

    *static_cast<uint32_t*>(dst) = value;
    return true;
}

bool TmrController::MmioWrite(uint64_t pa, const void* src, size_t size) {
    if (pa < TmrMmio::ECAM_BASE || pa + size > TmrMmio::ECAM_BASE + TmrMmio::MMIO_RANGE) {
        return false;
    }

    const uint64_t offset = pa - TmrMmio::ECAM_BASE;

    if (size != sizeof(uint32_t)) {
        LOGF("TMR: unhandled MMIO write size %zu at offset 0x%llx\n",
             size, static_cast<unsigned long long>(offset));
        return true;
    }

    const uint32_t value = *static_cast<const uint32_t*>(src);

    switch (offset) {
        case TmrMmio::INDEX_OFF:
            // Write the index — selects which entry/field to access.
            m_index = value;
            break;
        case TmrMmio::DATA_OFF:
            // Write data to the currently-selected entry field.
            WriteEntryField(m_index, m_index % TmrEntry::ENTRY_SIZE, value);
            break;
        default:
            // Other ECAM writes are ignored (PCI config space, etc.)
            break;
    }
    return true;
}

// Global singleton accessor.
namespace {
std::unique_ptr<TmrController> g_tmr;
}

TmrController* GetTmr() {
    return g_tmr.get();
}

void InitializeTmr() {
    if (g_tmr != nullptr) {
        return; // already initialized
    }
    g_tmr = std::make_unique<TmrController>();
    g_tmr->RegisterWithDispatcher();
    LOGF("TMR: initialized and registered with MMIO dispatcher at 0x%llx "
         "(all entries permissive)\n",
         static_cast<unsigned long long>(TmrMmio::ECAM_BASE));
}

} // namespace Libs::Graphics
