#include "loader/x64InstructionEmulator.h"

#include "common/common.h"
#include "common/logging/log.h"
#include "graphics/host_gpu/mmioDispatcher.h"

#include <Zydis/Zydis.h>
#include <bit>
#include <cstring>
#if !defined(__APPLE__)
#include <emmintrin.h>
#include <xmmintrin.h>
#endif

#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
#include <windows.h> // IWYU pragma: keep
#elif defined(__APPLE__)
#include <sys/ucontext.h>
#else
#include <sched.h>
#include <ucontext.h>
#endif

namespace Loader::X64InstructionEmulator {

static uint64_t ExtractBitField(uint64_t value, uint32_t length, uint32_t index) {
        length &= 0x3fu;
        index &= 0x3fu;

        if (length == 0) {
                length = 64;
        }

        if (index >= 64) {
                return 0;
        }

        auto available = 64u - index;
        if (length > available) {
                length = available;
        }

        const uint64_t mask = (length == 64 ? UINT64_MAX : ((uint64_t {1} << length) - 1u));
        return (value >> index) & mask;
}

static uint64_t InsertBitField(uint64_t dst, uint64_t src, uint32_t length, uint32_t index) {
        length &= 0x3fu;
        index &= 0x3fu;

        if (length == 0) {
                length = 64;
        }

        if (index >= 64) {
                return dst;
        }

        auto available = 64u - index;
        if (length > available) {
                length = available;
        }

        const uint64_t mask        = (length == 64 ? UINT64_MAX : ((uint64_t {1} << length) - 1u));
        const uint64_t shifted     = (index == 0 ? mask : (mask << index));
        const uint64_t src_shifted = (src & mask) << index;

        return (dst & ~shifted) | src_shifted;
}

struct XmmWords {
        uint32_t w[4];
};

static void Sha1Msg1(XmmWords& dest, const XmmWords& src2) {
        const uint32_t w0 = dest.w[3];
        const uint32_t w1 = dest.w[2];
        const uint32_t w2 = dest.w[1];
        const uint32_t w3 = dest.w[0];
        const uint32_t w4 = src2.w[3];
        const uint32_t w5 = src2.w[2];
        dest.w[3]         = w2 ^ w0;
        dest.w[2]         = w3 ^ w1;
        dest.w[1]         = w4 ^ w2;
        dest.w[0]         = w5 ^ w3;
}

static void Sha1Msg2(XmmWords& dest, const XmmWords& src2) {
        const uint32_t w13 = src2.w[2];
        const uint32_t w14 = src2.w[1];
        const uint32_t w15 = src2.w[0];
        const uint32_t w16 = std::rotl(dest.w[3] ^ w13, 1);
        const uint32_t w17 = std::rotl(dest.w[2] ^ w14, 1);
        const uint32_t w18 = std::rotl(dest.w[1] ^ w15, 1);
        const uint32_t w19 = std::rotl(dest.w[0] ^ w16, 1);
        dest.w[3]          = w16;
        dest.w[2]          = w17;
        dest.w[1]          = w18;
        dest.w[0]          = w19;
}

static void Sha1Nexte(XmmWords& dest, const XmmWords& src2) {
        const uint32_t tmp = std::rotl(dest.w[3], 30);
        dest.w[3]          = src2.w[3] + tmp;
        dest.w[2]          = src2.w[2];
        dest.w[1]          = src2.w[1];
        dest.w[0]          = src2.w[0];
}

static uint32_t Sha1RoundFunc(uint8_t group, uint32_t b, uint32_t c, uint32_t d) {
        switch (group & 3u) {
                case 0: return (b & c) ^ ((~b) & d);
                case 1: return b ^ c ^ d;
                case 2: return (b & c) ^ (b & d) ^ (c & d);
                default: return b ^ c ^ d;
        }
}

static uint32_t Sha1RoundConstant(uint8_t group) {
        switch (group & 3u) {
                case 0: return 0x5a827999u;
                case 1: return 0x6ed9eba1u;
                case 2: return 0x8f1bbcdcu;
                default: return 0xca62c1d6u;
        }
}

static void Sha1Rnds4(XmmWords& dest, const XmmWords& src2, uint8_t imm8) {
        const uint8_t  group = imm8 & 3u;
        const uint32_t k     = Sha1RoundConstant(group);
        const uint32_t w[4]  = {src2.w[3], src2.w[2], src2.w[1], src2.w[0]};

        uint32_t a = dest.w[3];
        uint32_t b = dest.w[2];
        uint32_t c = dest.w[1];
        uint32_t d = dest.w[0];
        uint32_t e = 0;

        for (unsigned int round = 0; round < 4u; round++) {
                uint32_t term = Sha1RoundFunc(group, b, c, d) + std::rotl(a, 5) + w[round] + k;
                if (round > 0u) {
                        term += e;
                }
                const uint32_t a1 = term;
                e                 = d;
                d                 = c;
                c                 = std::rotl(b, 30);
                b                 = a;
                a                 = a1;
        }

        dest.w[3] = a;
        dest.w[2] = b;
        dest.w[1] = c;
        dest.w[0] = d;
}

static uint32_t Sha256Sigma0(uint32_t x) {
        return std::rotr(x, 7) ^ std::rotr(x, 18) ^ (x >> 3u);
}

static uint32_t Sha256Sigma1(uint32_t x) {
        return std::rotr(x, 17) ^ std::rotr(x, 19) ^ (x >> 10u);
}

static uint32_t Sha256Sum0(uint32_t x) {
        return std::rotr(x, 2) ^ std::rotr(x, 13) ^ std::rotr(x, 22);
}

static uint32_t Sha256Sum1(uint32_t x) {
        return std::rotr(x, 6) ^ std::rotr(x, 11) ^ std::rotr(x, 25);
}

static uint32_t Sha256Ch(uint32_t e, uint32_t f, uint32_t g) {
        return (e & f) ^ ((~e) & g);
}

static uint32_t Sha256Maj(uint32_t a, uint32_t b, uint32_t c) {
        return (a & b) ^ (a & c) ^ (b & c);
}

static void Sha256Msg1(XmmWords& dest, const XmmWords& src2) {
        const uint32_t w4 = src2.w[0];
        const uint32_t w3 = dest.w[3];
        const uint32_t w2 = dest.w[2];
        const uint32_t w1 = dest.w[1];
        const uint32_t w0 = dest.w[0];
        dest.w[3]         = w3 + Sha256Sigma0(w4);
        dest.w[2]         = w2 + Sha256Sigma0(w3);
        dest.w[1]         = w1 + Sha256Sigma0(w2);
        dest.w[0]         = w0 + Sha256Sigma0(w1);
}

static void Sha256Msg2(XmmWords& dest, const XmmWords& src2) {
        const uint32_t w14 = src2.w[2];
        const uint32_t w15 = src2.w[3];
        const uint32_t w16 = dest.w[0] + Sha256Sigma1(w14);
        const uint32_t w17 = dest.w[1] + Sha256Sigma1(w15);
        const uint32_t w18 = dest.w[2] + Sha256Sigma1(w16);
        const uint32_t w19 = dest.w[3] + Sha256Sigma1(w17);
        dest.w[3]          = w19;
        dest.w[2]          = w18;
        dest.w[1]          = w17;
        dest.w[0]          = w16;
}

static void Sha256Rnds2(XmmWords& dest, const XmmWords& src2, const XmmWords& xmm0) {
        uint32_t a = src2.w[3];
        uint32_t b = src2.w[2];
        uint32_t c = dest.w[3];
        uint32_t d = dest.w[2];
        uint32_t e = src2.w[1];
        uint32_t f = src2.w[0];
        uint32_t g = dest.w[1];
        uint32_t h = dest.w[0];

        for (unsigned int round = 0; round < 2u; round++) {
                const uint32_t wk = xmm0.w[round];
                const uint32_t t1 = Sha256Ch(e, f, g) + Sha256Sum1(e) + wk + h;
                const uint32_t t2 = Sha256Maj(a, b, c) + Sha256Sum0(a);
                const uint32_t a1 = t1 + t2;
                const uint32_t e1 = t1 + d;
                const uint32_t b1 = a;
                const uint32_t c1 = b;
                const uint32_t d1 = c;
                const uint32_t f1 = e;
                const uint32_t g1 = f;
                const uint32_t h1 = g;
                a                 = a1;
                b                 = b1;
                c                 = c1;
                d                 = d1;
                e                 = e1;
                f                 = f1;
                g                 = g1;
                h                 = h1;
        }

        dest.w[3] = a;
        dest.w[2] = b;
        dest.w[1] = e;
        dest.w[0] = f;
}

struct ShaNiInsn {
        uint8_t escape;
        uint8_t opcode;
        uint8_t imm8;
        uint8_t rex;
        size_t  modrm_offset;
        size_t  length;
};

static bool DecodeShaNiInsn(const uint8_t* rip, ShaNiInsn& insn) {
        size_t  offset = 0;
        uint8_t rex    = 0;
        if ((rip[0] & 0xf0u) == 0x40u) {
                rex    = rip[0];
                offset = 1;
        }

        if (rip[offset] != 0x0f) {
                return false;
        }

        if (rip[offset + 1] == 0x38) {
                const uint8_t op = rip[offset + 2];
                if (op != 0xc8 && op != 0xc9 && op != 0xca && op != 0xcb && op != 0xcc && op != 0xcd) {
                        return false;
                }
                insn.escape       = 0x38;
                insn.opcode       = op;
                insn.imm8         = 0;
                insn.rex          = rex;
                insn.modrm_offset = offset + 3;
        } else if (rip[offset + 1] == 0x3a && rip[offset + 2] == 0xcc) {
                insn.escape       = 0x3a;
                insn.opcode       = 0xcc;
                insn.rex          = rex;
                insn.modrm_offset = offset + 3;
        } else {
                return false;
        }

        const uint8_t modrm = rip[insn.modrm_offset];
        const uint8_t mod   = modrm >> 6u;
        const uint8_t rm    = modrm & 0x07u;
        size_t        end   = insn.modrm_offset + 1;

        if (mod != 3u) {
                uint8_t sib_base = 0xffu;
                if (rm == 4u) {
                        sib_base = rip[end] & 0x07u;
                        end++;
                }

                if (mod == 0u && (rm == 5u || (rm == 4u && sib_base == 5u))) {
                        end += 4;
                } else if (mod == 1u) {
                        end++;
                } else if (mod == 2u) {
                        end += 4;
                }
        }

        if (insn.escape == 0x3a) {
                insn.imm8 = rip[end];
                end++;
        }

        insn.length = end;
        return true;
}

static bool ShaNiModrmIsRegister(uint8_t modrm) {
        return (modrm & 0xc0u) == 0xc0u;
}

static uint8_t ShaNiRegIndex(uint8_t modrm, uint8_t rex, bool reg_field) {
        if (reg_field) {
                return ((modrm >> 3u) & 0x07u) | ((rex & 0x04u) << 1u);
        }
        return (modrm & 0x07u) | ((rex & 0x01u) << 3u);
}

static bool ResolveShaNiMemoryAddress(const uint8_t* rip, const ShaNiInsn&    insn,
                                      const uint64_t (&gpr)[16], const void*& address) {
        const uint8_t modrm = rip[insn.modrm_offset];
        const uint8_t mod   = modrm >> 6u;
        const uint8_t rm    = modrm & 0x07u;
        if (mod == 3u) {
                return false;
        }

        size_t   offset = insn.modrm_offset + 1;
        uint64_t result = 0;

        if (rm == 4u) {
                const uint8_t sib       = rip[offset++];
                const uint8_t scale     = sib >> 6u;
                const uint8_t index_low = (sib >> 3u) & 0x07u;
                const uint8_t base_low  = sib & 0x07u;
                const bool    has_index = index_low != 4u || (insn.rex & 0x02u) != 0;
                const bool    has_base  = mod != 0u || base_low != 5u;

                if (has_base) {
                        const uint8_t base = base_low | ((insn.rex & 0x01u) << 3u);
                        result += gpr[base];
                }
                if (has_index) {
                        const uint8_t index = index_low | ((insn.rex & 0x02u) << 2u);
                        result += gpr[index] << scale;
                }

                if (!has_base) {
                        int32_t displacement = 0;
                        std::memcpy(&displacement, rip + offset, sizeof(displacement));
                        result += static_cast<uint64_t>(static_cast<int64_t>(displacement));
                        offset += sizeof(displacement);
                }
        } else if (mod == 0u && rm == 5u) {
                int32_t displacement = 0;
                std::memcpy(&displacement, rip + offset, sizeof(displacement));
                result = reinterpret_cast<uint64_t>(rip + insn.length) +
                         static_cast<uint64_t>(static_cast<int64_t>(displacement));
                offset += sizeof(displacement);
        } else {
                const uint8_t base = rm | ((insn.rex & 0x01u) << 3u);
                result             = gpr[base];
        }

        if (mod == 1u) {
                const auto displacement = static_cast<int8_t>(rip[offset]);
                result += static_cast<uint64_t>(static_cast<int64_t>(displacement));
        } else if (mod == 2u) {
                int32_t displacement = 0;
                std::memcpy(&displacement, rip + offset, sizeof(displacement));
                result += static_cast<uint64_t>(static_cast<int64_t>(displacement));
        }

        address = reinterpret_cast<const void*>(result);
        return true;
}

static bool ExecuteShaNiInsn(const ShaNiInsn& insn, const XmmWords& src2, const XmmWords& xmm0,
                             XmmWords& dest) {
        if (insn.escape == 0x3a && insn.opcode == 0xcc) {
                Sha1Rnds4(dest, src2, insn.imm8);
                return true;
        }

        switch (insn.opcode) {
                case 0xc8: Sha1Nexte(dest, src2); return true;
                case 0xc9: Sha1Msg1(dest, src2); return true;
                case 0xca: Sha1Msg2(dest, src2); return true;
                case 0xcb: Sha256Rnds2(dest, src2, xmm0); return true;
                case 0xcc: Sha256Msg1(dest, src2); return true;
                case 0xcd: Sha256Msg2(dest, src2); return true;
                default: return false;
        }
}

// Keep instruction semantics shared; only access to the saved host context differs.
struct Context {
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
        PCONTEXT native;

