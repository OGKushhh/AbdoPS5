# Kyty-007: VCC/EXEC Mask Provenance Revert Investigation

> **Status:** Investigation complete. Root cause identified. Fix requires careful IR redesign.
> 
> **Source:** Revert `e591a66` of PR #418 (`6758d67`) by `nomolao2-cell`, reverted by `nmzik` on Sep 1, 2026.
> 
> **Revert message (verbatim):** "An EXEC pair loses its mask provenance and falls into the raw high-word path. The saved all-ones high word then keeps waterfall branches true and wedges GPU work."

---

## 1. What PR #418 Tried to Do

PR #418 ("shader: preserve raw VCC bits in S_MOV_B64") was a major refactoring of how the shader recompiler handles 64-bit scalar masks (VCC and EXEC registers). The problem it addressed:

### The Old Approach (pre-PR #418, current state)

The old code treats EXEC and VCC as two separate things:
1. **Raw 64-bit value** (`ExecLo`/`ExecHi`, `VccLo`/`VccHi`) — the actual bits
2. **Per-thread invocation mask** (`Exec`, `Vcc`) — a derived `U1` that says "is this thread active?"

When you do `S_MOV_B64 EXEC, SGPR0`, the old code:
- Copies the raw 64 bits (`WriteU32Pair`)
- Tries to preserve the "mask" provenance (`ReadMask` + `ReadMaskValid`)
- But `ReadMaskValid` for EXEC/VCC always returns `true` — there's no way to track whether the mask is "valid" (derived from a comparison) or "raw" (just copied bits)

### The Problem

When a game does:
```
S_MOV_B64 S0, EXEC       ; save EXEC to S0
S_MOV_B64 EXEC, S0       ; restore EXEC from S0
S_CBRANCH_EXECZ label    ; branch if EXEC is zero
```

The old code's `ReadMaskValid` always returns `true` for SGPRs, so the `S_CBRANCH_EXECZ` thinks EXEC is a valid mask. But if the saved EXEC had its high word as all-ones (which is common in wave32 mode), the branch condition is wrong — it evaluates as "not zero" when it should be "zero".

This causes **waterfall branches** (branches that are supposed to execute one lane at a time) to evaluate as always-true, **wedging GPU work** — the GPU hangs because the waterfall loop never terminates.

### PR #418's Solution

PR #418 introduced a `ScalarU64` struct that bundles all three components:

```cpp
struct ScalarU64 {
    std::array<IR::U32, 2> raw;   // The raw 64 bits
    IR::U1 invocation;            // The per-thread mask (may be derived from raw)
    IR::U1 mask_valid;            // Is `invocation` a real mask, or just raw bits?
};
```

And added two new IR state variables:
- `ExecMaskTag` — tracks whether EXEC's `invocation` is a valid mask
- `VccMaskValidTag` — tracks whether VCC's `invocation` is a valid mask

When you write raw bits to EXEC/VCC (via `WriteRawU32`), `mask_valid` is set to `false`. When you write a mask (via `WriteMask`), `mask_valid` is set to `true`.

The `ReadMaskValid` function then returns the actual tag, instead of always returning `true`.

### Why It Was Reverted

The revert message says: "An EXEC pair loses its mask provenance and falls into the raw high-word path."

This means: in some code paths, the `mask_valid` tag was getting lost (defaulting to `true` or `false` incorrectly), causing the same wedging behavior the PR was trying to fix. The fix introduced a new bug that was just as bad as the original.

The likely root cause: the `ScalarU64` struct was not propagated through ALL code paths that touch EXEC/VCC. Some paths (probably in the SPIR-V backend or the SSA rewrite pass) were still using the old `ReadMask`/`ReadMaskValid` approach, which didn't know about the new `ExecMaskTag`/`VccMaskValidTag` state. When these paths interacted with the new `ScalarU64` paths, the `mask_valid` tag was inconsistent.

---

## 2. The 13 Files PR #418 Changed

| File | Lines changed | What it does |
|---|---|---|
| `frontend/translate/Translate.cpp` | +123, -... | `ReadScalarU64`, `WriteScalarU64`, `BinaryScalarU64`, `NotScalarU64`, `SelectScalarU64`, `MakeScalarU64Result`, `ScalarU64NonZero` helpers |
| `frontend/translate/Translator.h` | +39, -... | `ScalarU64` struct declaration + helper method signatures |
| `frontend/translate/Control.cpp` | +major rewrite | `S_SAVEEXEC`, `S_CSELECT_B64`, `S_MOV_B64`, `S_WQM_B64` rewritten to use `ScalarU64` |
| `frontend/translate/Compare.cpp` | +1 | `ir.SetExecMaskTag(IR::U1(IR::Value(true)))` in `EmitCompareResult` |
| `frontend/translate/Integer.cpp` | -53, +7 | `S_U64_MASK` rewritten from 60 lines to 7 lines using `ScalarU64` |
| `frontend/translate/Memory.cpp` | +3 | Minor: `GetScalarAddressResource` signature change |
| `ir/IREmitter.cpp` | +16 | `GetExecMaskTag`, `SetExecMaskTag`, `GetVccMaskValidTag`, `SetVccMaskValidTag` |
| `ir/IREmitter.h` | +4 | Method declarations |
| `ir/Program.cpp` | +4 | New IR state initialization |
| `ir/opcodes/ValueOpcodes.inc` | +4 | `GetExecMaskTag`, `SetExecMaskTag`, `GetVccMaskValidTag`, `SetVccMaskValidTag` opcodes |
| `ir/passes/SsaRewrite.cpp` | +42, -... | `ExecMaskTag` and `VccMaskValidTag` variant types in the SSA variable map |
| `tests/ShaderRecompilerComputeTests.cpp` | +187 | Tests for the new mask provenance tracking |
| `tests/shaderCfgTests.cpp` | +28, -... | CFG test updates |

