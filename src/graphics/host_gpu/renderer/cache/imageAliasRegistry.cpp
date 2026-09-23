// Kyty-034: Image Alias Registry implementation.
//
// See imageAliasRegistry.h for design rationale.

#include "graphics/host_gpu/renderer/cache/imageAliasRegistry.h"

#include "common/assert.h"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace Libs::Graphics {

namespace {

// Returns true iff [a_start, a_end) overlaps [b_start, b_end).
// Both ranges are half-open: a_end and b_end are exclusive.
[[nodiscard]] constexpr bool RangesOverlap(uint64_t a_start, uint64_t a_end,
                                            uint64_t b_start, uint64_t b_end) noexcept {
        return a_start < b_end && b_start < a_end;
}

} // namespace

void ImageAliasRegistry::Register(ImageId id, uint64_t address, uint64_t size,
                                 uint8_t view_class) {
        if (size == 0) {
                EXIT("ImageAliasRegistry: cannot register a zero-size range\n");
        }
        const auto [it, inserted] = m_by_id.try_emplace(id, ImageAliasInfo {
                .address         = address,
                .size            = size,
                .generation      = 0,
                .last_write_tick = 0,
                .view_class      = view_class,
        });
        if (!inserted) {
                EXIT("ImageAliasRegistry: image id already registered\n");
        }
        m_by_start[address] = RangeEntry {address + size, id};
}

void ImageAliasRegistry::Unregister(ImageId id) {
        const auto it = m_by_id.find(id);
        if (it == m_by_id.end()) {
                return;
        }
        const auto address = it->second.address;
        m_by_id.erase(it);

        // Erase from m_by_start. Note that multiple images can share a start
        // address; we only erase the entry that matches our id.
        const auto [first, last] = m_by_start.equal_range(address);
        for (auto rit = first; rit != last; ++rit) {
                if (rit->second.id == id) {
                        m_by_start.erase(rit);
                        break;
                }
        }
}

std::vector<ImageAliasRegistry::ImageId>
ImageAliasRegistry::NotifyGpuWrite(ImageId id, uint64_t tick) {
        auto it = m_by_id.find(id);
        if (it == m_by_id.end()) {
                EXIT("ImageAliasRegistry: NotifyGpuWrite on unregistered image\n");
        }
        auto& info = it->second;
        info.generation += 1;
        info.last_write_tick = tick;

        std::vector<ImageId> overlaps;
        CollectOverlapping(info.address, info.size, overlaps);
        // Remove the writer itself from the returned list.
        overlaps.erase(std::remove(overlaps.begin(), overlaps.end(), id), overlaps.end());
        return overlaps;
}

std::vector<ImageAliasRegistry::ImageId>
ImageAliasRegistry::FindOverlapping(uint64_t address, uint64_t size) const {
        std::vector<ImageId> out;
        CollectOverlapping(address, size, out);
        // Deduplicate — CollectOverlapping may return the same id twice if
        // the query range straddles two registered ranges that happen to
        // share an image (rare, but possible with overlapping registrations).
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
}

const ImageAliasInfo* ImageAliasRegistry::Info(ImageId id) const {
        const auto it = m_by_id.find(id);
        return it == m_by_id.end() ? nullptr : &it->second;
}

uint64_t ImageAliasRegistry::Generation(ImageId id) const {
        const auto it = m_by_id.find(id);
        return it == m_by_id.end() ? 0 : it->second.generation;
}

bool ImageAliasRegistry::IsCanonicalWriter(ImageId id, uint64_t address,
                                           uint64_t size) const {
        const auto writer_info = Info(id);
        if (writer_info == nullptr) {
                return false;
        }
        // Walk all overlapping ranges. If any other image has a strictly
        // newer last_write_tick, we are not the canonical writer.
        const auto query_end = address + size;
        for (const auto& [start, entry] : m_by_start) {
                if (!RangesOverlap(start, entry.end, address, query_end)) {
                        continue;
                }
                if (entry.id == id) {
                        continue;
                }
                const auto other = Info(entry.id);
                if (other == nullptr) {
                        continue;
                }
                if (other->last_write_tick > writer_info->last_write_tick) {
                        return false;
                }
        }
        return true;
}

void ImageAliasRegistry::Clear() {
        m_by_start.clear();
        m_by_id.clear();
}

void ImageAliasRegistry::CollectOverlapping(uint64_t address, uint64_t size,
                                            std::vector<ImageId>& out) const {
        if (size == 0) {
                return;
        }
        const auto query_end = address + size;
        // Walk m_by_start in address order. We can stop early once we hit an
        // entry whose start address is >= query_end, because all subsequent
        // entries are even further to the right.
        for (auto it = m_by_start.begin(); it != m_by_start.end() && it->first < query_end; ++it) {
                const auto& [start, entry] = *it;
                if (RangesOverlap(start, entry.end, address, query_end)) {
                        out.push_back(entry.id);
                }
        }
}

} // namespace Libs::Graphics