        [[nodiscard]] uint64_t Rip() const { return native->Rip; }
        void                   Advance(size_t length) { native->Rip += length; }
        [[nodiscard]] void*    Xmm(uint8_t index) const { return &native->Xmm0 + index; }

        void LoadGprs(uint64_t (&gpr)[16]) const {
                const uint64_t registers[] = {native->Rax, native->Rcx, native->Rdx, native->Rbx,
                                              native->Rsp, native->Rbp, native->Rsi, native->Rdi,
                                              native->R8,  native->R9,  native->R10, native->R11,
                                              native->R12, native->R13, native->R14, native->R15};
                std::memcpy(gpr, registers, sizeof(gpr));
        }

        void ClearUpperYmm(uint8_t index) const {
                if ((native->ContextFlags & CONTEXT_XSTATE) != CONTEXT_XSTATE) {
                        return;
                }
                DWORD64 features = 0;
                if (!GetXStateFeaturesMask(native, &features) || (features & XSTATE_MASK_AVX) == 0) {
                        return; // An absent AVX component restores zeroes.
                }
                DWORD size = 0;
                auto* ymm = static_cast<M128A*>(LocateXStateFeature(native, XSTATE_AVX, &size));
                if (ymm != nullptr && size >= (index + 1u) * sizeof(M128A)) {
                        ymm[index] = {};
                }
        }
#elif defined(__APPLE__)
        ucontext_t* native;

