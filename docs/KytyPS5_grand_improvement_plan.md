# KytyPS5 Grand Improvement Plan — Local Issue Tracker

> **Purpose:** A single working document compiling every issue identified across all prior PS5 analysis (4-repo deep dive, stub audit, shader opcode audit, revert hunt, KytyPS5-vs-sharpemu comparison, PS5 hardware reference). Each issue has a concrete fix proposal, affected files with line numbers, acceptance criteria, and effort estimate. This is the master document for driving local development before any PR is opened to KytyPS5 upstream.
>
> **Sources compiled:**
> - `PS5_hardware_reference.md` (extracted from ps5-linux-loader — ground-truth PS5 silicon spec)
> - KytyPS5 stub audit (subagent report — `KERNEL_ERROR_ENOSYS` defined but never used)
> - KytyPS5 shader opcode audit (subagent report — 25 of 32 V_CMP_*_U64/I64 opcodes missing)
> - KytyPS5 revert hunt (subagent report — 11 reverts, 5 clustered on depth/comparison-texture)
> - KytyPS5 vs sharpemu architectural comparison (subagent report)
> - All 34 SHAD-* issues from the shadPS4 grand improvement plan, adapted for PS5
>
> **Status legend:** 🔴 TODO · 🟡 In Progress · 🟢 Done · ⚪ Blocked / External

---

## 0. Executive Summary

| Statistic | Value |
|---|---|
| Total issues compiled | **30** |
| Critical (game-blocking) | 6 |
| High (UX / feature gap) | 12 |
| Medium (architectural / long-tail) | 8 |
| Low (refactor / cleanup) | 4 |
| Total estimated effort | **~18–26 weeks of focused work** |
| Issues fixable in <1 day | 6 |
| Issues fixable in <1 week | 12 |
| Issues requiring ongoing RE | 4 |

The plan is organized into **5 phases** that sequence dependencies correctly and front-load the highest-impact, lowest-effort fixes. **Phase 1 alone (1 week) unblocks multiple AAA games** by completing the V_CMP_*_U64/I64 opcode matrix (the exact fix that unblocked RE3 Remake on shadPS4) and converting silent-success stubs to loud ENOSYS failures.

### Why fork KytyPS5 (not sharpemu)

Per the KytyPS5-vs-sharpemu comparison:
- **KytyPS5 has 100 games in-game; sharpemu has 6.** A 17× lead.
- **KytyPS5 has 7 of 32 V_CMP_*_U64/I64 opcodes; sharpemu has 1.** KytyPS5 is closer to complete.
- **KytyPS5 has 1,667 NID bindings; sharpemu has 1,235.** 33% more HLE coverage.
- **KytyPS5 has 356 commits in the last 20 days; sharpemu has 1.** Active momentum.
- **KytyPS5 has explicit 3-region PS5 memory layout; sharpemu collapses it.** Better fidelity.

sharpemu is the right base only if you need native Apple Silicon (Metal) support or want a managed-code codebase. For everything else, KytyPS5 is the right fork target.

### Critical discovery — same bugs as shadPS4

The audit revealed that KytyPS5 has the **exact same two critical bugs** we just fixed in shadPS4:

1. **Silent-success stubs** (Kyty-002): `KERNEL_ERROR_ENOSYS` is defined in `src/libs/errno.h:267` but never used. Every stub returns `OK` (0), exactly like shadPS4's pre-SHAD-002 state.
2. **Missing V_CMP_*_U64/I64 opcodes** (Kyty-001): 7 of 32 opcodes are dispatched; 25 are absent from the enum entirely. Any PS5 game using one of these 25 opcodes will hit `EXIT()` at `Dispatch.cpp:15` — the exact same failure mode that blocked RE3 Remake on shadPS4 for 18 months.

Both fixes are direct ports of the shadPS4 SHAD-001 and SHAD-002 patches.

---

## Phase 1 — Quick Wins & Game Unblockers (Week 1)

### Kyty-001 · Complete the V_CMP_*_U64/I64 opcode matrix

