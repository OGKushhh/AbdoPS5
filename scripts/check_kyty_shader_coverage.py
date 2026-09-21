#!/usr/bin/env python3
"""
Kyty-008: Shader opcode coverage checker.

Checks that all canonical V_CMP_*_{U,I}64 VOPC opcodes (encoding bytes
0xA0-0xF7) have dispatch entries in KytyPS5's shader recompiler.

This is the CI gate that would have caught Kyty-001 (the 25-missing-
opcode gap that blocked Sifu/Returnal/Spider-Man/Demon's Souls) at
build time instead of at game-boot time.

Usage:
  python3 check_kyty_shader_coverage.py [--verbose]

Exit code 0 = all opcodes covered
Exit code 1 = missing opcodes found
"""
import re
import sys
import os
from pathlib import Path

# Use the script's own location to find the repo root (works locally and in CI)
REPO = Path(__file__).resolve().parent.parent
SHADER_DECODER_H = REPO / "src/graphics/shader/recompiler/frontend/decode/ShaderDecoder.h"
VECTOR_ALU_OPS_CPP = REPO / "src/graphics/shader/recompiler/frontend/decode/VectorAluOps.cpp"
VECTOR_CPP = REPO / "src/graphics/shader/recompiler/frontend/translate/Vector.cpp"

# Canonical V_CMP_*_{U,I}64 opcodes (8 conditions × 4 variants = 32 total)
# Each condition maps to a VOPC encoding byte
CANONICAL_OPCODES = {
    # V_CMP_*_I64 (0xA0-0xA7)
    0xA0: "V_CMP_F_I64",  0xA1: "V_CMP_LT_I64", 0xA2: "V_CMP_EQ_I64", 0xA3: "V_CMP_LE_I64",
    0xA4: "V_CMP_GT_I64", 0xA5: "V_CMP_NE_I64", 0xA6: "V_CMP_GE_I64", 0xA7: "V_CMP_T_I64",
    # V_CMPX_*_I64 (0xB0-0xB7)
    0xB0: "V_CMPX_F_I64",  0xB1: "V_CMPX_LT_I64", 0xB2: "V_CMPX_EQ_I64", 0xB3: "V_CMPX_LE_I64",
    0xB4: "V_CMPX_GT_I64", 0xB5: "V_CMPX_NE_I64", 0xB6: "V_CMPX_GE_I64", 0xB7: "V_CMPX_T_I64",
    # V_CMP_*_U64 (0xE0-0xE7)
    0xE0: "V_CMP_F_U64",  0xE1: "V_CMP_LT_U64", 0xE2: "V_CMP_EQ_U64", 0xE3: "V_CMP_LE_U64",
    0xE4: "V_CMP_GT_U64", 0xE5: "V_CMP_NE_U64", 0xE6: "V_CMP_GE_U64", 0xE7: "V_CMP_T_U64",
    # V_CMPX_*_U64 (0xF0-0xF7)
    0xF0: "V_CMPX_F_U64",  0xF1: "V_CMPX_LT_U64", 0xF2: "V_CMPX_EQ_U64", 0xF3: "V_CMPX_LE_U64",
    0xF4: "V_CMPX_GT_U64", 0xF5: "V_CMPX_NE_U64", 0xF6: "V_CMPX_GE_U64", 0xF7: "V_CMPX_T_U64",
}

def check_enum_defined():
    """Check that all canonical opcodes are defined in ShaderDecoder.h"""
    text = SHADER_DECODER_H.read_text()
    missing = []
    for encoding, opcode_name in CANONICAL_OPCODES.items():
        # Look for the opcode name as an enum entry
        pattern = rf"\b{opcode_name}\b"
        if not re.search(pattern, text):
            missing.append((encoding, opcode_name, "enum"))
    return missing

def check_decoder_table():
    """Check that all canonical opcodes have decoder table entries in VectorAluOps.cpp"""
    text = VECTOR_ALU_OPS_CPP.read_text()
    missing = []
    for encoding, opcode_name in CANONICAL_OPCODES.items():
        # Look for the encoding byte in the decoder table
        hex_byte = f"0x{encoding:02x}u"
        pattern = rf"{re.escape(hex_byte)}.*Opcode::{opcode_name}"
        if not re.search(pattern, text):
            missing.append((encoding, opcode_name, "decoder_table"))
    return missing

def check_dispatch_cases():
    """Check that all canonical opcodes have dispatch cases in Vector.cpp"""
    text = VECTOR_CPP.read_text()
    missing = []
    for encoding, opcode_name in CANONICAL_OPCODES.items():
        pattern = rf"case\s+O::{opcode_name}\s*:"
        if not re.search(pattern, text):
            missing.append((encoding, opcode_name, "dispatch_case"))
    return missing

def main():
    verbose = "--verbose" in sys.argv

    print("=== Kyty-008: Shader Opcode Coverage Check ===\n")
    print(f"Checking {len(CANONICAL_OPCODES)} canonical V_CMP_*_{{U,I}}64 opcodes...\n")

    all_good = True

    # Check enum
    missing_enum = check_enum_defined()
    if missing_enum:
        print(f"FAIL: {len(missing_enum)} opcodes missing from ShaderDecoder.h enum:")
        for enc, name, _ in missing_enum:
            print(f"  0x{enc:02X} {name}")
        all_good = False
    else:
        print(f"OK: All {len(CANONICAL_OPCODES)} opcodes defined in enum")

    # Check decoder table
    missing_table = check_decoder_table()
    if missing_table:
        print(f"\nFAIL: {len(missing_table)} opcodes missing from decoder table:")
        for enc, name, _ in missing_table:
            print(f"  0x{enc:02X} {name}")
        all_good = False
    else:
        print(f"OK: All {len(CANONICAL_OPCODES)} opcodes have decoder table entries")

    # Check dispatch cases
    missing_dispatch = check_dispatch_cases()
    if missing_dispatch:
        print(f"\nFAIL: {len(missing_dispatch)} opcodes missing from dispatch:")
        for enc, name, _ in missing_dispatch:
            print(f"  0x{enc:02X} {name}")
        all_good = False
    else:
        print(f"OK: All {len(CANONICAL_OPCODES)} opcodes have dispatch cases")

    # Summary
    total_missing = len(missing_enum) + len(missing_table) + len(missing_dispatch)
    if total_missing == 0:
        print(f"\n✅ ALL {len(CANONICAL_OPCODES)} V_CMP_*_{{U,I}}64 opcodes are fully covered.")
        print("   This check would have caught Kyty-001 at build time.")
        return 0
    else:
        print(f"\n❌ {total_missing} coverage gaps found. See details above.")
        print("   These gaps will cause EXIT() at Dispatch.cpp:15 when a game uses them.")
        return 1

if __name__ == "__main__":
    sys.exit(main())