        [[nodiscard]] uint64_t Rip() const {
                return static_cast<uint64_t>(native->uc_mcontext->__ss.__rip);
        }
        void Advance(size_t length) {
                native->uc_mcontext->__ss.__rip += static_cast<uint64_t>(length);
        }
        // Darwin names the XMM file __fpu_xmm0..__fpu_xmm15 instead of exposing an array.
        [[nodiscard]] void* Xmm(uint8_t index) const {
                auto* fs = &native->uc_mcontext->__fs;
                switch (index) {
                        case 0: return &fs->__fpu_xmm0;
                        case 1: return &fs->__fpu_xmm1;
                        case 2: return &fs->__fpu_xmm2;
                        case 3: return &fs->__fpu_xmm3;
                        case 4: return &fs->__fpu_xmm4;
                        case 5: return &fs->__fpu_xmm5;
                        case 6: return &fs->__fpu_xmm6;
                        case 7: return &fs->__fpu_xmm7;
                        case 8: return &fs->__fpu_xmm8;
                        case 9: return &fs->__fpu_xmm9;
                        case 10: return &fs->__fpu_xmm10;
                        case 11: return &fs->__fpu_xmm11;
                        case 12: return &fs->__fpu_xmm12;
                        case 13: return &fs->__fpu_xmm13;
                        case 14: return &fs->__fpu_xmm14;
                        case 15: return &fs->__fpu_xmm15;
                        default: return nullptr;
                }
        }
#else
        ucontext_t* native;

