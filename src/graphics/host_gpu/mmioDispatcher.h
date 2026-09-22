// SPDX-FileCopyrightText: Copyright 2026 AbdoPS5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Kyty-016 (continued): MMIO dispatcher.
//
// Provides a registry of MMIO handlers keyed by physical address range.
// The kernel's MMIO access functions (e.g. an in-kernel 'ioremap' /
// 'readq' / 'writeq' implementation) consult this dispatcher to find
// the right handler for a given physical address.
//
// Why a separate dispatcher (not direct calls to Iommu::MmioRead/Write)?
//   - PS5 has multiple MMIO regions: IOMMU at 0xFDD80000, TMR controller
//     elsewhere, GPU registers, etc. A central dispatcher lets each
//     device register itself without the caller needing to know which
//     device lives at which address.
//   - Future devices (Kyty-017 TMR controller, GPU MMIO, etc.) can
//     register without modifying the call sites.
//
// This file is intentionally minimal — just the registry. Each handler
// is responsible for its own MMIO range (see Iommu::MmioRead/Write for
// an example).

#ifndef KYTY_GRAPHICS_HOST_GPU_MMIO_DISPATCHER_H_
#define KYTY_GRAPHICS_HOST_GPU_MMIO_DISPATCHER_H_

#include "common/common.h"

#include <cstdint>
#include <mutex>
#include <vector>

namespace Libs::Graphics {

// Abstract base for an MMIO handler.
// Implementations override MmioRead/MmioWrite — they should return
// true if the access was handled (the address is in their range),
// false otherwise.
class MmioHandler {
public:
    virtual ~MmioHandler() = default;

    // Handle a read of `size` bytes at physical address `pa`.
    // Returns true if the address is in this handler's range.
    // On success, the result is written to `dst`.
    [[nodiscard]] virtual bool MmioRead(uint64_t pa, void* dst, size_t size) const = 0;

    // Handle a write of `size` bytes at physical address `pa`.
    // Returns true if the address is in this handler's range.
    [[nodiscard]] virtual bool MmioWrite(uint64_t pa, const void* src, size_t size) = 0;
};

// Central MMIO dispatcher. Singleton — call Instance() to access.
// The kernel calls DispatchRead/DispatchWrite when it needs to access
// an MMIO address. The dispatcher finds the first registered handler
// whose range covers `pa` and delegates to it.
class MmioDispatcher {
public:
    static MmioDispatcher& Instance() {
        static MmioDispatcher instance;
        return instance;
    }

    // Register a handler for the address range [base, base+size).
    // Multiple handlers can be registered for different ranges.
    // If ranges overlap, the first-registered handler wins (matches
    // hardware probe order on real systems).
    //
    // The handler is stored as a non-owning pointer — the caller
    // (e.g. the kernel) is responsible for keeping it alive.
    void Register(uint64_t base, uint64_t size, MmioHandler* handler) {
        std::lock_guard lock(m_mutex);
        m_handlers.push_back({base, size, handler});
    }

    // Dispatch a read. Returns true if a handler accepted the access.
    // If no handler covers `pa`, returns false (caller should fault
    // or treat as unmapped).
    [[nodiscard]] bool DispatchRead(uint64_t pa, void* dst, size_t size) const {
        std::lock_guard lock(m_mutex);
        for (const auto& entry : m_handlers) {
            if (pa >= entry.base && pa + size <= entry.base + entry.size) {
                return entry.handler->MmioRead(pa, dst, size);
            }
        }
        return false;
    }

    // Dispatch a write. Returns true if a handler accepted the access.
    [[nodiscard]] bool DispatchWrite(uint64_t pa, const void* src, size_t size) {
        std::lock_guard lock(m_mutex);
        for (auto& entry : m_handlers) {
            if (pa >= entry.base && pa + size <= entry.base + entry.size) {
                return entry.handler->MmioWrite(pa, src, size);
            }
        }
        return false;
    }

    // Clear all registered handlers. Useful for tests / shutdown.
    void Clear() {
        std::lock_guard lock(m_mutex);
        m_handlers.clear();
    }

private:
    struct HandlerEntry {
        uint64_t      base;
        uint64_t      size;
        MmioHandler* handler;
    };

    mutable std::mutex          m_mutex;
    std::vector<HandlerEntry>   m_handlers;
};

} // namespace Libs::Graphics

#endif // KYTY_GRAPHICS_HOST_GPU_MMIO_DISPATCHER_H_
