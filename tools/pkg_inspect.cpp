// SPDX-FileCopyrightText: Copyright 2026 AbdoPS5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Kyty-012: PKG Inspector tool.
// Views PS5 PKG file contents without extracting them.
// Shows: header info, file table, PFS image location, encryption status.
//
// Usage:
//   kytyps5-pkg-inspect <file.pkg>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#pragma pack(push, 1)

struct PkgHeader {
    uint32_t magic;
    uint32_t pkg_type;
    uint32_t pkg_0x8;
    uint32_t pkg_file_count;
    uint32_t pkg_table_entry_count;
    uint16_t pkg_sc_entry_count;
    uint16_t pkg_table_entry_count_2;
    uint32_t pkg_table_entry_offset;
    uint32_t pkg_sc_entry_data_size;
    uint64_t pkg_body_offset;
    uint64_t pkg_body_size;
    uint64_t pkg_content_offset;
    uint64_t pkg_content_size;
    char     pkg_content_id[0x24];
    uint8_t  pkg_padding[0xC];
    uint32_t pkg_drm_type;
    uint32_t pkg_content_type;
    uint32_t pkg_content_flags;
    uint32_t pkg_promote_size;
    uint32_t pkg_version_date;
    uint32_t pkg_version_hash;
    uint32_t pkg_0x088;
    uint32_t pkg_0x08C;
    uint32_t pkg_0x090;
    uint32_t pkg_0x094;
    uint32_t pkg_iro_tag;
    uint32_t pkg_drm_type_version;
    uint8_t  pkg_zeroes_1[0x60];
    uint8_t  digest_entries1[0x20];
    uint8_t  digest_entries2[0x20];
    uint8_t  digest_table_digest[0x20];
    uint8_t  digest_body_digest[0x20];
    uint8_t  pkg_zeroes_2[0x280];
    uint32_t pkg_0x400;
    uint32_t pfs_image_count;
    uint64_t pfs_image_flags;
    uint64_t pfs_image_offset;
    uint64_t pfs_image_size;
    uint64_t mount_image_offset;
    uint64_t mount_image_size;
    uint64_t pkg_size;
    uint32_t pfs_signed_size;
    uint32_t pfs_cache_size;
    uint8_t  pfs_image_digest[0x20];
    uint8_t  pfs_signed_digest[0x20];
    uint64_t pfs_split_size_nth_0;
    uint64_t pfs_split_size_nth_1;
    uint8_t  pkg_zeroes_3[0xB50];
    uint8_t  pkg_digest[0x20];
};
static_assert(sizeof(PkgHeader) == 0x1000);

struct PkgEntry {
    uint32_t id;
    uint32_t filename_offset;
    uint32_t flags1;
    uint32_t flags2;
    uint32_t offset;
    uint32_t size;
    uint64_t padding;
};
static_assert(sizeof(PkgEntry) == 32);

#pragma pack(pop)

static const char* GetContentTypeName(uint32_t type) {
    switch (type) {
        case 0x01: return "PS5 Game";
        case 0x02: return "PS5 Game (Digital)";
        case 0x03: return "PS5 Patch";
        case 0x04: return "PS5 Remaster";
        case 0x05: return "PS5 Theme";
        case 0x06: return "PS5 Avatar";
        case 0x07: return "PS5 Premium Theme";
        case 0x08: return "PS5 Additional Content";
        case 0x09: return "PS5 Big App";
        case 0x0A: return "PS5 Mini App";
        case 0x0C: return "PS5 Retail Package";
        case 0x0D: return "PS5 Demo";
        case 0x10: return "PS4 Game";
        case 0x12: return "PS4 Patch";
        default: return "Unknown";
    }
}