        [[nodiscard]] uint64_t Rip() const {
                return static_cast<uint64_t>(native->uc_mcontext.gregs[REG_RIP]);
        }
        void Advance(size_t length) {
                native->uc_mcontext.gregs[REG_RIP] += static_cast<greg_t>(length);
        }
        [[nodiscard]] void* Xmm(uint8_t index) const {
                if (native->uc_mcontext.fpregs == nullptr) {
                        return nullptr;
                }
                return native->uc_mcontext.fpregs->_xmm[index].element;
        }

        void LoadGprs(uint64_t (&gpr)[16]) const {
                constexpr int registers[] = {REG_RAX, REG_RCX, REG_RDX, REG_RBX, REG_RSP, REG_RBP,
                                             REG_RSI, REG_RDI, REG_R8,  REG_R9,  REG_R10, REG_R11,
                                             REG_R12, REG_R13, REG_R14, REG_R15};
                for (size_t i = 0; i < 16; ++i) {
                        gpr[i] = static_cast<uint64_t>(native->uc_mcontext.gregs[registers[i]]);
                }
        }

        void ClearUpperYmm(uint8_t index) const {
                // Linux signal frames use the standard XSAVE layout. An absent AVX component
                // already restores the architectural initial value (all zeroes).
                auto*    state    = reinterpret_cast<uint8_t*>(native->uc_mcontext.fpregs);
                uint32_t magic    = 0;
                uint32_t size     = 0;
                uint64_t features = 0;
                std::memcpy(&magic, state + 464, sizeof(magic));
                if (magic != 0x46505853) {
                        return;
                }
                std::memcpy(&size, state + 480, sizeof(size));
                if (size < 832) {
                        return;
                }
                std::memcpy(&features, state + 512, sizeof(features));
                if ((features & 4) != 0) {
                        std::memset(state + 576 + index * 16, 0, 16);
                }
        }
#endif
};

#if !defined(__APPLE__)

static bool TryEmulateShaNi(Context& context) {
        const auto* rip = reinterpret_cast<const uint8_t*>(context.Rip());
        ShaNiInsn   insn {};
        if (!DecodeShaNiInsn(rip, insn)) {
                return false;
        }

        const uint8_t modrm_byte = rip[insn.modrm_offset];
        const uint8_t dest_index = ShaNiRegIndex(modrm_byte, insn.rex, true);
        auto*         dest_xmm   = context.Xmm(dest_index);
        auto*         xmm0       = context.Xmm(0);
        if (dest_xmm == nullptr || xmm0 == nullptr) {
                return false;
        }

        XmmWords dest {};
        XmmWords src2 {};
        XmmWords xmm0_words {};
        std::memcpy(&dest, dest_xmm, sizeof(dest));
        std::memcpy(&xmm0_words, xmm0, sizeof(xmm0_words));

        if (ShaNiModrmIsRegister(modrm_byte)) {
                const uint8_t src_index = ShaNiRegIndex(modrm_byte, insn.rex, false);
                auto*         src_xmm   = context.Xmm(src_index);
                if (src_xmm == nullptr) {
                        return false;
                }
                std::memcpy(&src2, src_xmm, sizeof(src2));
        } else {
                uint64_t    gpr[16] {};
                const void* source = nullptr;
                context.LoadGprs(gpr);
                if (!ResolveShaNiMemoryAddress(rip, insn, gpr, source)) {
                        return false;
                }
                std::memcpy(&src2, source, sizeof(src2));
        }

        if (!ExecuteShaNiInsn(insn, src2, xmm0_words, dest)) {
                return false;
        }

        std::memcpy(dest_xmm, &dest, sizeof(dest));
        context.Advance(insn.length);
        return true;
}

#endif

static bool TryEmulateSse4a(Context& context) {
        const auto*   rip    = reinterpret_cast<const uint8_t*>(context.Rip());
        const uint8_t prefix = rip[0];
        if (prefix != 0x66 && prefix != 0xf2) {
                return false;
        }

        size_t  offset = 1;
        uint8_t rex    = 0;
        if ((rip[offset] & 0xf0u) == 0x40u) {
                rex = rip[offset++];
        }
        if (rip[offset] != 0x0f) {
                return false;
        }
        const bool register_extract = prefix == 0x66 && rip[offset + 1] == 0x79;
        if (rip[offset + 1] != 0x78 && !register_extract) {
                return false;
        }

        const uint8_t modrm = rip[offset + 2];
        if ((modrm & 0xc0u) != 0xc0u) {
                return false;
        }

        const uint8_t reg = ((modrm >> 3u) & 0x07u) | ((rex & 0x04u) << 1u);
        const uint8_t rm  = (modrm & 0x07u) | ((rex & 0x01u) << 3u);

        // Immediate EXTRQ encodes its destination in r/m; the two-register form uses reg.
        uint8_t dest_index = reg;
        if (prefix == 0x66 && !register_extract) {
                dest_index = rm;
        }
        auto* dest_xmm = context.Xmm(dest_index);
        auto* src_xmm  = context.Xmm(rm);
        if (dest_xmm == nullptr || src_xmm == nullptr) {
                return false;
        }
        uint64_t dest[2] {};
        uint64_t source = 0;
        std::memcpy(dest, dest_xmm, sizeof(dest));
        std::memcpy(&source, src_xmm, sizeof(source));
        uint8_t length             = 0;
        uint8_t index              = 0;
        size_t  instruction_length = offset + 3;
        if (register_extract) {
                length = static_cast<uint8_t>(source);
                index  = static_cast<uint8_t>(source >> 8u);
        } else {
                length = rip[offset + 3];
                index  = rip[offset + 4];
                instruction_length += 2;
        }
        if (prefix == 0x66) {
                dest[0] = ExtractBitField(dest[0], length, index);
                dest[1] = 0;
        } else {
                dest[0] = InsertBitField(dest[0], source, length, index);
        }
        std::memcpy(dest_xmm, dest, sizeof(dest));
        context.Advance(instruction_length);
        return true;
}

#if !defined(__APPLE__)

static bool TryEmulateMonitorxMwaitx(Context& context) {
        const auto* rip = reinterpret_cast<const uint8_t*>(context.Rip());
        if (rip[0] != 0x0f || rip[1] != 0x01 || (rip[2] != 0xfa && rip[2] != 0xfb)) {
                return false;
        }

        // Approximate AMD MONITORX/MWAITX as no-op/yield.
        if (rip[2] == 0xfb) {
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
                SwitchToThread();
#else
                ::sched_yield();
#endif
        }
        context.Advance(3);
        return true;
}

static uint32_t ReciprocalSquareRoot(uint32_t bits) {
        const uint32_t magnitude = bits & 0x7fffffffu;
        const uint32_t exponent  = magnitude & 0x7f800000u;
        if (exponent == 0) {
                // RSQRT treats denormals as signed zero regardless of MXCSR.DAZ.
                return (bits & 0x80000000u) | 0x7f800000u;
        }
        if (magnitude > 0x7f800000u) {
                return bits | 0x00400000u; // Quiet NaNs without raising an exception.
        }
        if ((bits & 0x80000000u) != 0) {
                return 0xffc00000u;
        }
        if (magnitude == 0x7f800000u) {
                return 0;
        }

        // A deterministic accurate estimate meets the instruction's relative-error
        // bound without relying on the host vendor's approximation table.
        const __m128d input  = _mm_set_sd(static_cast<double>(std::bit_cast<float>(bits)));
        const __m128d result = _mm_div_sd(_mm_set_sd(1.0), _mm_sqrt_sd(input, input));
        return std::bit_cast<uint32_t>(_mm_cvtss_f32(_mm_cvtsd_ss(_mm_setzero_ps(), result)));
}

static bool TryEmulateReciprocalSquareRoot(Context& context) {
        const auto* rip            = reinterpret_cast<const uint8_t*>(context.Rip());
        size_t      prefix_size    = 0;
        uint8_t     dest_extension = 0;
        uint8_t     src_extension  = 0;
        if (rip[0] == 0xc5 && (rip[1] & 0x7fu) == 0x70u) {
                prefix_size    = 2;
                dest_extension = (~rip[1] & 0x80u) >> 4u;
        } else if (rip[0] == 0xc4 && (rip[1] & 0x1fu) == 1 && (rip[2] & 0x7fu) == 0x70u) {
                prefix_size    = 3;
                dest_extension = (~rip[1] & 0x80u) >> 4u;
                src_extension  = (~rip[1] & 0x20u) >> 2u;
        } else {
                return false;
        }
        if (rip[prefix_size] != 0x52 || (rip[prefix_size + 1] & 0xc0u) != 0xc0u) {
                return false;
        }

        const uint8_t modrm    = rip[prefix_size + 1];
        const uint8_t dest     = ((modrm >> 3u) & 7u) | dest_extension;
        const uint8_t source   = (modrm & 7u) | src_extension;
        auto*         dest_xmm = context.Xmm(dest);
        auto*         src_xmm  = context.Xmm(source);
        if (dest_xmm == nullptr || src_xmm == nullptr) {
                return false;
        }
        XmmWords result {};
        std::memcpy(&result, src_xmm, sizeof(result));
        // RSQRT ignores the rounding mode and never changes guest exception flags.
        // Mask host exceptions while calculating, then restore the handler's state.
        const uint32_t mxcsr = _mm_getcsr();
        _mm_setcsr(0x1f80);
        for (auto& word: result.w) {
                word = ReciprocalSquareRoot(word);
        }
        _mm_setcsr(mxcsr);
        std::memcpy(dest_xmm, &result, sizeof(result));
        context.ClearUpperYmm(dest);
        context.Advance(prefix_size + 2);
        return true;
}

#endif

bool IsReciprocalSquareRoot(const ZydisDecodedInstruction& instruction,
                            const ZydisDecodedOperand* operands) {
        return instruction.mnemonic == ZYDIS_MNEMONIC_VRSQRTPS &&
               instruction.encoding == ZYDIS_INSTRUCTION_ENCODING_VEX &&
               instruction.raw.vex.offset == 0 && operands[0].size == 128 &&
               operands[1].type == ZYDIS_OPERAND_TYPE_REGISTER;
}

uint64_t PatchReciprocalSquareRoots(uint64_t address, uint64_t size) {
        uint64_t patched = 0;
#if !defined(__APPLE__)
        ZydisDecoder decoder {};
        if (!ZYAN_SUCCESS(
                ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64))) {
                return 0;
        }
        for (uint64_t offset = 0; offset < size;) {
                auto*                   code = reinterpret_cast<uint8_t*>(address + offset);
                ZydisDecodedInstruction instruction {};
                ZydisDecodedOperand     operands[ZYDIS_MAX_OPERAND_COUNT] {};
                if (!ZYAN_SUCCESS(
                        ZydisDecoderDecodeFull(&decoder, code, size - offset, &instruction, operands))) {
                        ++offset;
                        continue;
                }
                if (IsReciprocalSquareRoot(instruction, operands)) {
                        // vvvv is reserved (must be 1111b). Clear one bit to route this
                        // otherwise intact instruction through the illegal-instruction emulator.
                        code[instruction.raw.vex.size - 1] &= ~0x08u;
                        ++patched;
                }
                offset += instruction.length;
        }
#else
        (void)address;
        (void)size;
#endif
        return patched;
}