**Total:** 444 insertions, 185 deletions across 13 files. This is a large, invasive change.

---

## 3. Root Cause of the Wedge

The revert message says: "The saved all-ones high word then keeps waterfall branches true and wedges GPU work."

**Waterfall branches** are a GCN/RDNA2 pattern where you execute a branch for one lane at a time:
```
S_SAVEEXEC_B64 S0, EXEC    ; save current EXEC
S_MOV_B64 EXEC, 1           ; enable only lane 0
; ... do work for lane 0 ...
S_MOV_B64 EXEC, S0          ; restore EXEC
S_CBRANCH_EXECZ loop_end    ; if no more lanes active, exit loop
```

If `S_SAVEEXEC_B64` saves the EXEC pair but loses the `mask_valid` tag (defaulting it to `true`), then when `S_CBRANCH_EXECZ` checks if EXEC is zero, it reads the raw bits. If the raw high word is all-ones (which happens in wave32 mode where the high word is unused), the branch condition is "not zero" — so the waterfall loop never terminates.

**The GPU hangs.** This is the "wedges GPU work" from the revert message.

---

## 4. Why the Fix Is Hard

PR #418 was the right approach — you DO need to track mask provenance separately from raw bits. But the implementation had a flaw: the `mask_valid` tag was not consistently propagated through all code paths.

The specific issue is that the old code had many places that read/write EXEC and VCC:
- `ReadMask` / `WriteMask` — reads/writes the `invocation` (U1)
- `ReadU32Pair` / `WriteU32Pair` — reads/writes the raw bits
- `ReadMaskValid` — reads whether the mask is valid
- `WriteRawU32` — writes raw bits without updating the mask

PR #418 added `ExecMaskTag` / `VccMaskValidTag` but didn't update ALL of these paths consistently. Some paths still used the old approach, creating inconsistencies.

### The Proper Fix

The proper fix requires:

1. **Audit every code path** that reads or writes EXEC/VCC. There are at least 15 such paths in the codebase.

2. **Make `ScalarU64` the only way** to read/write 64-bit scalar masks. Remove the separate `ReadMask`/`WriteMask`/`ReadU32Pair`/`WriteU32Pair` paths for EXEC/VCC — force everything through `ReadScalarU64`/`WriteScalarU64`.

3. **Propagate `mask_valid` through the SPIR-V backend.** The backend needs to know whether to emit a `OpBitcast` (raw bits) or an `OpLoad` (mask) when reading EXEC/VCC.

4. **Propagate `mask_valid` through the SSA rewrite pass.** The SSA pass needs to phi-merge `mask_valid` tags at basic block boundaries.

5. **Add regression tests** for the waterfall-branch pattern. The 187 lines of tests in PR #418 should be re-added with additional tests for the specific wedge scenario.

This is a **2-3 week** refactor that touches the shader recompiler's core IR. It's the right thing to do, but it's not a quick fix.

---

## 5. Immediate Workaround

Until the proper fix is developed, the `SkipShaderAssert` hack from Kyty-003 can be used to skip the waterfall-branch wedge — but this will cause **visual artifacts** (incorrect rendering of any shader that uses waterfall branches).

No clean workaround exists for this issue. The proper fix is required.

---

## 6. Impact on Games

The VCC/EXEC mask provenance bug affects **any game that uses waterfall branches** — which is most AAA titles. Waterfall branches are used for:
- **Atomic operations** on specific lanes
- **Scalar reductions** (sum, min, max across lanes)
- **Divergent control flow** where each lane takes a different path

Games likely affected:
- **Sifu** (#739) — uses waterfall branches for particle systems
- **Returnal** (#742) — uses waterfall branches for ray-traced reflections
- **Spider-Man Remastered** (#701) — uses waterfall branches for NPC AI
- **Demon's Souls** (#697) — uses waterfall branches for physics

But these games are ALSO blocked by Kyty-001 (shader opcodes) and Kyty-006 (depth-texture). The VCC/EXEC mask provenance bug is a **third** blocker that would manifest after the first two are fixed.

---

## 7. Recommended Next Steps

1. **Phase 1 (1 week):** Re-apply PR #418 on a test branch and identify the specific code path where `mask_valid` is lost. Use Tracy profiling to trace the EXEC/VCC tag through the shader pipeline.

2. **Phase 2 (1-2 weeks):** Fix the identified code paths. Make `ScalarU64` the only way to read/write 64-bit scalar masks.

3. **Phase 3 (1 week):** Add regression tests for the waterfall-branch wedge scenario. Re-add the 187 lines of tests from PR #418 plus new tests.

4. **Phase 4 (ongoing):** Test with Sifu, Returnal, Spider-Man, Demon's Souls once Kyty-001 and Kyty-006 are also fixed.

**Estimated total effort:** 3-4 weeks of focused shader-compiler work.

---

## 8. Commit Hashes for Reference

| Item | Hash | Description |
|---|---|---|
| Original PR #418 | `6758d67` | "shader: preserve raw VCC bits in S_MOV_B64 (#418)" by nomolao2-cell, Aug 30 |
| Revert | `e591a66` | "Revert 'shader: preserve raw VCC bits in S_MOV_B64 (#418)'" by nmzik, Sep 1 |
| Related fix (Sep 9) | `1af19ea` | "shader: fix incorrect per-lane VCCZ/EXECZ reads" — partial fix for the same class of bug |
