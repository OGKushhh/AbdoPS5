#include "loader/pkg.h"

#include "common/assert.h"
#include "common/logging/log.h"
#include "common/stringUtils.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <vector>

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
        m_file_path = filepath; // Kyty-005: keep the path so ExtractAll can re-open the file
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

// Read the file table from the PKG. The table is at pkg_table_entry_offset
// and contains pkg_table_entry_count entries of 32 bytes each.
// After the entries, there's a filename table (null-terminated strings).
bool Pkg::ParseEntries() {
        if (!m_entries.empty()) {
                return true; // already parsed
        }
        if (m_file_path.empty()) {
                LOGF("PKG: ParseEntries called but no file is open\n");
                return false;
        }
        if (m_header.pkg_table_entry_count == 0 || m_header.pkg_table_entry_offset == 0) {
                LOGF("PKG: no file table in PKG\n");
                return false;
        }

        std::ifstream file(m_file_path, std::ios::binary);
        if (!file) {
                LOGF("PKG: cannot re-open %s for entry table read\n",
                     Common::PathToString(m_file_path).c_str());
                return false;
        }

        m_entries.resize(m_header.pkg_table_entry_count);
        file.seekg(m_header.pkg_table_entry_offset);
        file.read(reinterpret_cast<char*>(m_entries.data()),
                  static_cast<std::streamsize>(m_entries.size() * sizeof(PkgEntry)));
        if (!file) {
                LOGF("PKG: failed to read %u entry table records\n",
                     m_header.pkg_table_entry_count);
                m_entries.clear();
                return false;
        }

        LOGF("PKG: parsed %zu file table entries\n", m_entries.size());
        return true;
}

bool Pkg::ExtractPfs(const std::filesystem::path& extract_dir, std::string& failreason) {
        if (m_header.pfs_image_size == 0) {
                failreason = "PKG has no PFS image";
                return false;
        }

        if (m_encrypted) {
                failreason = "PKG is encrypted (retail). Retail PKG decryption requires Sony's "
                             "private keyset, which is not public and cannot be legally shipped "
                             "with the emulator. Use FPKG (fake PKG) files instead — they are "
                             "unencrypted and don't need any keys.";
                LOGF("PKG: cannot extract PFS from encrypted PKG (drm_type=%u)\n",
                     m_header.pkg_drm_type);
                return false;
        }

        if (m_file_path.empty()) {
                failreason = "no PKG file is open";
                return false;
        }

        std::filesystem::create_directories(extract_dir);

        // Read the PFS image from the PKG file
        std::ifstream file(m_file_path, std::ios::binary);
        if (!file) {
                failreason = "cannot re-open PKG: " + Common::PathToString(m_file_path);
                return false;
        }

        // Seek to pfs_image_offset and read pfs_image_size bytes
        file.seekg(static_cast<std::streamoff>(m_header.pfs_image_offset));
        if (!file) {
                failreason = "failed to seek to PFS image offset";
                return false;
        }

        const auto out_path = extract_dir / "pfs_image.bin";
        std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
        if (!out) {
                failreason = "cannot create output file: " + Common::PathToString(out_path);
                return false;
        }

        // Stream the PFS image in 1 MiB chunks (the PFS image can be 50+ GB
        // for big games — don't read it all into memory).
        constexpr size_t kChunkSize = 1u << 20; // 1 MiB
        std::vector<uint8_t> chunk(kChunkSize);
        uint64_t remaining = m_header.pfs_image_size;
        uint64_t copied = 0;

        while (remaining > 0) {
                const auto this_chunk = std::min<uint64_t>(remaining, kChunkSize);
                file.read(reinterpret_cast<char*>(chunk.data()),
                          static_cast<std::streamsize>(this_chunk));
                if (!file) {
                        failreason = "failed to read PFS chunk at offset " +
                                     std::to_string(m_header.pfs_image_offset + copied);
                        return false;
                }
                out.write(reinterpret_cast<const char*>(chunk.data()),
                          static_cast<std::streamsize>(this_chunk));
                if (!out) {
                        failreason = "failed to write PFS chunk to output file";
                        return false;
                }
                remaining -= this_chunk;
                copied += this_chunk;
        }

        LOGF("PKG: extracted PFS image (%llu bytes) to %s\n",
             m_header.pfs_image_size, Common::PathToString(out_path).c_str());
        return true;
}