bool TryEmulate(void* native_context) {
        if (native_context == nullptr) {
                return false;
        }
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
        Context context {static_cast<PCONTEXT>(native_context)};
#elif defined(__APPLE__)
        auto* saved_context = static_cast<ucontext_t*>(native_context);
        if (saved_context->uc_mcontext == nullptr) {
                return false;
        }
        Context context {saved_context};
#else
        Context context {static_cast<ucontext_t*>(native_context)};
#endif
#if !defined(__APPLE__)
        if (TryEmulateReciprocalSquareRoot(context)) {
                return true;
        }
        return TryEmulateMonitorxMwaitx(context) || TryEmulateSse4a(context) ||
               TryEmulateShaNi(context);
#else
        return TryEmulateSse4a(context);
#endif
}

// ============================================================================
// Kyty-016: MMIO access handler
// ============================================================================

// Map a ZydisRegister to a GPR index (0-15) in the x86 encoding order:
// 0=RAX, 1=RCX, 2=RDX, 3=RBX, 4=RSP, 5=RBP, 6=RSI, 7=RDI, 8-15=R8-R15
// Returns -1 if the register is not a GPR.
static int ZydisRegToGprIndex(ZydisRegister reg) {
        // 64-bit GPRs
        if (reg >= ZYDIS_REGISTER_RAX && reg <= ZYDIS_REGISTER_R15) {
                return static_cast<int>(reg - ZYDIS_REGISTER_RAX);
        }
        // 32-bit GPRs
        if (reg >= ZYDIS_REGISTER_EAX && reg <= ZYDIS_REGISTER_R15D) {
                return static_cast<int>(reg - ZYDIS_REGISTER_EAX);
        }
        // 16-bit GPRs
        if (reg >= ZYDIS_REGISTER_AX && reg <= ZYDIS_REGISTER_R15W) {
                return static_cast<int>(reg - ZYDIS_REGISTER_AX);
        }
        // 8-bit GPRs (low byte)
        if (reg >= ZYDIS_REGISTER_AL && reg <= ZYDIS_REGISTER_R15B) {
                return static_cast<int>(reg - ZYDIS_REGISTER_AL);
        }
        return -1;
}

