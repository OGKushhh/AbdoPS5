#include "common/assert.h"
#include "common/logging/log.h"
#include "graphics/shader/opcodeTracker.h"
#include "graphics/shader/recompiler/frontend/translate/Translator.h"
#include "loader/hack_features.h"

#include <magic_enum.hpp>
#include <fmt/format.h>

namespace Libs::Graphics::ShaderRecompiler::Frontend {

void Translator::TranslateInstruction(const Decoder::Instruction& inst) {
        current_opcode = inst.opcode;
        current_pc     = inst.pc;

        // Kyty-013: record every opcode we translate. Skip UNKNOWN /
        // UNSUPPORTED / COUNT here — they're recorded separately below
        // with RecordUnsupportedOpcode so we can cross-reference with
        // GitHub issue #281 reports.
        if (inst.opcode != Decoder::Opcode::UNKNOWN &&
            inst.opcode != Decoder::Opcode::UNSUPPORTED &&
            inst.opcode != Decoder::Opcode::COUNT) {
                ShaderOpcodeTracker::Instance().RecordOpcode(
                    std::string(magic_enum::enum_name(inst.opcode)));
        }

        switch (inst.opcode) {
                case Decoder::Opcode::UNKNOWN:
                case Decoder::Opcode::COUNT:
                        EXIT("decoded opcode has no IR translation at pc 0x%08x", inst.pc);
                case Decoder::Opcode::UNSUPPORTED:
                        // Kyty-013: record unsupported opcodes separately so we
                        // can cross-reference with GitHub issue #281 reports.
                        ShaderOpcodeTracker::Instance().RecordUnsupportedOpcode(
                            fmt::format("{} opcode=0x{:02x} reason={}",
                                        magic_enum::enum_name(inst.family),
                                        inst.opcode_id,
                                        inst.unsupported_reason));
                        // Kyty-003: SkipShaderAssert hack — log the unsupported opcode
                        // and skip it instead of aborting. This lets users boot games
                        // that hit a missing opcode (e.g. during Kyty-001 verification)
                        // with visual artifacts, rather than crashing.
                        if (Loader::HackFeatures::HasHack(Loader::GameHack::SkipShaderAssert)) {
                                LOGF("SkipShaderAssert: skipping unsupported opcode at pc 0x%08x: %s\n",
                                     inst.pc, Decoder::InstructionToString(inst).c_str());
                                return;
                        }
                        EXIT("unsupported decoded instruction: %s", Decoder::InstructionToString(inst).c_str());
                default: break;
        }

        bool translated = false;
        switch (inst.family) {
                case Decoder::Family::SOP1:
                case Decoder::Family::SOP2:
                case Decoder::Family::SOPK:
                case Decoder::Family::SOPC:
                case Decoder::Family::SOPP: translated = EmitScalar(inst); break;
                case Decoder::Family::VOP1:
                case Decoder::Family::VOP2:
                case Decoder::Family::VOP3:
                case Decoder::Family::VOP3P:
                case Decoder::Family::VOPC: translated = EmitVector(inst); break;
                case Decoder::Family::SMEM:
                case Decoder::Family::MUBUF:
                case Decoder::Family::MTBUF:
                case Decoder::Family::FLAT:
                case Decoder::Family::DS:
                case Decoder::Family::MIMG: translated = EmitMemory(inst); break;
                case Decoder::Family::VINTRP: translated = EmitInterpolation(inst); break;
                case Decoder::Family::EXP:
                        EXP(inst);
                        translated = true;
                        break;
                default: break;
        }

        if (!translated) {
                // Kyty-003: SkipShaderAssert hack — same as above, for opcodes that
                // have a dispatch entry but no IR translation.
                if (Loader::HackFeatures::HasHack(Loader::GameHack::SkipShaderAssert)) {
                        LOGF("SkipShaderAssert: skipping untranslated opcode at pc 0x%08x: %s\n",
                             inst.pc, Decoder::InstructionToString(inst).c_str());
                        return;
                }
                EXIT("opcode %s at pc 0x%08x has no IR translation",
                     Decoder::InstructionToString(inst).c_str(), inst.pc);
        }
}

} // namespace Libs::Graphics::ShaderRecompiler::Frontend
