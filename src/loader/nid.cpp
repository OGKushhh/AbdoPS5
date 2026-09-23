// Kyty-035: NID computation from symbol names — implementation.
//
// See nid.h for design rationale. This file is a near-verbatim hoist of
// the sha1_digest() + kernel_symbol_to_nid() helpers that previously lived
// in src/libs/libKernel.cpp, so that all libraries can use them.

#include "loader/nid.h"

#include "common/assert.h"

#include <algorithm>
#include <cstdio>

namespace Loader::Nid {

std::array<uint8_t, kSha1DigestSize> Sha1Digest(const uint8_t* data, size_t size) {
        uint32_t h0 = 0x67452301u;
        uint32_t h1 = 0xefcdab89u;
        uint32_t h2 = 0x98badcfeu;
        uint32_t h3 = 0x10325476u;
        uint32_t h4 = 0xc3d2e1f0u;

        std::vector<uint8_t> msg(data, data + size);
        const uint64_t       bit_len = static_cast<uint64_t>(size) * 8u;

        msg.push_back(0x80u);
        while ((msg.size() % 64u) != 56u) {
                msg.push_back(0u);
        }
        for (int i = 7; i >= 0; i--) {
                msg.push_back(static_cast<uint8_t>((bit_len >> (i * 8)) & 0xffu));
        }

        for (size_t offset = 0; offset < msg.size(); offset += 64u) {
                uint32_t w[80] = {};
                for (int i = 0; i < 16; i++) {
                        const auto j = offset + static_cast<size_t>(i) * 4u;
                        w[i] = (static_cast<uint32_t>(msg[j + 0]) << 24u) |
                               (static_cast<uint32_t>(msg[j + 1]) << 16u) |
                               (static_cast<uint32_t>(msg[j + 2]) << 8u) |
                               static_cast<uint32_t>(msg[j + 3]);
                }
                for (int i = 16; i < 80; i++) {
                        w[i] = std::rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
                }

                uint32_t a = h0;
                uint32_t b = h1;
                uint32_t c = h2;
                uint32_t d = h3;
                uint32_t e = h4;

                for (int i = 0; i < 80; i++) {
                        uint32_t f = 0;
                        uint32_t k = 0;
                        if (i < 20) {
                                f = (b & c) | ((~b) & d);
                                k = 0x5a827999u;
                        } else if (i < 40) {
                                f = b ^ c ^ d;
                                k = 0x6ed9eba1u;
                        } else if (i < 60) {
                                f = (b & c) | (b & d) | (c & d);
                                k = 0x8f1bbcdcu;
                        } else {
                                f = b ^ c ^ d;
                                k = 0xca62c1d6u;
                        }

                        const auto temp = std::rotl(a, 5) + f + e + k + w[i];
                        e               = d;
                        d               = c;
                        c               = std::rotl(b, 30);
                        b               = a;
                        a               = temp;
                }

                h0 += a;
                h1 += b;
                h2 += c;
                h3 += d;
                h4 += e;
        }

        std::array<uint8_t, kSha1DigestSize> digest = {};
        const uint32_t                       h[5]   = {h0, h1, h2, h3, h4};
        for (int i = 0; i < 5; i++) {
                digest[static_cast<size_t>(i) * 4u + 0u] = static_cast<uint8_t>((h[i] >> 24u) & 0xffu);
                digest[static_cast<size_t>(i) * 4u + 1u] = static_cast<uint8_t>((h[i] >> 16u) & 0xffu);
                digest[static_cast<size_t>(i) * 4u + 2u] = static_cast<uint8_t>((h[i] >> 8u) & 0xffu);
                digest[static_cast<size_t>(i) * 4u + 3u] = static_cast<uint8_t>(h[i] & 0xffu);
        }
        return digest;
}

std::string ComputeFromName(std::string_view symbol_name) {
        std::vector<uint8_t> input(symbol_name.size() + kSalt.size());
        std::memcpy(input.data(), symbol_name.data(), symbol_name.size());
        std::memcpy(input.data() + symbol_name.size(), kSalt.data(), kSalt.size());

        const auto hash = Sha1Digest(input.data(), input.size());

        uint64_t digest = 0;
        std::memcpy(&digest, hash.data(), sizeof(digest));

        std::string nid(11, '\0');
        for (int i = 0; i < 10; i++) {
                nid[static_cast<size_t>(i)] = kCodes[(digest >> (58 - i * 6)) & 0x3fu];
        }
        nid[10] = kCodes[(digest & 0xfu) * 4u];
        return nid;
}

bool Verify(std::string_view symbol_name, std::string_view hardcoded_nid) {
        const auto computed = ComputeFromName(symbol_name);
        return computed == hardcoded_nid;
}

int SelfTest() {
        // Known NID/name pairs. These come from public PS4 SDK docs and
        // the existing libKernel.cpp code path that uses the same algorithm.
        // Adding a few entries from different libraries (kernel, videoOut,
        // pad) ensures the algorithm is correct across the board.
        struct TestCase {
                const char* name;
                const char* expected_nid;
        };
        static constexpr TestCase kCases[] = {
                // From src/core/libraries/kernel/memory.cpp (Kyty-003):
                //   sceKernelMlockall -> "EfqmKkirJF0"
                // (already registered in aerolib.inl with that NID)
                {"sceKernelMlockall", "EfqmKkirJF0"},

                // From src/libs/libVideoOut.cpp:15:
                //   LIB_FUNC("Up36PTk687E", VideoOut::VideoOutOpen)
                // Sony's SDK lists this NID against the symbol name
                // "sceVideoOutOpen" (the C symbol).
                {"sceVideoOutOpen", "Up36PTk687E"},

                // From src/libs/libPad.cpp:199:
                //   LIB_FUNC("xk0AcarP3V4", Controller::PadOpen)
                // Sony's SDK lists this NID against the symbol name
                // "scePadOpen".
                {"scePadOpen", "xk0AcarP3V4"},

                // From src/libs/libAudio.cpp:124:
                //   LIB_FUNC("JfEPXVxhFqA", AudioOut::AudioOutInit)
                // Sony's SDK lists this NID against the symbol name
                // "sceAudioOutInit".
                {"sceAudioOutInit", "JfEPXVxhFqA"},
        };

        int failures = 0;
        for (const auto& tc : kCases) {
                const auto computed = ComputeFromName(tc.name);
                if (computed != tc.expected_nid) {
                        std::fprintf(stderr,
                                     "[Kyty-035 Nid::SelfTest] FAIL name=%s "
                                     "expected=%s computed=%s\n",
                                     tc.name, tc.expected_nid, computed.c_str());
                        ++failures;
                }
        }
        return failures;
}

} // namespace Loader::Nid
