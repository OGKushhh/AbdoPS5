#ifndef KYTY_KERNEL_MEMORY_COMPRESSION_H_
#define KYTY_KERNEL_MEMORY_COMPRESSION_H_

#include "common/common.h"

#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <vector>

namespace Libs::LibKernel::Memory {

// Kyty-010: Memory compression for low-RAM systems.
//
// PS5 has 16 GB RAM (32 GB DevKit). On host systems with 8-16 GB,
// large PS5 games can OOM. This system compresses cold guest memory
// pages into an in-process compressed store, similar to macOS's memory
// compressor or Linux's zswap.
//
// The compression is purely host-side — the guest sees no difference.
// When a compressed page is accessed (via page fault), it's decompressed
// on-demand and marked as "hot" (won't be re-compressed for a while).
//
// Based on the shadPS4 Shadlix fork's memory_compression, adapted for
// KytyPS5's memory model.

struct CompressedMemoryBlock {
	uint64_t virtual_addr;
	uint64_t size;
	std::vector<uint8_t> compressed_data;
	std::chrono::steady_clock::time_point last_access;
	uint32_t access_count;
};

class MemoryCompression {
public:
	MemoryCompression();
	~MemoryCompression();

	// Compress a memory block if it hasn't been accessed recently.
	// The data vector is moved (not copied).
	bool TryCompressBlock(uint64_t virtual_addr, uint64_t size, std::vector<uint8_t> data);

	// Decompress a memory block when accessed.
	// Returns the original data, or empty vector if not compressed.
	std::vector<uint8_t> DecompressBlock(uint64_t virtual_addr);

	// Check if a block is compressed.
	bool IsCompressed(uint64_t virtual_addr) const;

	// Get compression statistics.
	struct CompressionStats {
		uint64_t total_blocks      = 0;
		uint64_t compressed_blocks = 0;
		uint64_t total_original_size = 0;
		uint64_t total_compressed_size = 0;
		uint64_t memory_saved_bytes  = 0;
		double compression_ratio    = 0.0;
	};

	CompressionStats GetStats() const;

	// Clean up old compressed blocks (LRU eviction).
	void CleanupOldBlocks();

	// Set compression level (0=disabled, 1=fast, 2=balanced, 3=max).
	void SetCompressionLevel(int level);
	bool IsEnabled() const { return m_enabled; }
	void SetEnabled(bool enabled) { m_enabled = enabled; }

	// Check if a block should be compressed based on access patterns.
	bool ShouldCompressBlock(const CompressedMemoryBlock& block) const;

	// Update block access information (called when a block is accessed).
	void UpdateBlockAccess(uint64_t virtual_addr);

private:
	mutable std::mutex m_mutex;
	std::map<uint64_t, CompressedMemoryBlock> m_compressed_blocks;
	int m_compression_level = 0;
	bool m_enabled = false;

	// Simple RLE-like compression (placeholder — replace with zlib/lz4)
	std::vector<uint8_t> CompressData(const std::vector<uint8_t>& data);
	std::vector<uint8_t> DecompressData(const std::vector<uint8_t>& compressed_data,
					     uint64_t original_size);
};

// Global accessor
MemoryCompression& GetMemoryCompression();

} // namespace Libs::LibKernel::Memory

#endif // KYTY_KERNEL_MEMORY_COMPRESSION_H_
