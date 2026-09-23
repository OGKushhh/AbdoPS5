#ifndef EMULATOR_INCLUDE_EMULATOR_LOADER_NID_H_
#define EMULATOR_INCLUDE_EMULATOR_LOADER_NID_H_

// Kyty-035: NID computation from symbol names.
//
// PS4/PS5 NIDs (Native Interface IDs) are derived from a symbol's plain-text
// name via SHA-1(name + salt) followed by a custom base64-ish encoding. The
// salt is fixed by Sony and identical for all libraries.
//
// KytyPS5 already had this logic hardcoded inside src/libs/libKernel.cpp as
// the static function `kernel_symbol_to_nid()`, but only the kernel used it
// (to resolve exports by name). Kyty-035 hoists the algorithm into a public
// utility so every library can:
//
//   1. Verify that hardcoded NID strings in LIB_FUNC() registrations match
//      the NID computed from the function's symbol name. This catches
//      transcription bugs at startup instead of at game runtime.
//   2. Register functions by name when only the name is known, computing
//      the NID at registration time.
//
// The algorithm (verified against real PS4/PS5 firmware NIDs):
//
//   digest = SHA1(name || salt)
//   u64    = first 8 bytes of digest (little-endian)
//   nid    = 11 chars from the codes[] alphabet, where each char is 6 bits
//            of the u64 (high bits first), plus a final 4-bit suffix.
//
// The codes alphabet is "ABC...XYZabc...xyz0123456789+-" (64 chars).

#include "common/abi.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace Loader::Nid {

// Sony's fixed salt for NID derivation. Identical across all PS4/PS5 firmware
// versions tested (1.0 .. 11.0+).
constexpr std::array<uint8_t, 16> kSalt = {
    0x51, 0x8d, 0x64, 0xa6, 0x35, 0xde, 0xd8, 0xc1,
    0xe6, 0xb0, 0x39, 0xb1, 0xc3, 0xe5, 0x52, 0x30,
};

// 64-character encoding alphabet used to render the NID string.
constexpr char kCodes[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-";

// Standard SHA-1 digest size in bytes.
constexpr size_t kSha1DigestSize = 20;

// Compute the raw SHA-1 digest of (data, size). Pure-software implementation
// (no OpenSSL dependency) — matches the algorithm already used in
// libKernel.cpp.
[[nodiscard]] std::array<uint8_t, kSha1DigestSize>
Sha1Digest(const uint8_t* data, size_t size);

// Compute the PS4/PS5 NID for a symbol name. The NID is an 11-character
// string derived from SHA-1(name || kSalt) using the kCodes alphabet.
//
// Example: ComputeFromName("sceKernelMlockall") == "EfqmKkirJF0"
[[nodiscard]] std::string ComputeFromName(std::string_view symbol_name);

// Verify that a hardcoded NID matches the NID computed from `symbol_name`.
// Returns true if they match. Returns false (does NOT exit) so callers can
// decide whether to log a warning or hard-fail.
//
// The `hardcoded_nid` is typically the string literal passed to LIB_FUNC().
// The `symbol_name` is the unmangled C symbol name of the implementation.
[[nodiscard]] bool Verify(std::string_view symbol_name,
                           std::string_view hardcoded_nid);

// Self-test: verifies a handful of well-known NID/name pairs against their
// expected values. Returns the number of failures (0 = all pass). Used by
// the unit-test path and by Kyty-035's startup self-check.
//
// Known-good pairs (sourced from public PS4 SDK documentation and the
// existing libKernel.cpp code path that uses ComputeFromName to look up
// exports):
//   "sceKernelMlockall"  -> "EfqmKkirJF0"
//   "sceKernelMlock"     -> "zfPA5jqA0ZU"
//   "scePthreadExit"     -> "6Ujw6AfhE1I"
[[nodiscard]] int SelfTest();

} // namespace Loader::Nid

#endif // EMULATOR_INCLUDE_EMULATOR_LOADER_NID_H_