static const char* GetFlagsString(uint32_t flags, char* buf, size_t buf_size) {
    buf[0] = '\0';
    if (flags & 0x100000)  strcat(buf, "FirstPatch ");
    if (flags & 0x200000)  strcat(buf, "PatchGo ");
    if (flags & 0x400000)  strcat(buf, "Remaster ");
    if (flags & 0x800000)  strcat(buf, "PsCloud ");
    if (flags & 0x2000000) strcat(buf, "GdAc ");
    if (flags & 0x4000000) strcat(buf, "NonGame ");
    if (flags & 0x40000000) strcat(buf, "SubsequentPatch ");
    if (flags & 0x41000000) strcat(buf, "DeltaPatch ");
    if (flags & 0x60000000) strcat(buf, "CumulativePatch ");
    if (buf[0] == '\0') strcpy(buf, "None");
    return buf;
}

static std::string FormatSize(uint64_t bytes) {
    if (bytes >= 1024 * 1024 * 1024)
        return std::to_string(bytes / (1024*1024*1024)) + " GB";
    if (bytes >= 1024 * 1024)
        return std::to_string(bytes / (1024*1024)) + " MB";
    if (bytes >= 1024)
        return std::to_string(bytes / 1024) + " KB";
    return std::to_string(bytes) + " bytes";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::printf("kytyps5-pkg-inspect — PS5 PKG file inspector (AbdoPS5)\n\n");
        std::printf("Usage: kytyps5-pkg-inspect <file.pkg>\n");
        std::printf("\nViews PKG file contents without extracting:\n");
        std::printf("  - Header info (magic, type, flags, content ID)\n");
        std::printf("  - File table (all files with sizes and offsets)\n");
        std::printf("  - PFS image location and size\n");
        std::printf("  - Encryption status (FPKG vs retail)\n");
        return 1;
    }

    std::ifstream file(argv[1], std::ios::binary | std::ios::ate);
    if (!file) {
        std::fprintf(stderr, "Error: cannot open %s\n", argv[1]);
        return 1;
    }

    const auto file_size = static_cast<uint64_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    // Read header
    PkgHeader header{};
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!file || file.gcount() != sizeof(header)) {
        std::fprintf(stderr, "Error: failed to read PKG header\n");
        return 1;
    }

    // Verify magic
    constexpr uint32_t PKG_MAGIC = 0x7F504B47;
    if (header.magic != PKG_MAGIC) {
        std::fprintf(stderr, "Error: invalid PKG magic: 0x%08X (expected 0x%08X)\n",
                     header.magic, PKG_MAGIC);
        return 1;
    }

    // Extract title ID (first 9 chars of content_id)
    char title_id[10] = {};
    std::memcpy(title_id, header.pkg_content_id, 9);

    char flags_buf[256];
    GetFlagsString(header.pkg_content_flags, flags_buf, sizeof(flags_buf));

    bool encrypted = (header.pkg_drm_type != 0);

    // Print header info
    std::printf("=== PKG Header ===\n");
    std::printf("  Magic:          0x%08X (PKG\\x7F)\n", header.magic);
    std::printf("  File size:      %s (%llu bytes)\n", FormatSize(file_size).c_str(), file_size);
    std::printf("  PKG size:       %s (%llu bytes)\n", FormatSize(header.pkg_size).c_str(), header.pkg_size);
    std::printf("  Content ID:     %s\n", header.pkg_content_id);
    std::printf("  Title ID:       %s\n", title_id);
    std::printf("  Content type:   0x%02X (%s)\n", header.pkg_content_type,
                GetContentTypeName(header.pkg_content_type));
    std::printf("  Content flags:  0x%08X (%s)\n", header.pkg_content_flags, flags_buf);
    std::printf("  DRM type:       0x%08X\n", header.pkg_drm_type);
    std::printf("  Encrypted:      %s\n", encrypted ? "YES (retail PKG)" : "NO (FPKG)");
    std::printf("  File count:     %u\n", header.pkg_file_count);
    std::printf("  Table entries:  %u\n", header.pkg_table_entry_count);
    std::printf("  Table offset:   0x%08X\n", header.pkg_table_entry_offset);
    std::printf("  Body offset:    0x%016llX\n", header.pkg_body_offset);
    std::printf("  Body size:      %s (%llu bytes)\n",
                FormatSize(header.pkg_body_size).c_str(), header.pkg_body_size);
    std::printf("  Content offset: 0x%016llX\n", header.pkg_content_offset);
    std::printf("  Content size:   %s (%llu bytes)\n",
                FormatSize(header.pkg_content_size).c_str(), header.pkg_content_size);
    std::printf("  Version date:   0x%08X\n", header.pkg_version_date);
    std::printf("  Version hash:   0x%08X\n", header.pkg_version_hash);

    // PFS image info
    std::printf("\n=== PFS Image ===\n");
    std::printf("  Count:          %u\n", header.pfs_image_count);
    std::printf("  Flags:          0x%016llX\n", header.pfs_image_flags);
    std::printf("  Offset:         0x%016llX\n", header.pfs_image_offset);
    std::printf("  Size:           %s (%llu bytes)\n",
                FormatSize(header.pfs_image_size).c_str(), header.pfs_image_size);
    std::printf("  Signed size:    %u\n", header.pfs_signed_size);
    std::printf("  Cache size:     %u\n", header.pfs_cache_size);

    // Digests (first 8 bytes only)
    std::printf("\n=== Digests (first 8 bytes) ===\n");
    std::printf("  Entry 1:        ");
    for (int i = 0; i < 8; i++) std::printf("%02X", header.digest_entries1[i]);
    std::printf("...\n");
    std::printf("  Entry 2:        ");
    for (int i = 0; i < 8; i++) std::printf("%02X", header.digest_entries2[i]);
    std::printf("...\n");
    std::printf("  Table digest:   ");
    for (int i = 0; i < 8; i++) std::printf("%02X", header.digest_table_digest[i]);
    std::printf("...\n");
    std::printf("  Body digest:    ");
    for (int i = 0; i < 8; i++) std::printf("%02X", header.digest_body_digest[i]);
    std::printf("...\n");
    std::printf("  PKG digest:     ");
    for (int i = 0; i < 8; i++) std::printf("%02X", header.pkg_digest[i]);
    std::printf("...\n");

    // Read file table
    if (header.pkg_table_entry_count > 0 && header.pkg_table_entry_offset > 0) {
        std::printf("\n=== File Table (%u entries) ===\n", header.pkg_table_entry_count);
        std::printf("  %-4s %-10s %-10s %-12s %-12s %-6s\n",
                    "ID", "Flags1", "Flags2", "Offset", "Size", "Enc");

        file.seekg(header.pkg_table_entry_offset);
        for (uint32_t i = 0; i < header.pkg_table_entry_count && i < 500; i++) {
            PkgEntry entry{};
            file.read(reinterpret_cast<char*>(&entry), sizeof(entry));
            if (!file) break;

            bool entry_encrypted = (entry.flags1 & 0x1) != 0;
            std::printf("  %-4u 0x%08X 0x%08X 0x%010X 0x%010X %s\n",
                        entry.id, entry.flags1, entry.flags2,
                        entry.offset, entry.size,
                        entry_encrypted ? "YES" : "no");
        }
        if (header.pkg_table_entry_count > 500) {
            std::printf("  ... (%u more entries not shown)\n",
                        header.pkg_table_entry_count - 500);
        }
    }

    // Summary
    std::printf("\n=== Summary ===\n");
    std::printf("  Title:          %s\n", title_id);
    std::printf("  Type:           %s\n", GetContentTypeName(header.pkg_content_type));
    std::printf("  Encryption:     %s\n", encrypted ? "Retail (needs crypto)" : "FPKG (no crypto)");
    std::printf("  Files inside:   %u\n", header.pkg_file_count);
    std::printf("  PFS image:      %s\n", FormatSize(header.pfs_image_size).c_str());
    std::printf("  Total PKG:      %s\n", FormatSize(header.pkg_size).c_str());
    std::printf("\n  %s\n", encrypted
        ? "This is a retail PKG. AbdoPS5 cannot decrypt it yet."
        : "This is an FPKG. AbdoPS5 can extract and mount it with --mount-pkg.");

    return 0;
}