// Read a GPR from the native context (by index 0-15).
static uint64_t ReadGpr(const Context& context, int gpr_index) {
        uint64_t gpr[16] = {};
        context.LoadGprs(gpr);
        return gpr[gpr_index];
}

// Write a GPR in the native context (by index 0-15).
// For sub-register writes (32/16/8-bit), preserves the upper bits.
static void WriteGpr(Context& context, int gpr_index, uint64_t value, size_t size) {
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
        auto* ctx = context.native;
        switch (gpr_index) {
                case 0:  ctx->Rax = size == 8 ? value : (ctx->Rax & ~0xFFFFFFFFULL) | (value & 0xFFFFFFFF); break;
                case 1:  ctx->Rcx = size == 8 ? value : (ctx->Rcx & ~0xFFFFFFFFULL) | (value & 0xFFFFFFFF); break;
                case 2:  ctx->Rdx = size == 8 ? value : (ctx->Rdx & ~0xFFFFFFFFULL) | (value & 0xFFFFFFFF); break;
                case 3:  ctx->Rbx = size == 8 ? value : (ctx->Rbx & ~0xFFFFFFFFULL) | (value & 0xFFFFFFFF); break;
                case 4:  ctx->Rsp = size == 8 ? value : (ctx->Rsp & ~0xFFFFFFFFULL) | (value & 0xFFFFFFFF); break;
                case 5:  ctx->Rbp = size == 8 ? value : (ctx->Rbp & ~0xFFFFFFFFULL) | (value & 0xFFFFFFFF); break;
                case 6:  ctx->Rsi = size == 8 ? value : (ctx->Rsi & ~0xFFFFFFFFULL) | (value & 0xFFFFFFFF); break;
                case 7:  ctx->Rdi = size == 8 ? value : (ctx->Rdi & ~0xFFFFFFFFULL) | (value & 0xFFFFFFFF); break;
                case 8:  ctx->R8  = size == 8 ? value : (ctx->R8  & ~0xFFFFFFFFULL) | (value & 0xFFFFFFFF); break;
                case 9:  ctx->R9  = size == 8 ? value : (ctx->R9  & ~0xFFFFFFFFULL) | (value & 0xFFFFFFFF); break;
                case 10: ctx->R10 = size == 8 ? value : (ctx->R10 & ~0xFFFFFFFFULL) | (value & 0xFFFFFFFF); break;
                case 11: ctx->R11 = size == 8 ? value : (ctx->R11 & ~0xFFFFFFFFULL) | (value & 0xFFFFFFFF); break;
                case 12: ctx->R12 = size == 8 ? value : (ctx->R12 & ~0xFFFFFFFFULL) | (value & 0xFFFFFFFF); break;
                case 13: ctx->R13 = size == 8 ? value : (ctx->R13 & ~0xFFFFFFFFULL) | (value & 0xFFFFFFFF); break;
                case 14: ctx->R14 = size == 8 ? value : (ctx->R14 & ~0xFFFFFFFFULL) | (value & 0xFFFFFFFF); break;
                case 15: ctx->R15 = size == 8 ? value : (ctx->R15 & ~0xFFFFFFFFULL) | (value & 0xFFFFFFFF); break;
        }
#elif defined(__APPLE__)
        auto* regs = &context.native->uc_mcontext->__ss;
        auto write = [&](auto& dst) {
                dst = size == 8 ? value : (dst & ~0xFFFFFFFFULL) | (value & 0xFFFFFFFF);
        };
        switch (gpr_index) {
                case 0:  write(regs->__rax); break;
                case 1:  write(regs->__rcx); break;
                case 2:  write(regs->__rdx); break;
                case 3:  write(regs->__rbx); break;
                case 4:  write(regs->__rsp); break;
                case 5:  write(regs->__rbp); break;
                case 6:  write(regs->__rsi); break;
                case 7:  write(regs->__rdi); break;
                case 8:  write(regs->__r8);  break;
                case 9:  write(regs->__r9);  break;
                case 10: write(regs->__r10); break;
                case 11: write(regs->__r11); break;
                case 12: write(regs->__r12); break;
                case 13: write(regs->__r13); break;
                case 14: write(regs->__r14); break;
                case 15: write(regs->__r15); break;
        }
#else
        auto* regs = context.native->uc_mcontext.gregs;
        constexpr int reg_map[] = {REG_RAX, REG_RCX, REG_RDX, REG_RBX, REG_RSP, REG_RBP,
                                   REG_RSI, REG_RDI, REG_R8,  REG_R9,  REG_R10, REG_R11,
                                   REG_R12, REG_R13, REG_R14, REG_R15};
        auto& dst = regs[reg_map[gpr_index]];
        dst = size == 8 ? static_cast<greg_t>(value)
                        : static_cast<greg_t>((dst & ~0xFFFFFFFFULL) | (value & 0xFFFFFFFF));
#endif
}

