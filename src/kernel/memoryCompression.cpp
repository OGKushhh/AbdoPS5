#include "kernel/memoryCompression.h"

#include "common/logging/log.h"

#include <algorithm>
#include <cstring>

namespace Libs::LibKernel::Memory {

MemoryCompression::MemoryCompression() = default;
MemoryCompression::~MemoryCompression() = default;

// Simple RLE compression (placeholder).
// In production, replace with lz4 or zlib for better ratios.
// RLE is chosen here because it's zero-dependency and simple to verify.
std::vector<uint8_t> MemoryCompression::CompressData(const std::vector<uint8_t>& data) {
	if (data.empty()) {
		return {};
	}

	std::vector<uint8_t> result;
	result.reserve(data.size() / 2); // Estimate ~50% ratio

	// Store original size as 8-byte prefix
	const uint64_t orig_size = data.size();
	result.resize(sizeof(orig_size));
	std::memcpy(result.data(), &orig_size, sizeof(orig_size));

	// Simple RLE: [count, byte] pairs
	size_t i = 0;
	while (i < data.size()) {
		uint8_t current = data[i];
		uint8_t count = 1;
		while (i + count < data.size() && data[i + count] == current && count < 255) {
			count++;
		}
		result.push_back(count);
		result.push_back(current);
		i += count;
	}

	return result;
}

std::vector<uint8_t> MemoryCompression::DecompressData(const std::vector<uint8_t>& compressed,
							uint64_t original_size) {
	if (compressed.empty() || original_size == 0) {
		return {};
	}

	// Skip the 8-byte size prefix
	if (compressed.size() < sizeof(uint64_t)) {
		return {};
	}

	std::vector<uint8_t> result;
	result.reserve(original_size);

	// Decode RLE: [count, byte] pairs
	size_t i = sizeof(uint64_t); // Skip size prefix
	while (i + 1 < compressed.size() && result.size() < original_size) {
		uint8_t count = compressed[i];
		uint8_t byte = compressed[i + 1];
		for (uint8_t j = 0; j < count && result.size() < original_size; j++) {
			result.push_back(byte);
		}
		i += 2;
	}

	return result;
}

bool MemoryCompression::TryCompressBlock(uint64_t virtual_addr, uint64_t size,
					 std::vector<uint8_t> data) {
	if (!m_enabled || data.empty() || size == 0) {
		return false;
	}

	std::scoped_lock lk(m_mutex);

	// Check if already compressed
	if (m_compressed_blocks.find(virtual_addr) != m_compressed_blocks.end()) {
		return false; // Already compressed
	}

	CompressedMemoryBlock block;
	block.virtual_addr = virtual_addr;
	block.size = size;
	block.last_access = std::chrono::steady_clock::now();
	block.access_count = 0;

	// Compress the data
	block.compressed_data = CompressData(data);

	// Only store if compression actually saves space
	if (block.compressed_data.size() < data.size()) {
		m_compressed_blocks[virtual_addr] = std::move(block);
		return true;
	}

	return false; // Compression didn't help
}

std::vector<uint8_t> MemoryCompression::DecompressBlock(uint64_t virtual_addr) {
	std::scoped_lock lk(m_mutex);

	auto it = m_compressed_blocks.find(virtual_addr);
	if (it == m_compressed_blocks.end()) {
		return {}; // Not compressed
	}

	// Decompress
	auto data = DecompressData(it->second.compressed_data, it->second.size);

	// Update access info
	it->second.last_access = std::chrono::steady_clock::now();
	it->second.access_count++;

	// If the block is frequently accessed, remove it from the compressed store
	// (decompress permanently — it's "hot")
	if (it->second.access_count > 3) {
		m_compressed_blocks.erase(it);
	}

	return data;
}

bool MemoryCompression::IsCompressed(uint64_t virtual_addr) const {
	std::scoped_lock lk(m_mutex);
	return m_compressed_blocks.find(virtual_addr) != m_compressed_blocks.end();
}

MemoryCompression::CompressionStats MemoryCompression::GetStats() const {
	std::scoped_lock lk(m_mutex);

	CompressionStats stats;
	stats.compressed_blocks = m_compressed_blocks.size();

	uint64_t total_compressed = 0;
	uint64_t total_original = 0;
	for (const auto& [addr, block] : m_compressed_blocks) {
		total_compressed += block.compressed_data.size();
		total_original += block.size;
	}

	stats.total_blocks = stats.compressed_blocks;
	stats.total_original_size = total_original;
	stats.total_compressed_size = total_compressed;
	stats.memory_saved_bytes = (total_original > total_compressed)
				       ? (total_original - total_compressed)
				       : 0;
	stats.compression_ratio = (total_compressed > 0)
				      ? static_cast<double>(total_original) / total_compressed
				      : 0.0;

	return stats;
}

void MemoryCompression::CleanupOldBlocks() {
	std::scoped_lock lk(m_mutex);

	const auto now = std::chrono::steady_clock::now();
	const auto max_age = std::chrono::minutes(10); // Evict after 10 minutes of no access

	auto it = m_compressed_blocks.begin();
	while (it != m_compressed_blocks.end()) {
		if (now - it->second.last_access > max_age) {
			it = m_compressed_blocks.erase(it);
		} else {
			++it;
		}
	}
}

void MemoryCompression::SetCompressionLevel(int level) {
	m_compression_level = level;
	m_enabled = (level > 0);

	if (m_enabled) {
		LOGF("MemoryCompression: enabled (level %d)\n", level);
	} else {
		LOGF("MemoryCompression: disabled\n");
	}
}

bool MemoryCompression::ShouldCompressBlock(const CompressedMemoryBlock& block) const {
	// Only compress blocks that haven't been accessed in the last 30 seconds
	const auto now = std::chrono::steady_clock::now();
	const auto age = now - block.last_access;
	return age > std::chrono::seconds(30);
}

void MemoryCompression::UpdateBlockAccess(uint64_t virtual_addr) {
	std::scoped_lock lk(m_mutex);

	auto it = m_compressed_blocks.find(virtual_addr);
	if (it != m_compressed_blocks.end()) {
		it->second.last_access = std::chrono::steady_clock::now();
		it->second.access_count++;
	}
}

MemoryCompression& GetMemoryCompression() {
	static MemoryCompression instance;
	return instance;
}

} // namespace Libs::LibKernel::Memory
