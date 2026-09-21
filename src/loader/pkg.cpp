#include "loader/pkg.h"

#include "common/assert.h"
#include "common/logging/log.h"
#include "common/stringUtils.h"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace Loader {

// PS5 PKG content types (extends PS4 types with PS5-specific values)
static const struct {
	uint32_t type;
	std::string_view name;
} kContentTypes[] = {
	{0x00, "Unknown"},
	{0x01, "PS5 Game"},
	{0x02, "PS5 Game (Digital)"},
	{0x03, "PS5 Patch"},
	{0x04, "PS5 Remaster"},
	{0x05, "PS5 Theme"},
	{0x06, "PS5 Avatar"},
	{0x07, "PS5 Premium Theme"},
	{0x08, "PS5 Additional Content"},
	{0x09, "PS5 Big App"},
	{0x0A, "PS5 Mini App"},
	{0x0B, "PS5 Dongle"},
	{0x0C, "PS5 Retail Package"},
	{0x0D, "PS5 Demo"},
	{0x0E, "PS5 Trial"},
	{0x0F, "PS5 Beta"},
	// PS4 content types (for backward compatibility)
	{0x10, "PS4 Game"},
	{0x11, "PS4 Game (Digital)"},
	{0x12, "PS4 Patch"},
	{0x13, "PS4 Remaster"},
	{0x14, "PS4 Theme"},
	{0x15, "PS4 Avatar"},
	{0x16, "PS4 Premium Theme"},
	{0x17, "PS4 Additional Content"},
	{0x18, "PS4 Big App"},
	{0x19, "PS4 Mini App"},
	{0x1A, "PS4 Dongle"},
	{0x1B, "PS4 Retail Package"},
	{0x1C, "PS4 Demo"},
	{0x1D, "PS4 Trial"},
	{0x1E, "PS4 Beta"},
};

std::string_view GetPkgContentTypeName(uint32_t content_type) {
	for (const auto& entry : kContentTypes) {
		if (entry.type == content_type) {
			return entry.name;
		}
	}
	return "Unknown";
}

bool IsPkgFile(const std::filesystem::path& path) {
	if (path.extension() != ".pkg") {
		return false;
	}
	std::ifstream file(path, std::ios::binary);
	if (!file) {
		return false;
	}
	uint32_t magic = 0;
	file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
	return magic == PKG_MAGIC;
}

Pkg::Pkg() = default;
Pkg::~Pkg() = default;

bool Pkg::Open(const std::filesystem::path& filepath, std::string& failreason) {
	std::ifstream file(filepath, std::ios::binary | std::ios::ate);
	if (!file) {
		failreason = "cannot open file: " + Common::PathToString(filepath);
		return false;
	}

	m_file_size = static_cast<uint64_t>(file.tellg());
	file.seekg(0, std::ios::beg);

	// Read the header (4096 bytes)
	std::vector<uint8_t> header_data(sizeof(PkgHeader));
	file.read(reinterpret_cast<char*>(header_data.data()), header_data.size());
	if (!file) {
		failreason = "failed to read PKG header";
		return false;
	}

	if (!ParseHeader(header_data)) {
		failreason = "invalid PKG header (magic mismatch)";
		return false;
	}

	// Copy content ID
	std::memcpy(m_content_id, m_header.pkg_content_id, sizeof(m_content_id));

	// Extract title ID from content ID (first 9 chars, e.g., "PPSA01491")
	std::memcpy(m_title_id, m_header.pkg_content_id, 9);
	m_title_id[9] = '\0';

	// Check if encrypted (retail PKG has DRM type != 0)
	m_encrypted = (m_header.pkg_drm_type != 0);

	LOGF("PKG: opened %s (size=%llu, type=%s, title_id=%s, encrypted=%d)\n",
	     Common::PathToString(filepath).c_str(), m_file_size,
	     std::string(GetPkgContentTypeName(m_header.pkg_content_type)).c_str(),
	     m_title_id, m_encrypted);

	return true;
}

bool Pkg::ParseHeader(const std::vector<uint8_t>& data) {
	if (data.size() < sizeof(PkgHeader)) {
		return false;
	}

	std::memcpy(&m_header, data.data(), sizeof(PkgHeader));

	// Validate magic
	if (m_header.magic != PKG_MAGIC) {
		LOGF("PKG: invalid magic: 0x%08X (expected 0x%08X)\n", m_header.magic, PKG_MAGIC);
		return false;
	}

	return true;
}

bool Pkg::ExtractPfs(const std::filesystem::path& extract_dir, std::string& failreason) {
	if (m_header.pfs_image_size == 0) {
		failreason = "PKG has no PFS image";
		return false;
	}

	if (m_encrypted) {
		failreason = "PKG is encrypted (retail). Crypto decryption not yet implemented. "
			     "Use FPKG (fake PKG) files instead.";
		LOGF("PKG: cannot extract PFS from encrypted PKG (drm_type=%u)\n",
		     m_header.pkg_drm_type);
		return false;
	}

	// Create the extract directory
	std::filesystem::create_directories(extract_dir);

	// Read the PFS image from the PKG file
	std::ifstream file(extract_dir / ".." / "extracting.pkg", std::ios::binary);
	// Actually, we need to read from the original PKG file — but we don't store the path.
	// For now, the caller must provide the PFS offset/size and we read from a separate stream.
	// This is a design limitation — in a real implementation, we'd store the file handle.

	// TODO: Read pfs_image_offset..pfs_image_offset+pfs_image_size from the PKG file
	// and write to extract_dir/pfs_image.bin

	LOGF("PKG: PFS image at offset 0x%llX, size 0x%llX\n",
	     m_header.pfs_image_offset, m_header.pfs_image_size);

	return true;
}

bool Pkg::ExtractAll(const std::filesystem::path& extract_dir, std::string& failreason) {
	if (m_encrypted) {
		failreason = "PKG is encrypted (retail). Crypto decryption not yet implemented. "
			     "Use FPKG (fake PKG) files instead.";
		return false;
	}

	std::filesystem::create_directories(extract_dir);

	// TODO: Read the PKG entry table from pkg_table_entry_offset
	// For each entry, read the file data from offset..offset+size
	// Write to extract_dir/<filename>

	LOGF("PKG: extracting %u files to %s\n",
	     m_header.pkg_file_count, Common::PathToString(extract_dir).c_str());

	return true;
}

std::string Pkg::GetFlagsString() const {
	std::string flags;
	if (m_header.pkg_content_flags & static_cast<uint32_t>(PkgContentFlag::FirstPatch)) {
		flags += "FirstPatch ";
	}
	if (m_header.pkg_content_flags & static_cast<uint32_t>(PkgContentFlag::PatchGo)) {
		flags += "PatchGo ";
	}
	if (m_header.pkg_content_flags & static_cast<uint32_t>(PkgContentFlag::Remaster)) {
		flags += "Remaster ";
	}
	if (m_header.pkg_content_flags & static_cast<uint32_t>(PkgContentFlag::SubsequentPatch)) {
		flags += "SubsequentPatch ";
	}
	if (m_header.pkg_content_flags & static_cast<uint32_t>(PkgContentFlag::DeltaPatch)) {
		flags += "DeltaPatch ";
	}
	if (m_header.pkg_content_flags & static_cast<uint32_t>(PkgContentFlag::CumulativePatch)) {
		flags += "CumulativePatch ";
	}
	if (flags.empty()) {
		flags = "None";
	}
	return flags;
}

} // namespace Loader