bool TryHandleMmioAccess(void* native_context, uint64_t fault_addr, bool is_write) {
        if (native_context == nullptr) {
                return false;
        }

#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
        Context context {static_cast<PCONTEXT>(native_context)};
#elif defined(__APPLE__)
        auto* saved_context = static_cast<ucontext_t*>(native_context);
        if (saved_context->uc_mcontext == nullptr) {
                return false;
        }
        Context context {saved_context};
#else
        Context context {static_cast<ucontext_t*>(native_context)};
#endif

        // Heuristic: translate the faulting VA to a physical address.
        // On PS5, the DMAP (direct map) maps physical address X to virtual
        // address (DMAP_BASE + X). DMAP_BASE is 4GB-aligned (or higher),
        // so the lower 32 bits of the DMAP VA are the physical address.
        //
        // This works for MMIO addresses like 0xFDD80000 which are in the
        // 0xFDDxxxxx range — distinctive enough that they won't collide
        // with normal RAM accesses (which are 0x0-0x3FFFFFFFF).
        const uint64_t phys_addr = fault_addr & 0xFFFFFFFFULL;

        // Check if any MMIO handler covers this physical address.
        // Use a 1-byte probe first to see if the dispatcher has a handler.
        uint64_t dummy = 0;
        if (is_write) {
                // For writes, we need to check if the dispatcher would handle
                // this address. But DispatchWrite actually performs the write,
                // so we can't use it as a probe. Instead, check the registered
                // range directly via the dispatcher's DispatchRead (read is
                // side-effect-free on the IOMMU — unknown registers return 0).
                if (!Libs::Graphics::MmioDispatcher::Instance().DispatchRead(phys_addr, &dummy, 1)) {
                        return false; // not in any MMIO range
                }
        } else {
                if (!Libs::Graphics::MmioDispatcher::Instance().DispatchRead(phys_addr, &dummy, 1)) {
                        return false; // not in any MMIO range
                }
        }

        // The address IS in an MMIO range. Decode the faulting instruction
        // to determine the access size and register.
        ZydisDecoder decoder {};
        if (!ZYAN_SUCCESS(ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64))) {
                return false;
        }

        const uint64_t rip = context.Rip();
        const auto*    code = reinterpret_cast<const uint8_t*>(rip);
        // Read up to 15 bytes (max x86 instruction length).
        ZydisDecodedInstruction instruction {};
        ZydisDecodedOperand     operands[ZYDIS_MAX_OPERAND_COUNT] {};
        if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&decoder, code, 15, &instruction, operands))) {
                LOGF("MMIO: failed to decode instruction at 0x%llx (fault_addr=0x%llx, phys=0x%llx)\n",
                     static_cast<unsigned long long>(rip),
                     static_cast<unsigned long long>(fault_addr),
                     static_cast<unsigned long long>(phys_addr));
                return false;
        }

        // Find the memory operand and the register operand.
        // For MOV reg, [mem] (read): operand 0 = register, operand 1 = memory
        // For MOV [mem], reg (write): operand 0 = memory, operand 1 = register
        const ZydisDecodedOperand* mem_op = nullptr;
        const ZydisDecodedOperand* reg_op = nullptr;
        for (size_t i = 0; i < instruction.operand_count_visible; ++i) {
                if (operands[i].type == ZYDIS_OPERAND_TYPE_MEMORY) {
                        mem_op = &operands[i];
                } else if (operands[i].type == ZYDIS_OPERAND_TYPE_REGISTER) {
                        reg_op = &operands[i];
                }
        }

        if (mem_op == nullptr || reg_op == nullptr) {
                LOGF("MMIO: instruction at 0x%llx has no memory+register operand pair\n",
                     static_cast<unsigned long long>(rip));
                return false;
        }

        // Determine the access size in bytes.
        const size_t access_size = mem_op->size / 8; // size is in bits
        if (access_size == 0 || access_size > 8) {
                LOGF("MMIO: unsupported access size %zu bits at 0x%llx\n",
                     mem_op->size, static_cast<unsigned long long>(rip));
                return false;
        }

        // Get the GPR index for the register operand.
        const int gpr_index = ZydisRegToGprIndex(reg_op->reg.value);
        if (gpr_index < 0) {
                LOGF("MMIO: register operand is not a GPR at 0x%llx\n",
                     static_cast<unsigned long long>(rip));
                return false;
        }

        if (is_write) {
                // MOV [mem], reg — write the register value to MMIO.
                const uint64_t value = ReadGpr(context, gpr_index);
                LOGF("MMIO: write %zu bytes at phys 0x%llx (fault_va=0x%llx), value=0x%llx\n",
                     access_size,
                     static_cast<unsigned long long>(phys_addr),
                     static_cast<unsigned long long>(fault_addr),
                     static_cast<unsigned long long>(value));
                if (!Libs::Graphics::MmioDispatcher::Instance().DispatchWrite(phys_addr, &value, access_size)) {
                        LOGF("MMIO: dispatcher rejected write — falling through to fault\n");
                        return false;
                }
        } else {
                // MOV reg, [mem] — read from MMIO and write to register.
                uint64_t value = 0;
                LOGF("MMIO: read %zu bytes at phys 0x%llx (fault_va=0x%llx)\n",
                     access_size,
                     static_cast<unsigned long long>(phys_addr),
                     static_cast<unsigned long long>(fault_addr));
                if (!Libs::Graphics::MmioDispatcher::Instance().DispatchRead(phys_addr, &value, access_size)) {
                        LOGF("MMIO: dispatcher rejected read — falling through to fault\n");
                        return false;
                }
                WriteGpr(context, gpr_index, value, access_size);
        }

        // Advance RIP past the faulting instruction.
        context.Advance(instruction.length);
        return true;
}

} // namespace Loader::X64InstructionEmulator
