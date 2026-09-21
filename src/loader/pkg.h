#ifndef KYTY_LOADER_PKG_H_
#define KYTY_LOADER_PKG_H_

#include "common/common.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Loader {

// Kyty-005: PS5 PKG file format parser.
//
// The PS5 PKG format is nearly identical to PS4's — same header structure,
// same entry table format, same PFS image. The main differences are:
// - Different content types (PS5-specific game/app/theme IDs)
// - Different crypto keys (PS5 RSA/AES keys, not PS4)
// - Different PPSA title ID format (vs PS4's CUSA)
//
// This parser supports FPKG (fake PKG) files, which don't require crypto
// decryption. Retail PKG files require the crypto layer (future work).
//
// Based on the shadPS4 Shadlix fork's PKG implementation, adapted for
// KytyPS5's codebase and PS5 specifics.

#pragma pack(push, 1)

struct PkgHeader {
        uint32_t magic;             // 0x7F504B47 ("PKG\x7F")
        uint32_t pkg_type;          // Package type
        uint32_t pkg_0x8;           // Unknown field
        uint32_t pkg_file_count;    // Number of files in the PKG
        uint32_t pkg_table_entry_count; // Number of entries in the table
        uint16_t pkg_sc_entry_count;
        uint16_t pkg_table_entry_count_2;
        uint32_t pkg_table_entry_offset; // File table offset
        uint32_t pkg_sc_entry_data_size;
        uint64_t pkg_body_offset;   // Offset of PKG entries
        uint64_t pkg_body_size;     // Length of all PKG entries
        uint64_t pkg_content_offset;
        uint64_t pkg_content_size;
        char     pkg_content_id[0x24]; // 36-byte content ID string
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

        // Digest table
        uint8_t  digest_entries1[0x20];
        uint8_t  digest_entries2[0x20];
        uint8_t  digest_table_digest[0x20];
        uint8_t  digest_body_digest[0x20];

        uint8_t  pkg_zeroes_2[0x280];

        uint32_t pkg_0x400;

        // PFS image info
        uint32_t pfs_image_count;
        uint64_t pfs_image_flags;
        uint64_t pfs_image_offset; // Offset to start of external PFS image
        uint64_t pfs_image_size;   // Size of external PFS image
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
static_assert(sizeof(PkgHeader) == 0x1000, "PkgHeader must be 4096 bytes");

struct PkgEntry {
        uint32_t id;                // File ID
        uint32_t filename_offset;   // Offset into the filenames table
        uint32_t flags1;            // Flags (encrypted, etc.)
        uint32_t flags2;            // Flags (key index, etc.)
        uint32_t offset;            // Offset into PKG
        uint32_t size;              // File size
        uint64_t padding;
};
static_assert(sizeof(PkgEntry) == 32, "PkgEntry must be 32 bytes");

#pragma pack(pop)

// PKG content flags
enum class PkgContentFlag : uint32_t {
        FirstPatch       = 0x100000,
        PatchGo          = 0x200000,
        Remaster         = 0x400000,
        PsCloud          = 0x800000,
        GdAc             = 0x2000000,
        NonGame          = 0x4000000,
        Unknown0x8000000 = 0x8000000,
        SubsequentPatch  = 0x40000000,
        DeltaPatch       = 0x41000000,
        CumulativePatch  = 0x60000000,
};

// PKG magic number
constexpr uint32_t PKG_MAGIC = 0x7F504B47;

class Pkg {
public:
        Pkg();
        ~Pkg();

        // Open a PKG file and parse the header.
        // Returns true on success, false on failure (with reason in failreason).
        bool Open(const std::filesystem::path& filepath, std::string& failreason);

        // Extract the PFS image from the PKG to the given directory.
        // The PFS image contains the game's file system (eboot.bin, sce_sys/, etc.).
        // Returns true on success.
        bool ExtractPfs(const std::filesystem::path& extract_dir, std::string& failreason);

        // Extract all files from the PKG to the given directory.
        // For FPKG files (no encryption), this extracts the raw files.
        // For retail PKG files, this requires crypto decryption (not yet implemented).
        bool ExtractAll(const std::filesystem::path& extract_dir, std::string& failreason);

        // Get the title ID from the PKG (e.g., "PPSA01491")
        std::string_view GetTitleId() const { return std::string_view(m_title_id); }

        // Get the content ID (e.g., "PPSA01491_00-XXXX...")
        std::string_view GetContentId() const { return std::string_view(m_content_id); }

        // Get the PKG flags as a human-readable string
        std::string GetFlagsString() const;

        // Get the number of files in the PKG
        uint32_t GetFileCount() const { return m_header.pkg_file_count; }

        // Get the total PKG size
        uint64_t GetPkgSize() const { return m_header.pkg_size; }

        // Get the content type
        uint32_t GetContentType() const { return m_header.pkg_content_type; }

        // Check if the PKG is encrypted (retail) or unencrypted (FPKG)
        bool IsEncrypted() const { return m_encrypted; }

        // The file path that was passed to Open(). Useful for the launcher
        // (to log it) and for extract methods (to re-open the file).
        [[nodiscard]] const std::filesystem::path& GetFilePath() const { return m_file_path; }

private:
        bool ParseHeader(const std::vector<uint8_t>& data);
        bool ParseEntries();
        bool ExtractEntry(const PkgEntry& entry, const std::filesystem::path& extract_dir,
                          const std::vector<uint8_t>& pkg_data);

        PkgHeader              m_header{};
        std::vector<PkgEntry>  m_entries;
        std::vector<uint8_t>   m_title_id_data; // param.sfo data
        char                   m_title_id[10]{};  // "PPSA00000"
        char                   m_content_id[0x24]{};
        bool                   m_encrypted = false;
        uint64_t               m_file_size = 0;
        // File path is kept so ExtractPfs / ExtractAll can re-open the PKG
        // file (we don't keep a stream open between calls — the original
        // design didn't store this, making extract impossible).
        std::filesystem::path  m_file_path;
};

// Get a human-readable name for a PKG content type
std::string_view GetPkgContentTypeName(uint32_t content_type);

// Check if a file is a PKG file (by checking the magic number)
bool IsPkgFile(const std::filesystem::path& path);

} // namespace Loader

#endif // KYTY_LOADER_PKG_H_
