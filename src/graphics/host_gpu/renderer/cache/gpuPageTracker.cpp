// Kyty-037: GPU Page Generation Tracker — implementation.
//
// See gpuPageTracker.h for design rationale.

#include "graphics/host_gpu/renderer/cache/gpuPageTracker.h"

#include "common/assert.h"

#include <algorithm>
#include <cstdint>

namespace Libs::Graphics {

namespace {

// Round an address down to the next 4 KiB page boundary.
[[nodiscard]] constexpr uint64_t PageFloor(uint64_t addr) noexcept {
        return addr & ~(TRACKER_PAGE_SIZE - 1u);
}

// Round an address up to the next 4 KiB page boundary.
[[nodiscard]] constexpr uint64_t PageCeil(uint64_t addr) noexcept {
        return (addr + TRACKER_PAGE_SIZE - 1u) & ~(TRACKER_PAGE_SIZE - 1u);
}

// Region index for a given address.
[[nodiscard]] constexpr uint64_t RegionOf(uint64_t addr) noexcept {
        return addr / TRACKER_REGION_SIZE;
}

// Page index within a region for a given address.
[[nodiscard]] constexpr uint64_t PageInRegion(uint64_t addr) noexcept {
        return (addr % TRACKER_REGION_SIZE) / TRACKER_PAGE_SIZE;
}

} // namespace

GpuPageTracker::RegionTable& GpuPageTracker::GetOrCreateRegion(uint64_t region_index) {
        auto it = m_regions.find(region_index);
        if (it != m_regions.end()) {
                return *it->second;
        }
        auto table = std::make_unique<RegionTable>(TRACKER_REGION_PAGES);
        auto& ref  = *table;
        m_regions.emplace(region_index, std::move(table));
        return ref;
}

GpuPageTracker::RegionTable* GpuPageTracker::FindRegion(uint64_t region_index) const {
        auto it = m_regions.find(region_index);
        return it == m_regions.end() ? nullptr : it->second.get();
}

void GpuPageTracker::NotifyGpuWrite(uint64_t address, uint64_t size, uint64_t tick) {
        (void)tick; // reserved for future per-tick ordering
        if (size == 0) {
                return;
        }
        const uint64_t start_page = PageFloor(address) / TRACKER_PAGE_SIZE;
        const uint64_t end_addr   = address + size - 1u;
        const uint64_t end_page   = PageFloor(end_addr) / TRACKER_PAGE_SIZE;

        // Walk every page in [start_page, end_page]. Cross region boundaries
        // as needed. We touch at most (size / 4 KiB) + 1 pages, so the cost
        // is O(pages) which is the same as the existing page-walker.
        for (uint64_t page = start_page; page <= end_page; ++page) {
                const auto region_index = page / TRACKER_REGION_PAGES;
                const auto page_in_region = page % TRACKER_REGION_PAGES;
                auto& region = GetOrCreateRegion(region_index);
                auto& gen    = region.pages[page_in_region];
                // Wrap-around is intentional: Generation is uint16_t, so after
                // 65535 writes the counter rolls back to 0. Callers comparing
                // against a cached snapshot should treat the comparison as
                // "different", not "newer". For typical workloads a page is
                // written a few hundred times per session, so wrap-around is
                // unlikely.
                Generation current = gen.load(std::memory_order_relaxed);
                Generation next    = static_cast<Generation>(current + 1u);
                gen.store(next, std::memory_order_release);
        }
}

GpuPageTracker::Generation GpuPageTracker::MaxGeneration(uint64_t address, uint64_t size) const {
        if (size == 0) {
                return kInitialGeneration;
        }
        const uint64_t start_page = PageFloor(address) / TRACKER_PAGE_SIZE;
        const uint64_t end_addr   = address + size - 1u;
        const uint64_t end_page   = PageFloor(end_addr) / TRACKER_PAGE_SIZE;

        Generation max_gen = kInitialGeneration;
        for (uint64_t page = start_page; page <= end_page; ++page) {
                const auto region_index = page / TRACKER_REGION_PAGES;
                const auto page_in_region = page % TRACKER_REGION_PAGES;
                const auto* region = FindRegion(region_index);
                if (region == nullptr) {
                        continue;
                }
                const auto gen = region->pages[page_in_region].load(std::memory_order_acquire);
                if (gen > max_gen) {
                        max_gen = gen;
                }
        }
        return max_gen;
}

GpuPageTracker::Generation GpuPageTracker::PageGeneration(uint64_t address) const {
        const auto page = PageFloor(address) / TRACKER_PAGE_SIZE;
        const auto region_index = page / TRACKER_REGION_PAGES;
        const auto page_in_region = page % TRACKER_REGION_PAGES;
        const auto* region = FindRegion(region_index);
        if (region == nullptr) {
                return kInitialGeneration;
        }
        return region->pages[page_in_region].load(std::memory_order_acquire);
}

bool GpuPageTracker::HasChangedSince(uint64_t address, uint64_t size, Generation since) const {
        if (since == kInitialGeneration) {
                // Caller has never observed this region. Anything non-zero
                // counts as "changed".
                return MaxGeneration(address, size) > kInitialGeneration;
        }
        // Strict-greater-than: if the cached snapshot matches the current
        // max generation exactly, the range is unchanged.
        return MaxGeneration(address, size) != since;
}

void GpuPageTracker::ClearRange(uint64_t address, uint64_t size) {
        if (size == 0) {
                return;
        }
        const uint64_t start_page = PageFloor(address) / TRACKER_PAGE_SIZE;
        const uint64_t end_addr   = address + size - 1u;
        const uint64_t end_page   = PageFloor(end_addr) / TRACKER_PAGE_SIZE;

        for (uint64_t page = start_page; page <= end_page; ++page) {
                const auto region_index = page / TRACKER_REGION_PAGES;
                const auto page_in_region = page % TRACKER_REGION_PAGES;
                auto* region = FindRegion(region_index);
                if (region == nullptr) {
                        continue;
                }
                region->pages[page_in_region].store(kInitialGeneration,
                                                    std::memory_order_release);
        }
}

void GpuPageTracker::Clear() {
        m_regions.clear();
}

size_t GpuPageTracker::TrackedPageCount() const {
        size_t total = 0;
        for (const auto& [_, region] : m_regions) {
                for (const auto& gen : region->pages) {
                        if (gen.load(std::memory_order_relaxed) != kInitialGeneration) {
                                ++total;
                        }
                }
        }
        return total;
}

} // namespace Libs::Graphics