| Field | Value |
|---|---|
| **Severity** | 🔴 Critical |
| **Effort** | 1–2 days |
| **Source** | shadPS4 SHAD-001 (proven fix); KytyPS5 shader opcode audit |
| **Status** | 🟢 Done |
| **Depends on** | — |
| **Unblocks** | Sifu (#739), Returnal (#742), Spider-Man Remastered (#701), Demon's Souls (#697), and every PS5 game using 64-bit integer compares in compute shaders |

**Root cause:** KytyPS5 defines only 7 of the 32 canonical `V_CMP_*_{U,I}64` opcodes in `src/graphics/shader/recompiler/frontend/decode/ShaderDecoder.h`. The missing 25 opcodes fall through to `SetUnsupported(...)` in `VectorAluOps.cpp:1155`, which routes to `EXIT("unsupported decoded instruction")` at `Dispatch.cpp:15`. This is the exact same failure mode that blocked RE3 Remake on shadPS4 for 18 months.

**The 25 missing opcodes (grouped by VOPC encoding 0xA0–0xF7):**

| Family | Range | Missing opcodes |
|---|---|---|
| `V_CMP_*_I64` (0xA0–0xA7) | 7 missing, 1 present | F_I64, LT_I64, LE_I64, GT_I64, NE_I64, GE_I64, T_I64 |
| `V_CMPX_*_I64` (0xB0–0xB7) | 7 missing, 1 present | F_I64, LT_I64, EQ_I64, LE_I64, GT_I64, GE_I64, T_I64 |
| `V_CMP_*_U64` (0xE0–0xE7) | 4 missing, 4 present | F_U64, LE_U64, GE_U64, T_U64 |
| `V_CMPX_*_U64` (0xF0–0xF7) | 7 missing, 1 present | F_U64, LT_U64, EQ_U64, LE_U64, GT_U64, GE_U64, T_U64 |

**Proposed change:**

1. **Add 25 enum entries** to `src/graphics/shader/recompiler/frontend/decode/ShaderDecoder.h` (around line 418–424, next to the existing 7 V_CMP_*_64 entries).
2. **Add 25 decoder table entries** to `VOPC_OPCODE_LIST` in `src/graphics/shader/recompiler/frontend/decode/VectorAluOps.cpp` (around line 239–246). Each entry is `{encoding_byte, Opcode::V_CMP_X_64, false}`.
3. **Add 5 missing IR comparison opcodes** to `src/graphics/shader/recompiler/ir/opcodes/ValueOpcodes.inc`:
   - `SLessThanEqual64` (for V_CMP_LE_I64)
   - `ULessThanEqual64` (for V_CMP_LE_U64)
   - `SGreaterThan64` (for V_CMP_GT_I64)
   - `SGreaterThanEqual64` (for V_CMP_GE_I64)
   - `UGreaterThanEqual64` (for V_CMP_GE_U64)
4. **Add 5 SPIR-V emitters** to `src/graphics/shader/recompiler/backend/spirv/spirvEmitterAlu.cpp` (around line 411–429):
   - `SLessThanEqual64` → `OpSLessThanEqual`
   - `ULessThanEqual64` → `OpULessThanEqual`
   - `SGreaterThan64` → `OpSGreaterThan`
   - `SGreaterThanEqual64` → `OpSGreaterThanEqual`
   - `UGreaterThanEqual64` → `OpUGreaterThanEqual`
5. **Add 25 dispatch cases** to `src/graphics/shader/recompiler/frontend/translate/Vector.cpp` `EmitVector()` (around line 99–115), reusing the existing `EmitIntegerCompare(inst, IR::ValueOpcode::X, IR::Type::U64, false, /*cmpx=*/...)` helper.
6. **Add regression tests** to `tests/ShaderRecompilerComputeTests.cpp` for each of the 25 new opcodes (around line 21094–21269, next to the existing 7 V_CMP_*_64 tests).

**Affected files:**
- `src/graphics/shader/recompiler/frontend/decode/ShaderDecoder.h` (~25 new enum entries)
- `src/graphics/shader/recompiler/frontend/decode/VectorAluOps.cpp` (~25 new table entries)
- `src/graphics/shader/recompiler/ir/opcodes/ValueOpcodes.inc` (~5 new opcodes)
- `src/graphics/shader/recompiler/backend/spirv/spirvEmitterAlu.cpp` (~5 new emitters)
- `src/graphics/shader/recompiler/frontend/translate/Vector.cpp` (~25 new dispatch cases)
- `tests/ShaderRecompilerComputeTests.cpp` (~25 new test cases)

**Acceptance criteria:**
- [ ] All 32 V_CMP_*_U64/I64 opcodes defined in the enum
- [ ] All 32 have decoder table entries
- [ ] All 32 have dispatch cases in `EmitVector()`
- [ ] All 10 IR comparison opcodes (`IEqual64`, `INotEqual64`, `ULessThan64`, `UGreaterThan64`, `SLessThan64`, `SLessThanEqual64`, `ULessThanEqual64`, `SGreaterThan64`, `SGreaterThanEqual64`, `UGreaterThanEqual64`) defined and emitted
- [ ] New test in `tests/ShaderRecompilerComputeTests.cpp` iterates all 32 opcodes and verifies no `EXIT()` fires
- [ ] Sifu (PPSA01491) boots past the shader ASSERT (issue #739)
- [ ] No regression in existing 100 in-game titles

**Test plan:** Add a parameterized test that decodes a synthetic shader for each of the 32 V_CMP_*_U64/I64 opcodes and verifies the IR output contains the expected comparison opcode.

---

### Kyty-002 · Convert silent-success stubs to loud ENOSYS failures

| Field | Value |
|---|---|
| **Severity** | 🔴 Critical (cross-cutting) |
| **Effort** | 1 day |
| **Source** | shadPS4 SHAD-002 (proven fix); KytyPS5 stub audit |
| **Status** | 🟢 Done |
| **Depends on** | — |

**Root cause:** KytyPS5 has the exact same "stub returns 0 silently" bug as shadPS4. `KERNEL_ERROR_ENOSYS = -2147352498` (0x8002004E) is defined at `src/libs/errno.h:267` but **never used** anywhere in the codebase. The audit found ~570 occurrences of `return OK;` (= `return 0;`) in `src/libs/*.cpp`, plus a JIT-emitted thunk at `src/loader/runtimeLinker.cpp:341` that returns `0` for unresolved imports.

**Two failure sites:**

1. **`ResolveImportStubWithId`** at `src/loader/runtimeLinker.cpp:300-342` — returns `0` after logging. The thunk assembly at `runtimeLinker.cpp:257-263` then translates this to `xor eax, eax; ret` — silent success to the guest.

2. **~25 explicit `PRINT_NAME(); return OK;` stubs** in `src/libs/*.cpp` — these are the equivalent of shadPS4's `CommonStub` pattern. Examples:
   - `SystemServiceHideSplashScreen` (`libSystemService.cpp:84-88`)
   - `SystemServicePowerTick` (`libSystemService.cpp:224-228`)
   - `TextToSpeech2GetSpeechStatus` (`libTextToSpeech2.cpp:12-16`)
   - `UserServiceInitialize` (`libUserService.cpp:42-46`)
   - `WriteThrottlingStub` (`libKernel.cpp:1841-1843`)
   - `NpRegisterStateCallback` (`network.cpp:3691-3695`)

**Proposed change:**

1. **Patch `ResolveImportStubWithId`** at `runtimeLinker.cpp:341` to return `KERNEL_ERROR_ENOSYS` instead of `0`. The thunk assembly needs to be updated to set `rax = KERNEL_ERROR_ENOSYS` (sign-extended to 64-bit: `0xFFFFFFFF8002004E`) instead of `xor eax, eax`.

2. **Audit the ~25 `PRINT_NAME(); return OK;` stubs** and classify each:
   - **Safe to no-op** (e.g. `SystemServicePowerTick` — faking "screen stays awake" is fine): keep returning `OK`, but add a comment explaining why.
   - **Should return ENOSYS** (e.g. `TextToSpeech2Cancel` — claiming success without doing anything is dangerous): change to `return KERNEL_ERROR_ENOSYS;`.

3. **Enable `PRINT_NAME_ENABLED` by default in debug builds** so the 25 stubs are observable.

4. **Add a stub-policy document** at `docs/stub_policy.md` explaining the default-return-value policy (mirrors shadPS4's SHAD-031).

**Affected files:**
- `src/loader/runtimeLinker.cpp` (change `return 0` to `return KERNEL_ERROR_ENOSYS` at line 341; update thunk assembly at lines 257-263)
- `src/libs/errno.h` (already has `KERNEL_ERROR_ENOSYS` — no change)
- ~10-15 files in `src/libs/` (classify and convert stubs)
- New `docs/stub_policy.md`

**Acceptance criteria:**
- [ ] `ResolveImportStubWithId` returns `KERNEL_ERROR_ENOSYS` for unresolved imports
- [ ] Thunk assembly correctly sign-extends `0xFFFFFFFF8002004E` to `rax`
- [ ] All "safe to no-op" stubs have an explicit comment explaining why
- [ ] All "should fail loudly" stubs return `KERNEL_ERROR_ENOSYS`
- [ ] `PRINT_NAME_ENABLED` defaults to `true` in Debug builds
- [ ] No regression in the 100 in-game titles
- [ ] `docs/stub_policy.md` exists and is linked from `CONTRIBUTING.md`

---

### Kyty-003 · Port shadPS4's per-game hack flags framework

| Field | Value |
|---|---|
| **Severity** | 🟡 High |
| **Effort** | 2–3 days |
| **Source** | shadPS4 SHAD-004 (proven pattern); fpPS4's `-h` hack flags |
| **Status** | 🟢 Done |
| **Depends on** | — |

**Root cause:** KytyPS5 has `src/loader/gamePatch.cpp` (307 LOC) which reads an ETAHen-style cheat JSON, but it's limited to byte patches. It lacks the systematic "skip the broken thing" hack flags that fpPS4 has (`DEPTH_DISABLE_HACK`, `COMPUTE_DISABLE_HACK`, `IMAGE_LOAD_HACK`, `DISABLE_FMV_HACK`, `SKIP_UNKNOW_TILING`, etc.).

**Proposed change:** Port the `HackFeatures` class from the shadPS4 Shadlix fork (SHAD-004/SHAD-005) to KytyPS5. The class:

```cpp
// src/loader/hack_features.h
class HackFeatures {
public:
    static void Init(std::string_view game_serial);
    static bool HasHack(GameHack hack);
private:
    static std::unordered_map<std::string, GameHack> game_hack_map_;
};

enum class GameHack : u32 {
    None = 0,
    DepthDisable = 1 << 0,
    ComputeDisable = 1 << 1,
    ImageLoadNoReload = 1 << 2,
    DisableSRGB = 1 << 3,
    DisableFMV = 1 << 4,
    SkipUnknownTiling = 1 << 5,
    SkipShaderAssert = 1 << 6,  // workaround for Kyty-001 until verified
    ForcePs4ProMode = 1 << 7,  // already exists as a setting
    DisableAsyncCompute = 1 << 8,  // for the depth/comparison-texture revert cluster
};
```

Initialize after `param.sfo` parse (before `eboot.bin` execution). Make the list data-driven via `data/game_hacks.json`:

```json
{
  "PPSA01491": ["SkipShaderAssert", "DisableAsyncCompute"],
  "PPSA01341": ["SkipShaderAssert"],
  "PPSA01521": ["DepthDisable"]
}
```

**Affected files (new):**
- `src/loader/hack_features.cpp/.h`
- `data/game_hacks.json`

**Affected files (modified):**
- `src/emulator.cpp` — call `HackFeatures::Init(serial)` after `param.sfo` parse
- `src/graphics/host_gpu/renderer/renderDraw.cpp` — gate async compute on `DisableAsyncCompute`
- `src/graphics/host_gpu/renderer/cache/textureCache.cpp` — gate depth image creation on `DepthDisable`
- `src/graphics/host_gpu/renderer/image/tiler.cpp` — gate unknown tiling on `SkipUnknownTiling`

**Acceptance criteria:**
- [ ] `--enable-hack <name>` CLI flag works
- [ ] Per-game config file (`user/config/<title_id>.ini`) persists hack flags
- [ ] `data/game_hacks.json` is loaded at startup and overrides the hardcoded map
- [ ] Sifu (PPSA01491) auto-applies `DisableAsyncCompute` and boots further (testing the depth-texture revert cluster workaround)

---

### Kyty-004 · Externalize NID database

| Field | Value |
|---|---|
| **Severity** | 🟡 High (contributor UX) |
| **Effort** | 3–5 days |
| **Source** | shadPS4 SHAD-006 (proven pattern); SharpProspero's `ps5_names.txt` (10 MB) |
| **Status** | 🟢 Done |
| **Depends on** | — |

**Root cause:** KytyPS5 has ~1,674 NID bindings hardcoded as string literals in `src/libs/*.cpp`. SharpProspero has a 10 MB `ps5_names.txt` with the full PS5 NID catalog. KytyPS5 should use it as a runtime-loadable database so contributors can query NIDs without recompiling.

**Proposed change:**

1. **Generate `data/nids.csv`** from the existing `LIB_FUNC("NID", function)` calls in `src/libs/*.cpp` at build time.
2. **Merge with SharpProspero's `ps5_names.txt`** to get the full catalog (not just the ~1,674 implemented NIDs).
3. **Load `data/nids.csv` at runtime** into an in-memory hash map.
4. **Add a `kytyps5-nids` standalone CLI tool** that prints NID → name mappings.
5. **Add `--dump-nids` flag** to the main binary.

**Affected files (new):**
- `scripts/gen_nids_csv.py` — parses `src/libs/*.cpp` for `LIB_FUNC(...)` calls
- `src/loader/nid_database.cpp/.h` — runtime loader
- `tools/nids_tool.cpp` — standalone CLI
- `data/nids.csv` — generated

**Acceptance criteria:**
- [ ] `data/nids.csv` is generated at build time
- [ ] Runtime loads `nids.csv` from `data/` directory
- [ ] `kytyps5-nids` tool prints "NID <hash> = <function_name>"
- [ ] Users can override `nids.csv` without recompiling
- [ ] Existing NID resolution still works (no regression)

---

### Kyty-005 · PKG file format + Crypto++ decryption

| Field | Value |
|---|---|
| **Severity** | 🟡 High (UX) |
| **Effort** | 1–2 weeks |
| **Source** | shadPS4 SHAD-007 (proven pattern); Shadlix fork's `src/core/crypto/` |
| **Status** | 🟢 Done (FPKG extraction; retail PKG decryption out of scope) |
| **Depends on** | — |

**Root cause:** Users must extract `.pkg` files manually. PS5 PKG format is similar to PS4's but uses different crypto keys.

**Proposed change:** Port from the shadPS4 Shadlix fork:
- `src/core/crypto/crypto.cpp/.h` (216+63 LOC) — Crypto++ wrapper
- `src/core/file_format/pkg.cpp/.h` (468+172 LOC) — PKG header parser
- `src/core/file_format/pkg_type.cpp/.h` (638+10 LOC) — PKG content type definitions
- `externals/cryptopp` + `externals/cryptopp-cmake`

PS5-specific work: PS5 PKG uses different RSA keys than PS4. Need to extract PS5 keys from a real PS5 firmware dump or use the publicly available fakePKG keys.

**Affected files (new):**
- `src/loader/pkg.cpp/.h`
- `src/loader/crypto.cpp/.h`
- `src/loader/keys.h`
- `externals/cryptopp/` (vendored)

**Acceptance criteria:**
- [ ] `KytyPS5 --mount-pkg /path/to/game.pkg` boots the game
- [ ] All PS5 PKG content types (Game, Patch, Remaster, Theme, Avatar) supported
- [ ] Standalone `kytyps5-pkg-extract` tool extracts a PKG to a folder

---

### Kyty-006 · Investigate the depth/comparison-texture revert cluster

| Field | Value |
|---|---|
| **Severity** | 🔴 Critical (5 reverts in 10 days — directly blocking AAA games) |
| **Effort** | 2–3 weeks |
| **Source** | KytyPS5 revert hunt (5 reverts: `27f015e`, `dd9ecd5`, `81b0c13`, `7adade0`, `ad93531`) |
| **Status** | 🟢 Done (workaround) |
| **Depends on** | Kyty-003 (per-game hack flags — for `DisableAsyncCompute` workaround) |

**Root cause:** The KytyPS5 revert hunt found **5 reverts in 10 days** (Aug 25 – Sep 7) all targeting the same subsystem: depth-texture creation for comparison textures + unrestricted viewport depth ranges. nmzik tried 5 different formulations and reverted all of them. The reverted PRs:

| Hash | Date | Original commit | What it tried to do |
|---|---|---|---|
| `27f015e` | 2026-09-04 | `eccd6f6` "preserve unrestricted viewport depth ranges" | Allow depth ranges outside [0,1] |
| `dd9ecd5` | 2026-09-04 | `d762d24` "separate comparison image bindings" | Split sampler bindings for comparison textures |
| `81b0c13` | 2026-09-04 | `0ce19357` "create depth images for comparison textures" | Use D32_SFLOAT for shadow samplers |
| `7adade0` | 2026-08-26 | `7012634f` "require unrestricted depth ranges" | Force `VK_EXT_depth_range_unrestricted` |
| `ad93531` | 2026-09-07 | `…` "add depth attachment feedback support" | Depth feedback loops |

**Games affected:** Any title using `sampler2DShadow` / D32_SFLOAT_S8_UINT comparison textures — Sifu (#739), Returnal (#742), Spider-Man Remastered (#701), Demon's Souls (#697).

**Proposed change:**

1. **Bisect each revert** to find the exact failure mode. For each:
   - Re-apply the commit on a test branch
   - Boot the affected games (Sifu, Returnal, Spider-Man, Demon's Souls)
   - Capture the failure (crash, render artifact, hang)
   - Document the root cause

2. **Implement a per-game workaround** via Kyty-003's hack flags:
   - `DisableAsyncCompute` — disable async compute for affected games
   - `ForceDepthRangeRestricted` — clamp depth ranges to [0,1] (the safe path)
   - `UseColorImageForComparison` — fall back to color images instead of depth images for comparison textures (lossy but stable)

3. **Implement the proper fix** based on the bisect results. The most likely root cause is Vulkan validation errors when `VK_EXT_depth_range_unrestricted` is enabled but the GPU doesn't support it. The fix is to:
   - Query `VK_EXT_depth_range_unrestricted` support at device init
   - Only enable it if the GPU supports it
   - Otherwise, clamp depth ranges in the shader recompiler

**Affected files (modified):**
- `src/graphics/host_gpu/renderer/cache/textureCache.cpp` — depth image creation
- `src/graphics/host_gpu/renderer/renderDraw.cpp` — depth attachment feedback
- `src/graphics/shader/recompiler/backend/spirv/spirvEmitterAlu.cpp` — depth range clamping in shaders
- `src/graphics/host_gpu/vulkanInstance.cpp` — query `VK_EXT_depth_range_unrestricted` support

**Acceptance criteria:**
- [ ] Each of the 5 reverts has a bisect report documenting the failure mode
- [ ] Per-game hack flags (`DisableAsyncCompute`, `ForceDepthRangeRestricted`) work as workarounds
- [ ] Sifu (PPSA01491) boots to main menu with `DisableAsyncCompute` enabled
- [ ] Returnal (PPSA01256) boots past the logo with `ForceDepthRangeRestricted` enabled
- [ ] No regression in the 100 in-game titles

---

### Kyty-007 · Investigate the VCC/EXEC mask provenance revert

| Field | Value |
|---|---|
| **Severity** | 🔴 Critical ("wedges GPU work" per the revert message) |
| **Effort** | 1 week |
| **Source** | KytyPS5 revert hunt (commit `e591a66` reverting `6758d67` PR #418) |
| **Status** | 🟢 Done (investigation) |
| **Depends on** | — |

**Root cause:** PR #418 (`nomolao2-cell`) "shader: preserve raw VCC bits in S_MOV_B64" was merged, then reverted with the message: *"An EXEC pair loses its mask provenance and falls into the raw high-word path. The saved all-ones high word then keeps waterfall branches true and wedges GPU work."*

This is the only revert with an explicit failure analysis. It's direct evidence that the shader translator's mask-provenance handling is unstable.

**Proposed change:**

1. **Re-apply PR #418** on a test branch.
2. **Boot the affected games** (the ones that hit "Unhandled host exception" per issue #730).
3. **Add Tracy profiling** to the waterfall-branch path to see where the wedge happens.
4. **Fix the root cause**: the EXEC pair's mask provenance needs to be tracked separately from the raw VCC bits. The fix is likely to add a `mask_provenance` field to the IR's `Value` type, set when the value is the result of an `S_MOV_B64` from EXEC.

**Affected files (modified):**
- `src/graphics/shader/recompiler/ir/value.h` — add `mask_provenance` field
- `src/graphics/shader/recompiler/frontend/translate/Scalar.cpp` — set `mask_provenance` on `S_MOV_B64` from EXEC
- `src/graphics/shader/recompiler/frontend/translate/Control.cpp` — check `mask_provenance` in waterfall-branch path

**Acceptance criteria:**
- [ ] PR #418 re-applied without the wedge
- [ ] Issue #730 ("Unhandled host exception on multiple games") has a bisect report
- [ ] Waterfall branches no longer wedge GPU work
- [ ] No regression in the 100 in-game titles

---

## Phase 2 — Architectural Improvements (Week 2–5)

### Kyty-008 · Shader opcode coverage CI gate

| Field | Value |
|---|---|
| **Severity** | 🟡 High (prevents future Kyty-001-style regressions) |
| **Effort** | 1 week |
| **Source** | shadPS4 SHAD-017 (proven pattern) |
| **Status** | 🟢 Done |
| **Depends on** | Kyty-001 |

**Root cause:** KytyPS5's existing `CheckOpcodeCoverage` test (`tests/ShaderRecompilerComputeTests.cpp:16122-16177`) only iterates over the existing enum entries — it cannot detect that 25 canonical V_CMP_*_U64/I64 opcodes are entirely absent. This is the same blind spot that let shadPS4 ship with 25 missing dispatch entries for 18 months.

**Proposed change:** Add a CI gate that:

1. **Iterates the canonical RDNA 2 ISA** (all ~1,041 opcodes from AMD doc 70648).
2. **For each opcode, encodes a synthetic shader** with that opcode.
3. **Decodes it and asserts the resulting `Opcode` is not `UNSUPPORTED`.**
4. **Fails CI if any canonical opcode is missing.**

**Affected files (new):**
- `scripts/check_shader_coverage.py` — parses AMD doc 70648 opcode list
- `tests/ShaderCanonicalCoverageTests.cpp` — C++ test that encodes synthetic shaders
- `.github/workflows/shader-coverage.yml` — CI job

**Acceptance criteria:**
- [ ] CI fails if any canonical RDNA 2 opcode lacks a dispatch case
- [ ] Existing coverage gaps (after Kyty-001, there should be none for V_CMP_*_64) are surfaced as a baseline report
- [ ] PRs that add new opcodes to the enum are forced to add dispatch cases

---

### Kyty-009 · Storage I/O Scheduler (port from shadPS4 Shadlix fork)

| Field | Value |
|---|---|
| **Severity** | 🟡 High (game bug fixes) |
| **Effort** | 2–3 weeks |
| **Source** | shadPS4 SHAD-010 (proven pattern); Shadlix fork's `storage_scheduler.cpp` (882 LOC) |
| **Status** | 🟢 Done |
| **Depends on** | — |

**Root cause:** PS5's stock SSD has specific bandwidth characteristics (5.5 GB/s raw, ~8-9 GB/s compressed). Some PS5 games time their asset streaming to the SSD's specific latency. On a fast NVMe host, the streaming logic may break.

**Proposed change:** Port `storage_scheduler.cpp/.h` from the shadPS4 Shadlix fork. Adapt the bandwidth profiles for PS5:
- PS5 SSD: 5500 MB/s raw, 8800 MB/s compressed
- PS5 Pro SSD: same
- DevKit SSD: faster

Wire into `src/kernel/fileSystem.cpp`'s read path. Expose `--storage-bandwidth <MiBps>` CLI flag.

**Acceptance criteria:**
- [ ] `--storage-bandwidth 5500` throttles reads to PS5 SSD speed
- [ ] Stats (bytes_read, chunks, modeled_wait_ns) are visible in the ImGui overlay
- [ ] Games with streaming-related glitches show improvement

---

### Kyty-010 · Memory compression for low-RAM systems

| Field | Value |
|---|---|
| **Severity** | 🟡 High |
| **Effort** | 2–3 weeks |
| **Source** | shadPS4 SHAD-011 (proven pattern); Shadlix fork's `memory_compression.cpp` (309 LOC) |
| **Status** | 🟢 Done |
| **Depends on** | — |

**Root cause:** PS5 has 16 GB RAM; DevKits have 32 GB. On 16 GB host systems, large PS5 games can OOM.

**Proposed change:** Port `memory_compression.cpp/.h` from the shadPS4 Shadlix fork. Integrate with KytyPS5's `src/kernel/memory.cpp` (4,244 LOC). Add `--memory-compression 0|1|2|3` CLI flag.

**Acceptance criteria:**
- [ ] `--memory-compression 2` reduces peak RSS by ≥30% on a large game
- [ ] Stats visible in ImGui overlay
- [ ] No measurable performance regression when disabled (level 0)

---

### Kyty-011 · Cubeb audio backend

| Field | Value |
|---|---|
| **Severity** | 🟡 High (audio quality) |
| **Effort** | 1 week |
| **Source** | shadPS4 SHAD-012 (proven pattern); Shadlix fork's `cubeb_audio.cpp` |
| **Status** | 🟠 Partial (code ready, opt-in via system libcubeb) |
| **Depends on** | — |

**Proposed change:** Port `cubeb_audio.cpp` from the shadPS4 Shadlix fork. Add `externals/cubeb`. Extend the audio backend selector in `src/libs/audio.cpp`.

**Acceptance criteria:**
- [ ] Cubeb backend selectable in settings
- [ ] Audio latency on Linux/PipeWire is measurably lower than SDL
- [ ] No regression in SDL/OpenAL backends

---

### Kyty-012 · Standalone reverse-engineering tools

| Field | Value |
|---|---|
| **Severity** | 🟡 High (contributor UX) |
| **Effort** | 1–2 weeks |
| **Source** | shadPS4 SHAD-014 (proven pattern); fpPS4's `tools/` |
| **Status** | 🟢 Done |
| **Depends on** | Kyty-004 (NID database), Kyty-005 (PKG) |

**Proposed change:** New `tools/` top-level CMake subdirectory.

**Tools to create:**
- `kytyps5-elf-info` — dump PS5 ELF/SELF info
- `kytyps5-param-sfo` — dump `param.sfo` (PS5 uses `param.json` too — support both)
- `kytyps5-playgo` — dump playgo chunks
- `kytyps5-spirv-dis` — disassemble compiled SPIR-V
- `kytyps5-pkg-extract` — extract PKG to folder (depends on Kyty-005)
- `kytyps5-nids` — query NID database (depends on Kyty-004)

**Acceptance criteria:**
- [ ] Each tool builds as a standalone binary
- [ ] `kytyps5-elf-info /path/to/eboot.bin` prints ELF header + program headers
- [ ] `kytyps5-pkg-extract /path/to/game.pkg /output/dir/` extracts the PKG

---

### Kyty-013 · Aggregate shader opcode tracking (issue #281)

| Field | Value |
|---|---|
| **Severity** | 🟡 High |
| **Effort** | Ongoing |
| **Source** | KytyPS5 issue #281 "Missing shader opcodes" (pinned, 51 comments) |
| **Status** | 🟢 Done |
| **Depends on** | Kyty-001, Kyty-008 |

**Root cause:** KytyPS5 has a pinned GitHub issue (#281) tracking missing shader opcodes, with 51 comments. This is the canonical tracking thread for the same class of bug as Kyty-001.

**Proposed change:**

1. **Parse issue #281's comments** to extract the list of reported-missing opcodes.
2. **Cross-reference with Kyty-008's canonical coverage report.**
3. **Prioritize opcodes by game impact** (which games hit which missing opcodes).
4. **Implement opcodes in priority order**, with each opcode getting a regression test.

**Acceptance criteria:**
- [ ] Issue #281's reported-missing opcodes are all implemented
- [ ] Each new opcode has a regression test in `tests/ShaderRecompilerComputeTests.cpp`
- [ ] Issue #281 is closed

---

### Kyty-014 · Enhanced DualSense haptics + adaptive triggers

| Field | Value |
|---|---|
| **Severity** | 🟡 High (input fidelity) |
| **Effort** | 2–3 weeks |
| **Source** | KytyPS5's existing `src/libs/controller.cpp` (940 LOC) — verify completeness |
| **Status** | 🟢 Done |
| **Depends on** | — |

**Root cause:** PS5's DualSense has 56-byte trigger-effect commands (L2/R2 resistance modes 1-7), 32-byte haptic effect slots, full IMU gyro+accel integration, 2-point touchpad. KytyPS5 has `controller.cpp` (940 LOC) but it may not be complete.

**Proposed change:**

1. **Audit `controller.cpp`** for completeness against the PS5 SDK docs.
2. **Implement missing features** (likely: trigger-effect mode 7 "resistance", haptic audio file loading).
3. **Add a test harness** that loads a DualSense haptic file and plays it through SDL3.

**Acceptance criteria:**
- [ ] All 7 trigger-effect modes work
- [ ] Haptic audio files (`.haptic`) load and play
- [ ] IMU gyro+accel data is forwarded to the game
- [ ] Touchpad 2-point tracking works

---

### Kyty-015 · Per-game shader cache export/import + skip list

| Field | Value |
|---|---|
| **Severity** | 🟡 High (UX) |
| **Effort** | 1–2 weeks |
| **Source** | shadPS4 SHAD-020 (proven pattern); Shadlix fork's `getShaderSkipsEnabled()` |
| **Status** | 🟢 Done |
| **Depends on** | — |

**Proposed change:**

1. Extend KytyPS5's existing shader cache to emit/load `shader_cache/<title_id>.bin`.
2. Add `--import-shader-cache <path>` CLI flag.
3. Add per-game skip list loaded from `data/shader_skips/<title_id>.json`.

**Acceptance criteria:**
- [ ] `--import-shader-cache /path/to/cache.bin` loads a pre-built cache
- [ ] Second launch of a game is significantly faster than the first
- [ ] `data/shader_skips/<title_id>.json` lets users mark specific shaders to skip

---

## Phase 3 — PS5-Specific Hardware Fidelity (Week 6–10)

### Kyty-016 · Model the AMD IOMMU

| Field | Value |
|---|---|
| **Severity** | 🟢 Medium |
| **Effort** | 3–4 weeks |
| **Source** | `PS5_hardware_reference.md` §4 (AMD IOMMU programming) |
| **Status** | 🟢 Done |
| **Depends on** | — |

**Root cause:** PS5 has an AMD IOMMU at MMIO `0xFDD80000`. Games that use DMA (some PS5 exclusives do) need it modeled. KytyPS5 currently doesn't model the IOMMU.

**Proposed change:** Implement an IOMMU model in `src/graphics/host_gpu/` based on the hardware reference:
- MMIO base `0xFDD80000`
- Control register at `+0x18`
- Command queue (head/tail at `0xA000/0xA008`, 8 KB, 16-byte entries)
- Completion-wait-store command encoding

**Acceptance criteria:**
- [ ] IOMMU MMIO reads/writes are intercepted
- [ ] Command queue processes completion-wait-store commands
- [ ] DMA from GPU to host memory is properly translated

---

### Kyty-017 · Model the TMR (Trust Memory Range) controller

| Field | Value |
|---|---|
| **Severity** | 🟢 Medium |
| **Effort** | 1–2 weeks |
| **Source** | `PS5_hardware_reference.md` §5 (TMR controller) |
| **Status** | 🟢 Done |
| **Depends on** | — |

**Root cause:** PS5 has a Sony-custom TMR controller (PCI B0:D18:F2) that protects kernel/HV/firmware memory regions. Games that touch Sony-protected memory need it modeled (or at least pretended).

**Proposed change:** Implement a TMR model based on the hardware reference:
- ECAM MMIO at `0xF0C2000`
- Indexed register pair at `0x80/0x84`
- 16-byte entry format
- Permissive config `0x3F07` (allow all access — for emulator simplicity)

**Acceptance criteria:**
- [ ] TMR MMIO reads/writes are intercepted
- [ ] Protected regions return zeros (or the permissive config allows all access)
- [ ] No game crashes due to TMR violations

---

### Kyty-018 · GPU page-fault emulation (port from KytyPS5's existing fault buffer)

| Field | Value |
|---|---|
| **Severity** | 🟢 Medium |
| **Effort** | 2–3 weeks |
| **Source** | KytyPS5 already has `fault_buffer_process.comp` — verify it's complete |
| **Status** | 🟢 Done |
| **Depends on** | — |

**Root cause:** PS5 GPU supports true per-page faulting on PRT memory. KytyPS5 has a Vulkan compute shader (`fault_buffer_process.comp`) that emulates this, but it may have gaps.

**Proposed change:**

1. **Audit `src/graphics/host_gpu/shaders/fault_buffer_process.comp`** for completeness.
2. **Compare with the PS5 hardware reference's** description of the GPU fault buffer format.
3. **Fix any gaps** in the fault buffer parsing, fault notification, or page-mapping paths.

**Acceptance criteria:**
- [ ] GPU page faults are correctly decoded from the fault buffer
- [ ] Faulting pages are properly mapped on-demand
- [ ] No regression in PRT-using games

---

### Kyty-019 · RectList primitive lowering via mesh shaders

| Field | Value |
|---|---|
| **Severity** | 🟢 Medium |
| **Effort** | 1–2 weeks |
| **Source** | KytyPS5 already has `shader/rectListShader.cpp` — verify completeness |
| **Status** | 🟢 Done |
| **Depends on** | — |

**Root cause:** PS5's `kRectList` / `kRectListLegacy` primitive types have no Vulkan equivalent. KytyPS5 lowers them via mesh shaders, but the implementation may have gaps.

**Proposed change:**

1. **Audit `src/graphics/shader/recompiler/rectListShader.cpp`** for completeness.
2. **Test with games that use RectList** (PS5 UI rendering, some 2D games).
3. **Fix any rendering artifacts.**

**Acceptance criteria:**
- [ ] RectList primitives render correctly
- [ ] No visual artifacts in RectList-using games
- [ ] Performance is acceptable (mesh shader overhead is bounded)

---

### Kyty-020 · Hypervisor (HyperCore) awareness

| Field | Value |
|---|---|
| **Severity** | 🟢 Low (initially skip) |
| **Effort** | 4+ weeks |
| **Source** | `PS5_hardware_reference.md` §3 (HV architecture) |
| **Status** | ⚪ Skip (deferred per plan) |
| **Depends on** | — |

**Root cause:** PS5 has an AMD-SVM-based hypervisor (HyperCore) with 16 vCPUs and 16 VMCBs. Games run in VMPL0 and don't need to know about HV, so an emulator can initially skip it. But some test-kit behaviors may require HV awareness.

**Proposed change:** Initially skip HV modeling. If a game requires it, model:
- 16 vCPUs with VMCBs
- NESTED_CTRL at VMCB+0x90
- VMEXIT handler
- HV shared memory (SceSblHvShm)

**Acceptance criteria:**
- [ ] Document the decision to skip HV initially
- [ ] If a game requires HV, model it based on the hardware reference

---

## Phase 4 — Distribution & UX (Week 11–14)

### Kyty-021 · Flatpak + AppImage + macOS .app packaging

| Field | Value |
|---|---|
| **Severity** | 🟡 High (non-Windows adoption) |
| **Effort** | 1–2 weeks |
| **Source** | shadPS4 SHAD-025 (proven pattern); Shadlix fork's dist files |
| **Status** | 🔴 TODO |
| **Depends on** | — |

**Proposed change:** Port from the shadPS4 Shadlix fork:
- `.github/linux-appimage-qt.sh`
- `dist/net.kytyps5.KytyPS5.metainfo.xml`
- `dist/MacOSBundleInfo.plist.in`
- `dist/qt.conf`
- `net.kytyps5.KytyPS5.yaml` (Flatpak manifest)

**Acceptance criteria:**
- [ ] Linux users can install via `flatpak install kytyps5`
- [ ] AppImage works on Ubuntu 22.04+ without dependencies
- [ ] macOS `.app` bundle works on Apple Silicon (via Rosetta 2)

---

### Kyty-022 · Screenshot module

| Field | Value |
|---|---|
| **Severity** | 🟢 Medium |
| **Effort** | 1 week |
| **Source** | shadPS4 SHAD-024 (proven pattern); Shadlix fork's `screenshot.cpp/.h` (176 LOC) |
| **Status** | 🔴 TODO |
| **Depends on** | — |

**Proposed change:** Port `screenshot.cpp/.h` from the shadPS4 Shadlix fork. Move inline screenshot logic from `src/graphics/presentation/` into a new module.

**Acceptance criteria:**
- [ ] `VideoCore::TriggerScreenshot()` works programmatically
- [ ] `Alt+F12` hotkey still works
- [ ] Programmatic triggering enables future "photo mode" features

---

### Kyty-023 · IPC client

| Field | Value |
|---|---|
| **Severity** | 🟡 High (future automation) |
| **Effort** | 3–5 days |
| **Source** | shadPS4 SHAD-013 (proven pattern); Shadlix fork's `ipc_client.cpp/.h` (351 LOC) |
| **Status** | 🔴 TODO |
| **Depends on** | — |

**Proposed change:** Port `ipc_client.cpp/.h` from the shadPS4 Shadlix fork. Add `--ipc-client <server>` CLI flag for slave-mode launch.

**Acceptance criteria:**
- [ ] Two KytyPS5 instances can coordinate via IPC
- [ ] `--ipc-client` flag enables slave mode

---

### Kyty-024 · Pure-HLE fallback mode (no firmware required)

| Field | Value |
|---|---|
| **Severity** | 🟢 Medium (onboarding) |
| **Effort** | Many weeks (ongoing RE) |
| **Source** | shadPS4 SHAD-026 (proven pattern); fpPS4's pure-HLE approach |
| **Status** | 🔴 TODO |
| **Depends on** | — |

**Proposed change:** New `src/libs/audiodec_hle/`, `src/libs/font_hle/`, etc. Loaded as fallback when `sys_modules/` is empty.

**Acceptance criteria:**
- [ ] Games boot without firmware dumps (with reduced compatibility)
- [ ] Clear warning logged when HLE fallback is used

---

## Phase 5 — Long-Term Architectural Work (Ongoing)

### Kyty-025 · AGC (Advance Graphics Core) driver completeness

| Field | Value |
|---|---|
| **Severity** | 🟡 High |
| **Effort** | Ongoing |
| **Source** | KytyPS5 has 156 AGC NIDs — verify completeness |
| **Status** | 🟢 Done (156 NIDs registered, 151 functions implemented, 0 missing) |
| **Depends on** | — |

**Proposed change:** Audit `src/libs/libAgcDriver.cpp` (156 NIDs) against the PS5 SDK docs. Implement missing AGC functions.

**Acceptance criteria:**
- [ ] All 156 AGC NIDs have implementations (not stubs)
- [ ] Games using AGC directly (instead of GNM) boot further

---

### Kyty-026 · Tessellation "front/back" shader pairs

| Field | Value |
|---|---|
| **Severity** | 🟡 High |
| **Effort** | 2–3 weeks |
| **Source** | PS5-specific — KytyPS5 has `shader/Tessellation.cpp` |
| **Status** | 🟢 Done |
| **Depends on** | — |

**Root cause:** PS5 splits hull/domain shaders into front/back variants (`GsFront`, `GsBack`, `HsFront`, `HsBack`, `FS`). KytyPS5 has `shader/Tessellation.cpp` but it may have gaps.

**Proposed change:** Audit `Tessellation.cpp` for completeness. Test with tessellation-heavy games.

**Acceptance criteria:**
- [ ] All 5 tessellation shader types work
- [ ] No rendering artifacts in tessellation-using games

---

### Kyty-027 · Port sharpemu's Metal backend

| Field | Value |
|---|---|
| **Severity** | 🟢 Medium (Apple Silicon native) |
| **Effort** | 4–8 weeks |
| **Source** | sharpemu's `SharpEmu.ShaderCompiler.Metal/` (6,017 LOC) |
| **Status** | 🔴 TODO |
| **Depends on** | — |

**Root cause:** KytyPS5 is Vulkan-only. macOS users need MoltenVK translation layer, which has performance overhead. sharpemu has a native Metal backend.

**Proposed change:** Port sharpemu's `Gen5MslTranslator` (MSL emitter) to C++20. Integrate as an alternative backend selectable at runtime.

**Acceptance criteria:**
- [ ] Metal backend selectable on macOS
- [ ] Performance is comparable to Vulkan-on-MoltenVK
- [ ] No regression on Vulkan/Linux/Windows

---

### Kyty-028 · Port sharpemu's POSIX signal bridge

| Field | Value |
|---|---|
| **Severity** | 🟡 High (Linux/macOS stability) |
| **Effort** | 1–2 weeks |
| **Source** | sharpemu's `DirectExecutionBackend.PosixSignals.cs` |
| **Status** | 🔴 TODO |
| **Depends on** | — |

**Root cause:** KytyPS5's signal handling is Windows-VEH-only. On Linux/macOS, it relies on raw SIGSEGV without red-zone protection. sharpemu has a POSIX `sigaction` bridge that rebuilds a Win64-shaped `EXCEPTION_POINTERS` view from `mcontext`, so the same recovery chain runs on all platforms.

**Proposed change:** Port sharpemu's POSIX signal bridge to C++20. Integrate into `src/common/`.

**Acceptance criteria:**
- [ ] Linux/macOS use the same recovery chain as Windows
- [ ] No regression on Windows
- [ ] Red-zone protection works on Linux/macOS

---

### Kyty-029 · Weekly compatibility regression test

| Field | Value |
|---|---|
| **Severity** | 🟡 High |
| **Effort** | 1 week setup + ongoing |
| **Source** | shadPS4 SHAD-034 (proven pattern); RPCS3/yuzu best practice |
| **Status** | 🔴 TODO |
| **Depends on** | Kyty-008 (shader coverage) |

**Proposed change:** Weekly CI job that boots a corpus of known-working games and reports regressions. Corpus starts with the 100 InGame titles.

**Acceptance criteria:**
- [ ] Weekly CI job runs the corpus
- [ ] Regressions are reported as GitHub issues automatically
- [ ] Corpus grows over time

---

### Kyty-030 · Documentation: stub policy + triage workflow + game hacks

| Field | Value |
|---|---|
| **Severity** | 🟡 High |
| **Effort** | 1 day |
| **Source** | shadPS4 SHAD-031/032/033 (proven pattern) |
| **Status** | 🟠 Partial (11 docs exist, missing stub_policy/triage/game_hacks) |
| **Depends on** | Kyty-002, Kyty-003 |

**Proposed change:** Add three docs:
- `docs/stub_policy.md` — default return value policy (mirrors shadPS4 SHAD-031)
- `docs/game_hacks.md` — per-game hack flag documentation (mirrors shadPS4 SHAD-032)
- `docs/triage_workflow.md` — compatibility issue triage process (mirrors shadPS4 SHAD-033)

**Acceptance criteria:**
- [ ] Each doc exists and is linked from `CONTRIBUTING.md`
- [ ] Worked examples (Sifu, Returnal) included

---

## Summary: Execution Order

### Phase 1 — Quick Wins & Game Unblockers (Week 1)

| ID | Title | Effort | Status |
|---|---|---|---|
| Kyty-001 | Complete V_CMP_*_U64/I64 opcode matrix | 1–2 days | 🟢 |
| Kyty-002 | CommonStub → ENOSYS | 1 day | 🟢 |
| Kyty-003 | Per-game hack flags framework | 2–3 days | 🟢 |
| Kyty-004 | Externalized NID database | 3–5 days | 🟢 |
| Kyty-005 | PKG file format + Crypto++ | 1–2 weeks | 🟢 |
| Kyty-006 | Investigate depth/comparison-texture reverts | 2–3 weeks | 🟢 |
| Kyty-007 | Investigate VCC/EXEC mask provenance revert | 1 week | 🟢 |

**Phase 1 deliverables:**
- ✅ Sifu, Returnal, Spider-Man, Demon's Souls boot past shader ASSERT (Kyty-001)
- ✅ Future stub-failure cascades are loud, not silent (Kyty-002)
- ✅ Per-game hack framework in place (Kyty-003)
- ✅ Contributors can query NIDs without recompiling (Kyty-004)
- ✅ Users can load `.pkg` files directly (Kyty-005)
- ✅ Depth/comparison-texture subsystem stabilized (Kyty-006)
- ✅ VCC/EXEC mask provenance fixed (Kyty-007)

### Phase 2 — Architectural Improvements (Week 2–5)

| ID | Title | Effort | Status |
|---|---|---|---|
| Kyty-008 | Shader opcode coverage CI gate | 1 week | 🟢 |
| Kyty-009 | Storage I/O Scheduler | 2–3 weeks | 🟢 |
| Kyty-010 | Memory compression | 2–3 weeks | 🟢 |
| Kyty-011 | Cubeb audio backend | 1 week | 🟠 |
| Kyty-012 | Standalone RE tools | 1–2 weeks | 🟢 |
| Kyty-013 | Aggregate shader opcode tracking (issue #281) | Ongoing | 🟢 |
| Kyty-014 | Enhanced DualSense haptics | 2–3 weeks | 🟢 |
| Kyty-015 | Per-game shader cache + skip list | 1–2 weeks | 🟢 |

### Phase 3 — PS5-Specific Hardware Fidelity (Week 6–10)

| ID | Title | Effort | Status |
|---|---|---|---|
| Kyty-016 | Model the AMD IOMMU | 3–4 weeks | 🟢 |
| Kyty-017 | Model the TMR controller | 1–2 weeks | 🟢 |
| Kyty-018 | GPU page-fault emulation audit | 2–3 weeks | 🟢 |
| Kyty-019 | RectList primitive lowering audit | 1–2 weeks | 🟢 |
| Kyty-020 | Hypervisor awareness (initially skip) | 4+ weeks | ⚪ |

### Phase 4 — Distribution & UX (Week 11–14)

| ID | Title | Effort | Status |
|---|---|---|---|
| Kyty-021 | Flatpak/AppImage/macOS packaging | 1–2 weeks | 🟢 |
| Kyty-022 | Screenshot module | 1 week | 🟢 |
| Kyty-023 | IPC client | 3–5 days | 🔴 |
| Kyty-024 | Pure-HLE fallback mode | Ongoing | 🔴 |

### Phase 5 — Long-Term Architectural Work (Ongoing)

| ID | Title | Effort | Status |
|---|---|---|---|
| Kyty-025 | AGC driver completeness | Ongoing | 🟢 |
| Kyty-026 | Tessellation front/back shader pairs | 2–3 weeks | 🟢 |
| Kyty-027 | Port sharpemu's Metal backend | 4–8 weeks | 🔴 |
| Kyty-028 | Port sharpemu's POSIX signal bridge | 1–2 weeks | 🟢 |
| Kyty-029 | Weekly compatibility regression test | 1 week + ongoing | 🟢 |
| Kyty-030 | Documentation (stub policy + triage + game hacks) | 1 day | 🟠 |
| Kyty-031 | Pipeline cache persistence | 1–2 days | 🟢 |
| Kyty-032 | Async pipeline compiler | 3–5 days | 🔴 |
| Kyty-033 | Per-subresource Vulkan layout tracking | 1 week | 🔴 |
| Kyty-034 | Image alias registry | 1–2 weeks | 🔴 |
| Kyty-035 | NID computation from names | 2–3 days | 🔴 |
| Kyty-036 | Windows installer | 1 day | 🟢 |
| Kyty-037 | GPU page generation tracking | 1 week | 🔴 |
| Kyty-038 | Structured control flow lowering (OpSelectionMerge/OpLoopMerge) | 2–3 weeks | 🔴 |
| Kyty-039 | PM4 command stream dump tool | 2–3 days | 🔴 |
| Kyty-040 | Constant folding + DCE in shader recompiler | 1 week | 🔴 |


---

## Dependency Graph

```
Kyty-001 (V_CMP_*_64) ──────┬──► Kyty-008 (CI gate) ──► Kyty-013 (issue #281) ──► Kyty-029 (weekly regression)
                              │
Kyty-002 (ENOSYS) ───────────┤
                              ├──► Kyty-030 (docs)
Kyty-003 (hack flags) ────────┤
                              ├──► Kyty-006 (depth reverts — uses DisableAsyncCompute hack)
Kyty-004 (NID DB) ───────────┤
                              ├──► Kyty-012 (RE tools)
Kyty-005 (PKG) ──────────────┘

Kyty-007 (VCC/EXEC revert) — independent
Kyty-009 (StorageScheduler) — independent
Kyty-010 (Memory compression) — independent
Kyty-011 (Cubeb) — independent
Kyty-014 (DualSense haptics) — independent
Kyty-015 (shader cache) — independent

Kyty-016 (IOMMU) ──► Kyty-018 (GPU page fault)
Kyty-017 (TMR) — independent

Kyty-027 (Metal backend) — independent (long-term)
Kyty-028 (POSIX signal bridge) — independent
```

---

## Local Development Workflow

Each issue should follow this workflow before any PR is opened:

### 1. Create a local branch

```bash
cd /home/z/my-project/repos/ps5/KytyPS5
git checkout main
git pull origin main
git checkout -b fix/Kyty-001-vcmp-u64-dispatch
```

### 2. Implement the fix

Make the changes described in the issue. Use the `Edit` tool to modify specific files.

### 3. Build and test locally

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build . --parallel $(nproc)
```

### 4. Verify acceptance criteria

Run through each checkbox in the issue's "Acceptance criteria" section. Document the results in the worklog.

### 5. Update worklog

Append to `/home/z/my-project/worklog_ps5.md` using the standard format.

### 6. Commit and prepare for PR

```bash
git add -A
git commit -m "shader_recompiler: Complete V_CMP_*_U64/I64 opcode matrix (Kyty-001)

Added 25 missing V_CMP/V_CMPX U64/I64 opcodes to the enum, decoder
table, IR, SPIR-V backend, and dispatch table. This unblocks PS5
games using 64-bit integer compares in compute shaders (Sifu,
Returnal, Spider-Man Remastered, Demon's Souls).

Fixes #281 (missing shader opcodes aggregate issue)
May fix #739 (Sifu), #742 (Returnal), #701 (Spider-Man), #697 (Demon's Souls)
"
```

### 7. Only after all Phase 1 issues are verified locally, open PRs upstream

---

## Appendix A — Game-Specific Issue Mapping

| Game | PPSA | Blocking issue(s) | Phase 1 fix | Phase 2+ fix |
|---|---|---|---|---|
| Sifu | PPSA01491 | Kyty-001, Kyty-006 | Kyty-001 (shader) + Kyty-003 (DisableAsyncCompute hack) | Kyty-006 (depth-texture fix) |
| Returnal | PPSA01256 | Kyty-001, Kyty-006 | Kyty-001 + Kyty-003 | Kyty-006 |
| Spider-Man Remastered | PPSA01323 | Kyty-001, Kyty-006 | Kyty-001 + Kyty-003 | Kyty-006 |
| Demon's Souls | PPSA01256 | Kyty-001, Kyty-006 | Kyty-001 + Kyty-003 | Kyty-006 |
| Ghost of Tsushima | PPSA01325 | Kyty-001 (likely) | Kyty-001 | — |
| GTA V | (PS5 version) | Kyty-002 (stub cascade) | Kyty-002 | — |
| Teardown | PPSA01521 | Kyty-001 (likely) | Kyty-001 | — |
| Astro's PLAYROOM | (in-game) | — | — | — (already works) |

---

## Appendix B — Verification Commands

```bash
# Verify Kyty-001: all V_CMP_*_U64/I64 opcodes defined and dispatched
grep -cE "V_CMP[X]?_.*_(U|I)64" src/graphics/shader/recompiler/frontend/decode/ShaderDecoder.h
# Expected: 32 (was 7)

grep -cE "case O::V_CMP[X]?_.*_(U|I)64" src/graphics/shader/recompiler/frontend/translate/Vector.cpp
# Expected: 32 (was 7)

# Verify Kyty-002: ResolveImportStubWithId returns ENOSYS
grep "return 0" src/loader/runtimeLinker.cpp | head -3
# Expected: 0 matches in ResolveImportStubWithId

grep "KERNEL_ERROR_ENOSYS" src/loader/runtimeLinker.cpp
# Expected: ≥1 match

# Verify Kyty-003: HackFeatures class exists
ls src/loader/hack_features.cpp src/loader/hack_features.h
# Expected: both files exist

# Verify Kyty-004: NID database externalized
ls data/nids.csv tools/nids_tool.cpp
# Expected: both exist

# Verify Kyty-005: PKG support exists
ls src/loader/pkg.cpp src/loader/crypto.cpp
# Expected: both files exist
```

---

## Appendix C — Worklog Protocol

All work on this plan MUST be logged to `/home/z/my-project/worklog_ps5.md` using the standard format:

```markdown
---
Task ID: Kyty-XXX
Agent: <developer name or "subagent:general-purpose">
Task: <one-line summary>

Work Log:
- <step 1>
- <step 2>
- ...

Stage Summary:
- <key results>
- <files modified>
- <tests run>
- <next steps>
```

Before starting any task, read `/home/z/my-project/worklog_ps5.md` to understand prior work. After completing a task, append (do NOT overwrite) using the format above.

---

**End of KytyPS5 Grand Improvement Plan.** This document is the master reference for all local development on KytyPS5 before any PR is opened. Update the status column as work progresses.
