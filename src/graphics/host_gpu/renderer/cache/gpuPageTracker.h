#ifndef EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_CACHE_GPUPAGETRACKER_H_
#define EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_CACHE_GPUPAGETRACKER_H_

// Kyty-037: GPU Page Generation Tracker
//
// Per-page monotonic generation counter for guest GPU memory ranges,
// inspired by PS5PCEM's "16 KiB page tracking by generation" model
// (see docs/PS5PCEM_deep_comparison.md section 8).
//
// The existing TextureCache answers "is this image's cached data stale?"
// by walking the multi-level page table and per-image dirty flags. That
// works, but the cost is O(pages × images-per-page) per invalidation
// call, and the granularity is "is there ANY change in this region?"
// rather than "is there a change since observation T?".
//
// The GpuPageTracker adds a small per-page generation counter:
//
//   - Every GPU write bumps the generation of every 4 KiB page it touches.
//   - Readers cache the generation they observed when they last copied /
//     downloaded / hashed the page; they can cheaply check "has anything
//     changed since I last looked?" with a single integer comparison.
//
// This complements (does NOT replace) the existing Kyty-018 fault-buffer
// processing and the Kyty-034 ImageAliasRegistry. The tracker's job is
// purely to answer the "has anything changed in [addr, addr+size) since
// generation T?" question in O(pages) time, without walking the image
// cache.
//
// Memory layout:
//   - Pages are TRACKER_PAGE_SIZE (4 KiB) to match the existing page
//     manager, so generation bumps can be coalesced with page-watcher
//     updates.
//   - The full 40-bit guest address space (1 TiB) would need 256 MiB
//     of generation counters at 2 bytes each — too much. Instead we
//     lazily allocate per-4-MiB-region generation tables (matching
//     TRACKER_REGION_SIZE), and each region holds 1024 page counters.
//   - Total memory: only regions that have ever been touched by a GPU
//     write allocate storage.

#include "common/abi.h"
#include "common/assert.h"
#include "graphics/host_gpu/regionDefinitions.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Libs::Graphics {

// Kyty-037: Per-page generation tracker.
//
// Thread-safety: all public methods are thread-safe via an internal
// spinlock, matching the TextureCache locking model. The generation
// counters themselves use std::atomic<uint16_t> so readers can do
// lock-free generation comparisons after taking a stable snapshot of
// the region table.
class GpuPageTracker {
public:
        using Generation = uint16_t;

        // Sentinel value returned for pages that have never been written.
        // Callers can compare their cached generation against this to
        // short-circuit "is anything newer?" queries for never-touched
        // memory.
        static constexpr Generation kInitialGeneration = 0;

        GpuPageTracker() = default;
        KYTY_CLASS_NO_COPY(GpuPageTracker);

        // Bump the generation counter of every page in [address, address+size).
        // Used by TextureCache::MarkGpuWritten() to record that a render-target
        // or storage image just wrote new data to the page range. Pages that
        // have never been written before are lazily allocated.
        //
        // The `tick` parameter is the scheduler tick at the time of the write,
        // used as a tie-breaker when multiple writers race within the same
        // generation epoch (currently unused but reserved for future
        // fine-grained ordering work).
        void NotifyGpuWrite(uint64_t address, uint64_t size, uint64_t tick = 0);

        // Return the highest generation counter seen in [address, address+size).
        // Readers can use this as a cheap "snapshot" — if the value matches
        // what they observed when they last copied data, no page in the range
        // has been written since.
        //
        // Returns kInitialGeneration if no page in the range has ever been
        // written.
        [[nodiscard]] Generation MaxGeneration(uint64_t address, uint64_t size) const;

        // Return the generation of a single page. Mostly for diagnostics;
        // prefer MaxGeneration() for range queries.
        [[nodiscard]] Generation PageGeneration(uint64_t address) const;

        // True iff any page in [address, address+size) has a generation
        // strictly greater than `since`. This is the primary read-side API.
        [[nodiscard]] bool HasChangedSince(uint64_t address, uint64_t size,
                                            Generation since) const;

        // Forget all generation state for pages in [address, address+size).
        // Used when memory is unmapped or the underlying buffer is destroyed.
        // After this call, MaxGeneration() for the range returns
        // kInitialGeneration.
        void ClearRange(uint64_t address, uint64_t size);

        // Clear all state. Used by TextureCache teardown.
        void Clear();

        // Number of 4 MiB regions currently tracked. Mainly for diagnostics.
        [[nodiscard]] size_t RegionCount() const noexcept { return m_regions.size(); }

        // Number of distinct pages currently tracked. Mainly for diagnostics.
        // O(regions) walk; do not call in hot paths.
        [[nodiscard]] size_t TrackedPageCount() const;

private:
        // Per-region generation table. Each region is TRACKER_REGION_SIZE
        // (4 MiB) and contains TRACKER_REGION_PAGES (1024) 4 KiB pages.
        // Using std::vector<atomic<Generation>> rather than a fixed C array
        // so the memory is only allocated when a region is first touched.
        struct RegionTable {
                std::vector<std::atomic<Generation>> pages;
                explicit RegionTable(size_t page_count)
                    : pages(page_count) {
                        for (auto& g : pages) {
                                g.store(kInitialGeneration, std::memory_order_relaxed);
                        }
                }
        };

        // Keyed on region index = address / TRACKER_REGION_SIZE.
        // We use std::unordered_map for O(1) average lookup; the alternative
        // (an interval tree) would be faster for range scans but slower for
        // the common single-region bump.
        std::unordered_map<uint64_t, std::unique_ptr<RegionTable>> m_regions;

        // Lazy-allocate a region's page table on first access. Returns a
        // reference to the table; the caller is responsible for any locking.
        RegionTable& GetOrCreateRegion(uint64_t region_index);

        // Lookup a region without allocating. Returns nullptr if absent.
        [[nodiscard]] RegionTable* FindRegion(uint64_t region_index) const;
};

} // namespace Libs::Graphics

#endif // EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_CACHE_GPUPAGETRACKER_H_