bool Pkg::ExtractAll(const std::filesystem::path& extract_dir, std::string& failreason) {
        if (m_encrypted) {
                failreason = "PKG is encrypted (retail). Retail PKG decryption requires Sony's "
                             "private keyset, which is not public and cannot be legally shipped "
                             "with the emulator. Use FPKG (fake PKG) files instead — they are "
                             "unencrypted and don't need any keys.";
                return false;
        }

        if (m_file_path.empty()) {
                failreason = "no PKG file is open";
                return false;
        }

        if (!ParseEntries()) {
                failreason = "failed to parse PKG entry table";
                return false;
        }

        std::filesystem::create_directories(extract_dir);

        std::ifstream file(m_file_path, std::ios::binary);
        if (!file) {
                failreason = "cannot re-open PKG: " + Common::PathToString(m_file_path);
                return false;
        }

        // Read the filename table. It sits right after the entry table.
        // Each PkgEntry.filename_offset is relative to the start of the
        // filename table (which is at pkg_table_entry_offset + entries * 32).
        const auto entries_bytes = m_entries.size() * sizeof(PkgEntry);
        const auto filenames_offset =
            m_header.pkg_table_entry_offset + static_cast<uint64_t>(entries_bytes);

        // The filename table ends where the body starts (or somewhere before it).
        // Compute the max filename table size as body_offset - filenames_offset,
        // clamped to a sane upper bound.
        uint64_t filenames_size = 0;
        if (m_header.pkg_body_offset > filenames_offset) {
                filenames_size = m_header.pkg_body_offset - filenames_offset;
        } else {
                filenames_size = 0x10000; // fallback: 64 KiB
        }
        // Clamp to 1 MiB so we can't allocate a huge buffer if the PKG is malformed.
        filenames_size = std::min<uint64_t>(filenames_size, 1u << 20);

        std::vector<uint8_t> filename_table(filenames_size);
        file.seekg(static_cast<std::streamoff>(filenames_offset));
        file.read(reinterpret_cast<char*>(filename_table.data()),
                 static_cast<std::streamsize>(filenames_size));
        if (!file) {
                failreason = "failed to read filename table";
                return false;
        }

        // Helper: read a null-terminated string at a given offset in the filename table.
        auto read_name = [&](uint32_t offset) -> std::string {
                if (offset >= filename_table.size()) return "unknown_" + std::to_string(offset);
                auto end = std::find(filename_table.begin() + offset,
                                     filename_table.end(), 0);
                return std::string(filename_table.begin() + offset, end);
        };

        // Stream each entry's data into a file.
        // Body offset is where the entry data begins; entry.offset is relative to body.
        constexpr size_t kChunkSize = 1u << 20; // 1 MiB
        std::vector<uint8_t> chunk(kChunkSize);
        uint32_t extracted = 0;

        for (const auto& entry : m_entries) {
                // Skip entries that aren't actual files (e.g., 0x1000 = directory marker)
                if (entry.size == 0) continue;

                const auto name = read_name(entry.filename_offset);
                if (name.empty()) continue;

                const auto out_path = extract_dir / name;
                std::filesystem::create_directories(out_path.parent_path());

                // Entries store offsets relative to the body offset
                const uint64_t abs_offset = m_header.pkg_body_offset + entry.offset;
                file.seekg(static_cast<std::streamoff>(abs_offset));
                if (!file) {
                        LOGF("PKG: failed to seek to entry '%s' at 0x%llX — skipping\n",
                             name.c_str(), abs_offset);
                        continue;
                }

                std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
                if (!out) {
                        LOGF("PKG: cannot create %s — skipping\n",
                             Common::PathToString(out_path).c_str());
                        continue;
                }

                uint64_t remaining = entry.size;
                while (remaining > 0) {
                        const auto this_chunk = std::min<uint64_t>(remaining, kChunkSize);
                        file.read(reinterpret_cast<char*>(chunk.data()),
                                  static_cast<std::streamsize>(this_chunk));
                        if (!file) {
                                LOGF("PKG: short read on entry '%s' — file truncated\n", name.c_str());
                                break;
                        }
                        out.write(reinterpret_cast<const char*>(chunk.data()),
                                  static_cast<std::streamsize>(this_chunk));
                        remaining -= this_chunk;
                }
                ++extracted;
        }

        LOGF("PKG: extracted %u/%zu files to %s\n",
             extracted, m_entries.size(), Common::PathToString(extract_dir).c_str());

        // Also extract the PFS image alongside the files (some tools need it).
        if (m_header.pfs_image_size > 0) {
                std::string pfs_failreason;
                if (!ExtractPfs(extract_dir, pfs_failreason)) {
                        LOGF("PKG: ExtractPfs warning: %s\n", pfs_failreason.c_str());
                        // non-fatal — files may still have been extracted
                }
        }

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
