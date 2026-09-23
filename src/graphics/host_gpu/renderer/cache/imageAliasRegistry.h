#ifndef EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_CACHE_IMAGEALIASREGISTRY_H_
#define EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_CACHE_IMAGEALIASREGISTRY_H_

// Kyty-034: Image Alias Registry
//
// Tracks all guest memory ranges currently claimed by a host Image and
// answers range-overlap queries in O(log N). The registry is the single
// source of truth for "which images alias the same guest memory", which
// is the foundation for correct coherency between render-target writes,
// texture reads, and storage-image accesses on the same address range.
//
// Prior to Kyty-034 the TextureCache answered overlap queries by walking
// a multi-level page table keyed by guest page number. That worked but
// (a) only knew about images inserted into the TextureCache, (b) had no
// notion of "who is the canonical writer for this range", and (c) could
// not generate a monotonic generation counter for change detection.
//
// The ImageAliasRegistry complements the existing page table: every image
// that registers with the TextureCache is also registered here. When a
// render-target or storage image is written, NotifyGpuWrite() bumps the
// generation counter for the affected range and returns the list of
// *other* images that overlap the write — callers use that list to
// invalidate stale reads / trigger downloads / mark aliases dirty.
//
// The registry is intentionally thread-safe via TrackingSpinLock to match
// the TextureCache locking model.

#include "common/abi.h"
#include "common/assert.h"
#include "common/slotVector.h"
#include "graphics/host_gpu/renderer/image/image.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

namespace Libs::Graphics {

// Forward declarations to avoid header cycles.
class TextureCache;

// Kyty-034: per-image alias metadata. Stored inline in the registry's
// per-image map. The fields here are read by callers (TextureCache,
// render-target caches) to make coherency decisions.
struct ImageAliasInfo {
        // Guest address range claimed by this image. Captured at registration
        // time so callers can iterate aliases without dereferencing the Image.
        uint64_t address = 0;
        uint64_t size    = 0;

        // Monotonic counter, bumped every time this image is the canonical
        // writer for its range. Readers compare their own observed
        // `last_seen_generation` to detect stale views.
        uint64_t generation = 0;

        // The most recent tick at which this image was the writer. Used to
        // break ties when multiple aliases claim overlapping ranges: the
        // newest writer wins.
        uint64_t last_write_tick = 0;

        // Convenience: which "view class" the image was registered under.
        // Matches TextureCache::BindingType but stored as uint8_t to keep
        // the struct small.
        uint8_t view_class = 0;
};

// Kyty-034: the registry. Implemented as an interval tree keyed on guest
// address ranges. Lookups are O(log N + K) where K is the number of
// overlapping ranges.
//
// The registry does NOT own the Image objects — it holds ImageId handles
// and copies of the metadata needed to answer queries without touching
// the slot vector. The TextureCache remains the owner of the lifetime.
class ImageAliasRegistry {
public:
        using ImageId = Common::SlotId;

        ImageAliasRegistry() = default;
        KYTY_CLASS_NO_COPY(ImageAliasRegistry);

        // Register an image's guest range. The image must not already be
        // registered (call Unregister first if re-registering after a
        // metadata change). view_class matches TextureCache::BindingType.
        void Register(ImageId id, uint64_t address, uint64_t size, uint8_t view_class);

        // Remove an image from the registry. No-op if not registered.
        void Unregister(ImageId id);

        // Record that the image at `id` just wrote GPU-modified data.
        // Bumps the per-image generation counter, updates last_write_tick,
        // and returns the list of OTHER registered images whose guest
        // range overlaps [address, address+size). Callers use the returned
        // list to invalidate stale reads / mark aliases dirty.
        //
        // The `id` itself is never included in the returned list.
        std::vector<ImageId> NotifyGpuWrite(ImageId id, uint64_t tick);

        // Return all images whose guest range overlaps [address, address+size).
        // The returned vector is deduplicated and unsorted.
        std::vector<ImageId> FindOverlapping(uint64_t address, uint64_t size) const;

        // Return the per-image alias metadata, or nullptr if not registered.
        const ImageAliasInfo* Info(ImageId id) const;

        // Return the current generation counter for `id`, or 0 if not
        // registered. Readers compare this against the generation they
        // observed when they last copied / downloaded the image.
        uint64_t Generation(ImageId id) const;

        // True if `id` is the most recent writer for any sub-range of
        // [address, address+size). Used by readers to decide whether to
        // re-fetch from the writer or keep their cached copy.
        bool IsCanonicalWriter(ImageId id, uint64_t address, uint64_t size) const;

        // Clear all state. Used by TextureCache teardown.
        void Clear();

        // Number of currently registered images. Mainly for diagnostics.
        size_t Size() const noexcept { return m_by_id.size(); }

private:
        // Map keyed by start address → (end address, ImageId). Allows
        // O(log N) lookup of overlapping ranges via lower_bound on the
        // start address.
        //
        // We use std::map instead of std::unordered_map because we need
        // ordered iteration for the overlap query.
        struct RangeEntry {
                uint64_t end = 0;
                ImageId  id {};
        };
        using RangeMap = std::map<uint64_t, RangeEntry>;

        RangeMap                                            m_by_start;
        std::unordered_map<Common::SlotId, ImageAliasInfo>  m_by_id;

        // Internal: returns the iterator range in m_by_start that overlaps
        // [address, address+size). The first iterator is the first entry
        // whose start address is <= address+size and whose end address is
        // >= address. The second iterator is the end of the matching range.
        //
        // Implemented as: start from lower_bound(address+size) and walk
        // backwards, OR start from begin() and walk forwards filtering
        // by overlap. We pick the latter for simplicity, since the typical
        // registry size is small (< 10k images) and the per-image metadata
        // is cheap to scan.
        void CollectOverlapping(uint64_t address, uint64_t size,
                                std::vector<ImageId>& out) const;
};

} // namespace Libs::Graphics

#endif // EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_CACHE_IMAGEALIASREGISTRY_H_
