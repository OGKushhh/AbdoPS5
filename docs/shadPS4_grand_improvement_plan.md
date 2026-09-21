# shadPS4 Grand Improvement Plan — Local Issue Tracker

> **Purpose:** A single working document compiling every issue identified across all prior analysis (deep comparison, RE3/FIFA16 root-cause, fork regression audit). Each issue has a concrete fix proposal, affected files with line numbers, acceptance criteria, and effort estimate. This is the master document for driving local development before any PR is opened.
>
> **Sources compiled:**
> - `shadPS4_deep_comparison.md` (3-repo comparison + 26-item roadmap)
> - `RE3_FIFA16_root_cause_analysis.md` (game-specific root causes)
> - Direct source-code inspection of `shadPS4` upstream `main` and `shadPS4-diegolix29` `Shadlix` branch
>
> **Status legend:** 🔴 TODO · 🟡 In Progress · 🟢 Done · ⚪ Blocked / External

---

## 0. Executive Summary

| Statistic | Value |
|---|---|
| Total issues compiled | **34** |
| Critical (game-blocking) | 6 |
| High (UX / feature gap) | 14 |
| Medium (architectural / long-tail) | 9 |
| Low (refactor / cleanup) | 5 |
| Total estimated effort | **~22–32 weeks of focused work** |
| Issues fixable in <1 day | 7 |
| Issues fixable in <1 week | 14 |
| Issues requiring ongoing RE | 5 |

The plan is organized into **5 phases** that sequence dependencies correctly and front-load the highest-impact, lowest-effort fixes. Phase 1 alone (1–2 weeks) unblocks RE3 Remake and several other RE Engine titles.

---

## Phase 1 — Quick Wins & Game Unblockers (Week 1–2)

### SHAD-001 · Complete the `V_CMP_*_U64` opcode dispatch table

| Field | Value |
|---|---|
| **Severity** | 🔴 Critical |
| **Effort** | 30 minutes |
| **Source** | RE3 root-cause analysis ([#299](https://github.com/shadps4-compatibility/shadps4-game-compatibility/issues/299)) |
| **Status** | 🔴 TODO |
| **Depends on** | — |
| **Unblocks** | RE3 Remake, RE2 Remake, RE7, RE Village, DMC5, Monster Hunter World (every RE Engine title) |

**Root cause:** The GCN ISA defines 16 `V_CMP_*_U64` / `V_CMPX_*_U64` opcodes (8 base + 8 exec-setting). The translator function `Translator::V_CMP_U64` in `src/shader_recompiler/frontend/translate/vector_alu.cpp:1262` is **already complete** — it handles all 8 `ConditionOp` values. However, the **outer opcode dispatcher** (the giant `switch(inst.opcode)` in the same file) only has case entries for **7 of the 16** U64 opcodes:

```cpp
// src/shader_recompiler/frontend/translate/vector_alu.cpp:376-393 (CURRENT STATE)
case Opcode::V_CMPX_EQ_I64:  return V_CMP_U64(ConditionOp::EQ, true, true, inst);
case Opcode::V_CMP_EQ_U64:  return V_CMP_U64(ConditionOp::EQ, false, false, inst);
case Opcode::V_CMP_NE_U64:  return V_CMP_U64(ConditionOp::LG, false, false, inst);
case Opcode::V_CMP_GT_U64:  return V_CMP_U64(ConditionOp::GT, false, false, inst);
case Opcode::V_CMP_LT_U64:  return V_CMP_U64(ConditionOp::LT, false, false, inst);
case Opcode::V_CMPX_EQ_U64: return V_CMP_U64(ConditionOp::EQ, false, true, inst);
case Opcode::V_CMPX_LG_U64: return V_CMP_U64(ConditionOp::LG, false, true, inst);
// *** 9 cases missing ***
```

The 9 missing cases (verified against `src/shader_recompiler/frontend/opcodes.h`):

```cpp
// V_CMP_*_U64 (base, non-exec-setting) — missing 4
case Opcode::V_CMP_F_U64:    return V_CMP_U64(ConditionOp::F,   false, false, inst);
case Opcode::V_CMP_LE_U64:   return V_CMP_U64(ConditionOp::LE,  false, false, inst);
case Opcode::V_CMP_GE_U64:   return V_CMP_U64(ConditionOp::GE,  false, false, inst);
case Opcode::V_CMP_TRU_U64:  return V_CMP_U64(ConditionOp::TRU, false, false, inst);
// Also the I64 (signed) variants — missing 4
case Opcode::V_CMP_F_I64:    return V_CMP_U64(ConditionOp::F,   true,  false, inst);
case Opcode::V_CMP_LT_I64:   return V_CMP_U64(ConditionOp::LT,  true,  false, inst);
case Opcode::V_CMP_LE_I64:   return V_CMP_U64(ConditionOp::LE,  true,  false, inst);
case Opcode::V_CMP_GE_I64:   return V_CMP_U64(ConditionOp::GE,  true,  false, inst);
case Opcode::V_CMP_TRU_I64:  return V_CMP_U64(ConditionOp::TRU, true,  false, inst);
// V_CMPX_*_U64 / V_CMPX_*_I64 (exec-setting) — missing ~7
case Opcode::V_CMPX_F_U64:   return V_CMP_U64(ConditionOp::F,   false, true, inst);
case Opcode::V_CMPX_LT_U64:  return V_CMP_U64(ConditionOp::LT,  false, true, inst);
case Opcode::V_CMPX_LE_U64:  return V_CMP_U64(ConditionOp::LE,  false, true, inst);
case Opcode::V_CMPX_GT_U64:  return V_CMP_U64(ConditionOp::GT,  false, true, inst);
case Opcode::V_CMPX_GE_U64:  return V_CMP_U64(ConditionOp::GE,  false, true, inst);
case Opcode::V_CMPX_TRU_U64: return V_CMP_U64(ConditionOp::TRU, false, true, inst);
case Opcode::V_CMPX_F_I64:    return V_CMP_U64(ConditionOp::F,   true,  true, inst);
case Opcode::V_CMPX_LT_I64:   return V_CMP_U64(ConditionOp::LT,  true,  true, inst);
case Opcode::V_CMPX_LE_I64:   return V_CMP_U64(ConditionOp::LE,  true,  true, inst);
case Opcode::V_CMPX_GT_I64:   return V_CMP_U64(ConditionOp::GT,  true,  true, inst);
case Opcode::V_CMPX_GE_I64:   return V_CMP_U64(ConditionOp::GE,  true,  true, inst);
case Opcode::V_CMPX_TRU_I64:  return V_CMP_U64(ConditionOp::TRU, true,  true, inst);
```

**Affected files:**
- `src/shader_recompiler/frontend/translate/vector_alu.cpp` (add ~13 case entries around line 393)

**Acceptance criteria:**
- [ ] All 16 `V_CMP_*_U64` opcodes have dispatch entries
- [ ] All 16 `V_CMP_*_I64` opcodes have dispatch entries (signed variants — also currently incomplete)
- [ ] RE3 Remake (CUSA14168) boots past the shader compiler ASSERT
- [ ] New unit test in `tests/gcn/translator.cpp` compiles a synthetic shader for each of the 32 opcodes and verifies no `UNREACHABLE()` fires

**Test plan:** Add a test that iterates every `Opcode::V_CMP_*_U64` and `Opcode::V_CMP_*_I64` value from the enum and verifies each produces a valid SPIR-V module.

---

### SHAD-002 · Change `CommonStub` default return from `0` to `SCE_KERNEL_ERROR_ENOSYS`

| Field | Value |
|---|---|
| **Severity** | 🔴 Critical (cross-cutting) |
| **Effort** | 1 day |
| **Source** | RE3/FIFA16 analysis §5.1 |
| **Status** | 🔴 TODO |
| **Depends on** | — |
| **Unblocks** | Easier diagnosis of every "stub lies about success" cascade |

**Root cause:** `src/core/aerolib/stubs.cpp:42` always returns `0`:

```cpp
static u64 CommonStub(int stub_index, void* addr) {
    auto entry = stub_nids[stub_index];
    if (entry) {
        LOG_ERROR(Core, "Stub: {} (nid: {}) called, returning zero to {}", entry->name, entry->nid, addr);
    } ...
    return 0;  // <-- ALWAYS returns success
}
```

When a game calls an unimplemented syscall (e.g. `sceKernelMlockall` — see SHAD-003), the stub returns `0` (= `SCE_OK` = success). The game then proceeds assuming the syscall worked, dereferences pointers that "should" have been initialized, and crashes much later with a NULL pointer dereference. The crash signature points at the dereference site, not the stub, making diagnosis extremely difficult.

**Proposed change:**

```cpp
// src/core/aerolib/stubs.cpp
static u64 CommonStub(int stub_index, void* addr) {
    auto entry = stub_nids[stub_index];
    if (entry) {
        LOG_WARNING(Core, "Stub: {} (nid: {}) called, returning ENOSYS to {}",
                    entry->name, entry->nid, addr);
    } else {
        LOG_WARNING(Core, "Stub: Unknown (nid: {}) called, returning ENOSYS to {}",
                    stub_nids_unknown[stub_index], addr);
    }
    return SCE_KERNEL_ERROR_ENOSYS;  // -2147418110 ("function not implemented")
}
```

For specific stubs that are known-safe to no-op (e.g. logging functions, no-op telemetry), override them with explicit implementations that return `0`. The default should be loud failure.

**Affected files:**
- `src/core/aerolib/stubs.cpp` (change `return 0` to `return SCE_KERNEL_ERROR_ENOSYS` at line 42)
- Audit `src/core/aerolib/aerolib.inl` for stubs that should explicitly return 0 (logging, telemetry, etc.) — add `LIB_FUNCTION` overrides for those few

**Acceptance criteria:**
- [ ] Default `CommonStub` returns `SCE_KERNEL_ERROR_ENOSYS`
- [ ] All existing games that worked before still work (regression test against Bloodborne, DS Remastered, RDR)
- [ ] Games that were silently broken now produce a clear "ENOSYS from <NID>" log entry that points to the root cause
- [ ] Documentation added to `documents/` explaining the stub-return-value policy

**Test plan:** Boot each known-working game (Bloodborne, DS Remastered, RDR) and verify no regression. Boot a known-broken game (FIFA 16) and verify the log now contains a clear ENOSYS trace.

---

### SHAD-003 · Implement `sceKernelMlockall` properly

| Field | Value |
|---|---|
| **Severity** | 🔴 Critical (FIFA 16 cascade root cause) |
| **Effort** | 1 day |
| **Source** | FIFA 16 root-cause analysis ([#1209](https://github.com/shadps4-compatibility/shadps4-game-compatibility/issues/1209)) |
| **Status** | 🔴 TODO |
| **Depends on** | — |

**Root cause:** `sceKernelMlockall` is still a stub in `src/core/aerolib/aerolib.inl:44574`:

```
STUB("EfqmKkirJF0", sceKernelMlockall)
```

The PS4's `sceKernelMlockall(flags)` wraps POSIX `mlockall(2)`, which locks all mapped pages into RAM (prevents swap). On a real PS4, this is critical for:
- Real-time audio buffers (FIFA's audio mixing)
- Gameplay-critical memory (physics scratch, animation poses)

When shadPS4's stub returns `0` (success), FIFA's init code computes pointers into the (supposedly-locked) buffer. Because nothing was actually locked, the next memory operation can hit a non-backed page → NULL deref cascade.

**Proposed change:** Implement `sceKernelMlockall` in `src/core/libraries/kernel/` as a no-op that returns success — **but only after SHAD-002 is done**, so the default is loud failure and this is an explicit opt-in to "we know mlockall is safe to no-op on a host OS that has its own pager."

```cpp
// src/core/libraries/kernel/memory.cpp (or new file)
s32 PS4_SYSV_ABI sceKernelMlockall(s32 flags) {
    // On the host OS, mlockall is unnecessary — the host kernel manages paging.
    // We return success because games expect this to work, but we don't actually
    // need to lock pages (the host OS won't swap guest memory under normal load).
    LOG_TRACE(Kernel_Memory, "called flags={}, no-op (host OS handles paging)", flags);
    return SCE_OK;
}
```

**Affected files:**
- `src/core/libraries/kernel/memory.cpp` (add implementation)
- `src/core/libraries/kernel/libsce_kernel.cpp` (register `LIB_FUNCTION`)
- Remove `STUB("EfqmKkirJF0", sceKernelMlockall)` from `src/core/aerolib/aerolib.inl:44574`

**Acceptance criteria:**
- [ ] `sceKernelMlockall` returns `SCE_OK` without going through `CommonStub`
- [ ] FIFA 16 boot no longer produces the `stubs.cpp:42 CommonStub: Stub: sceKernelMlockall` log entry
- [ ] No regression in other games

---

### SHAD-004 · Per-game hack flags framework (fpPS4 pattern)

| Field | Value |
|---|---|
| **Severity** | 🟡 High |
| **Effort** | 2–3 days |
| **Source** | fpPS4's `-h` hack flags; deep comparison P0.1 |
| **Status** | 🔴 TODO |
| **Depends on** | — |

**Root cause:** When a single bug blocks a game, users have no escape hatch. fpPS4 has 8 proven hack flags: `DEPTH_DISABLE_HACK`, `COMPUTE_DISABLE_HACK`, `MEMORY_BOUND_HACK`, `IMAGE_TEST_HACK`, `IMAGE_LOAD_HACK`, `DISABLE_SRGB_HACK`, `DISABLE_FMV_HACK`, `SKIP_UNKNOW_TILING`.

**Proposed change:** Add a `GameHacks` bitmask to `EmulatorSettings` and a `--enable-hack <name>` CLI flag.

```cpp
// src/core/emulator_settings.h
enum class GameHack : u32 {
    None = 0,
    DepthDisable = 1 << 0,
    ComputeDisable = 1 << 1,
    MemoryBound = 1 << 2,
    ImageTest = 1 << 3,
    ImageLoadNoReload = 1 << 4,
    DisableSRGB = 1 << 5,
    DisableFMV = 1 << 6,
    SkipUnknownTiling = 1 << 7,
    SkipShaderAssert = 1 << 8,  // SHAD-001 workaround for users
};

DECLARE_ENUM_FLAG_OPERATORS(GameHack)

class EmulatorSettingsImpl {
    GameHack game_hacks_{GameHack::None};
    void SetGameHack(GameHack hack, bool enable);
    bool IsGameHackEnabled(GameHack hack) const;
};
```

```cpp
// src/main.cpp — new CLI flag
app.add_option("--enable-hack", hack_names, "Enable a per-game hack (comma-separated)");
// Parse names to GameHack bitmask
```

**Affected files:**
- `src/core/emulator_settings.h/.cpp` — add `GameHack` enum + getters/setters
- `src/main.cpp` — add `--enable-hack` flag
- `src/video_core/renderer_vulkan/vk_rasterizer.cpp` — gate depth/compute paths on flags
- `src/video_core/texture_cache/image.cpp` — gate image reload on `ImageLoadNoReload`
- `src/video_core/renderer_vulkan/vk_presenter.cpp` — gate sRGB on `DisableSRGB`

**Acceptance criteria:**
- [ ] `shadPS4 --enable-hack SKIP_SHADER_ASSERT CUSA14168` lets RE3 boot past the shader ASSERT (with visual artifacts) before SHAD-001 is merged
- [ ] `shadPS4 --enable-hack IMAGE_LOAD_HACK CUSA00001` speeds up games that re-upload textures unnecessarily
- [ ] Per-game config file (`user/config/<title_id>.ini`) persists hack flags

---

### SHAD-005 · Per-game hack auto-detection by CUSA serial (fork pattern)

| Field | Value |
|---|---|
| **Severity** | 🟡 High |
| **Effort** | 2–3 days |
| **Source** | Shadlix fork `src/common/hack_features.cpp`; deep comparison P0.2 |
| **Status** | 🔴 TODO |
| **Depends on** | SHAD-004 |

**Root cause:** Users shouldn't need to know which hack to enable. The fork auto-detects games by CUSA ID and applies workarounds automatically. The fork made The Order: 1886 playable this way (commit `cb0d7cd 1886 hacks perf and graphics (playable)`).

**Proposed change:** Port the fork's `src/common/hack_features.cpp/.h` (24 LOC) and make it data-driven.

```cpp
// src/common/hack_features.h
class HackFeatures {
public:
    static void Init(std::string_view game_serial);
    static bool HasHack(GameHack hack);  // O(1) lookup
private:
    static std::unordered_map<std::string, GameHack> game_hack_map_;
};
```

```cpp
// src/common/hack_features.cpp — data-driven from data/game_hacks.json
void HackFeatures::Init(std::string_view game_serial) {
    static const std::unordered_map<std::string, GameHack> hardcoded = {
        {"CUSA00035", GameHack::SkipShaderAssert | GameHack::DepthDisable},  // The Order: 1886
        {"CUSA00076", GameHack::SkipShaderAssert | GameHack::DepthDisable},
        {"CUSA00100", GameHack::SkipShaderAssert | GameHack::DepthDisable},
        // RE Engine games may benefit from ComputeDisable during RE3 debugging
        {"CUSA14168", GameHack::SkipShaderAssert},  // RE3 Remake — until SHAD-001 is verified
    };
    // Also load data/game_hacks.json for community-contributed overrides
}
```

**Affected files:**
- New `src/common/hack_features.cpp/.h`
- `src/core/emulator_state.cpp` — call `HackFeatures::Init(serial)` after `param.sfo` parse
- New `data/game_hacks.json` — community-editable database

**Acceptance criteria:**
- [ ] The Order: 1886 (CUSA00035/076/100) auto-applies `SkipShaderAssert | DepthDisable` and boots to a playable state
- [ ] `data/game_hacks.json` is loaded at startup and overrides the hardcoded map
- [ ] Per-game `.ini` config (from SHAD-004) overrides both

---

### SHAD-006 · Externalize NID database as loadable CSV

| Field | Value |
|---|---|
| **Severity** | 🟡 High (contributor UX) |
| **Effort** | 3–5 days |
| **Source** | fpPS4's `tools/ps4libdoc/`; deep comparison P0.4 |
| **Status** | 🔴 TODO |
| **Depends on** | — |

**Root cause:** `src/core/aerolib/aerolib.inl` is a 121,000+ line file compiled into the binary. Contributors can't extend the NID database without recompiling.

**Proposed change:**

1. Generate `data/nids.csv` from `aerolib.inl` at build time (build script reads the `STUB(...)` lines and emits CSV).
2. At runtime, load `data/nids.csv` into an in-memory hash map.
3. Add a `shadps4-nids` standalone CLI tool that prints NID → name mappings.
4. Add `--dump-nids` flag to the main binary.

```csv
# data/nids.csv (excerpt)
nid,name
EfqmKkirJF0,sceKernelMlockall
g8cM39EUZ6o,sceSysmoduleLoadModule
...
```

**Affected files:**
- New `scripts/gen_nids_csv.py` — parses `aerolib.inl` and emits `data/nids.csv`
- New `src/common/nid_database.cpp/.h` — runtime loader
- `src/core/aerolib/stubs.cpp` — use the runtime database instead of the compiled-in table
- New `tools/nids_tool.cpp` — standalone CLI
- `CMakeLists.txt` — add build target for `nids.csv` generation

**Acceptance criteria:**
- [ ] `data/nids.csv` is generated at build time
- [ ] Runtime loads `nids.csv` from `data/` directory (or `--nids-db <path>`)
- [ ] `shadps4-nids` tool prints "NID EfqmKkirJF0 = sceKernelMlockall"
- [ ] Existing stub resolution still works (no regression)
- [ ] Users can override `nids.csv` without recompiling

---

### SHAD-007 · PKG file format + Crypto++ decryption

| Field | Value |
|---|---|
| **Severity** | 🟡 High (UX) |
| **Effort** | 1–2 weeks |
| **Source** | Shadlix fork `src/core/file_format/pkg.*` + `src/core/crypto/*`; deep comparison P0.5 |
| **Status** | 🔴 TODO |
| **Depends on** | — |

**Root cause:** Users must extract `.pkg` files manually with separate tools. The fork can decrypt and mount `.pkg` files directly.

**Proposed change:** Port from the fork:
- `src/core/crypto/crypto.cpp/.h` (216+63 LOC)
- `src/core/crypto/keys.h` (304 LOC)
- `src/core/file_format/pkg.cpp/.h` (468+172 LOC)
- `src/core/file_format/pkg_type.cpp/.h` (638+10 LOC)
- `externals/cryptopp` + `externals/cryptopp-cmake`

Add `--mount-pkg <path>` CLI flag. Integrate with `src/core/file_sys/backends/`.

**Affected files (new):**
- `src/core/crypto/crypto.cpp/.h`
- `src/core/crypto/keys.h`
- `src/core/file_format/pkg.cpp/.h`
- `src/core/file_format/pkg_type.cpp/.h`
- `externals/cryptopp/` (vendored)

**Affected files (modified):**
- `src/main.cpp` — add `--mount-pkg` flag
- `src/core/file_sys/fs.cpp` — add PKG backend
- `CMakeLists.txt` — add Crypto++ dependency

**Acceptance criteria:**
- [ ] `shadPS4 --mount-pkg /path/to/game.pkg` boots the game
- [ ] All PKG content types (Game, Patch, Remaster, Theme, Avatar) supported
- [ ] Standalone `shadps4-pkg-extract` tool extracts a PKG to a folder (depends on SHAD-006's tools framework)
- [ ] No regression in existing folder-based game loading

---

### SHAD-008 · ZArchive (`.zar`) filesystem

| Field | Value |
|---|---|
| **Severity** | 🟡 High (UX) |
| **Effort** | 1 week |
| **Source** | Shadlix fork `src/common/zar_fs.cpp/.h`; deep comparison P0.6 |
| **Status** | 🔴 TODO |
| **Depends on** | — |

**Root cause:** `.zar` is the de-facto standard for compressed PS4 game dumps. Upstream only supports folders and extracted PFS.

**Proposed change:** Port from the fork:
- `src/common/zar_fs.cpp/.h` (497 LOC)

Integrate with `Common::FS::FindGameByID` in `src/main.cpp:247` so `.zar` archives are detected automatically when scanning game install folders.

**Affected files (new):**
- `src/common/zar_fs.cpp/.h`

**Affected files (modified):**
- `src/main.cpp` — `archive_component_exists` lambda already handles `.zar` paths; verify the integration works end-to-end
- `src/common/path_util.cpp` — extend `FindGameByID` to detect `.zar` archives

**Acceptance criteria:**
- [ ] `shadPS4 /games/CUSA01234.zar` boots the game
- [ ] Game-folder scanner detects `.zar` archives in install directories
- [ ] Inner paths (e.g. `/games/CUSA01234.zar/sce_sys/param.sfo`) work transparently

---

## Phase 2 — Architectural Improvements (Week 3–6)

### SHAD-009 · Fix `page_manager` "Tracking non-GPU memory" assertion (RE3 secondary crash)

| Field | Value |
|---|---|
| **Severity** | 🔴 Critical (RE3 Remake) |
| **Effort** | 2–3 weeks |
| **Source** | RE3 root-cause analysis §1.3 |
| **Status** | 🔴 TODO |
| **Depends on** | SHAD-001 (RE3 must boot past the shader ASSERT first) |

**Root cause:** `src/video_core/page_manager.cpp:130-140` warns when the rasterizer's `IsMapped()` returns false for a memory region the GPU is trying to track. The warning escalates to an ASSERT in some configurations.

RE Engine bypasses the standard `libSceGnmDriver` and constructs PM4 packets directly into manually-allocated buffers. Because shadPS4's HLE layer doesn't intercept this manual path, `PageManager::OnGpuMap` is never called for these buffers, and the GPU memory tracker doesn't know about them.

**Proposed change:**

1. Audit every `libSceGnmDriver` HLE function that registers a graphics resource and ensure each calls `PageManager::OnGpuMap`. Focus on:
   - `sceGnmMapMemory`
   - `sceGnmRegisterOwner`
   - `sceGnmRegisterResource`
   - `sceGnmSubmitCommandBuffers` (the entry point for RE Engine's manual PM4)

2. For the warning at `page_manager.cpp:135`, change the policy from "warn and continue" to "warn and auto-register the region as GPU-mapped" — this matches the PS4's actual behavior (any memory the GPU can address is implicitly GPU-mapped).

3. Make `--userfaultfd` the default on Linux (it handles this case correctly because it tracks all guest writes, not just the ones the HLE layer reports).

**Affected files:**
- `src/video_core/page_manager.cpp` — change warning policy at line 130
- `src/core/libraries/gnmdriver/gnmdriver.cpp` — audit and add `OnGpuMap` calls
- `src/main.cpp` — enable `userfaultfd` by default on Linux (with fallback to signal-based if unavailable)

**Acceptance criteria:**
- [ ] RE3 Remake boots past the `page_manager` ASSERT
- [ ] No regression in games that worked before
- [ ] The `LOG_WARNING` at `page_manager.cpp:135` fires less frequently
- [ ] On Linux with `--userfaultfd`, the warning never fires

---

### SHAD-010 · Storage I/O Scheduler

| Field | Value |
|---|---|
| **Severity** | 🟡 High (game bug fixes) |
| **Effort** | 2–3 weeks |
| **Source** | Shadlix fork `src/core/file_sys/storage_scheduler.cpp/.h` (882 LOC); deep comparison P1.1 |
| **Status** | 🔴 TODO |
| **Depends on** | — |

**Root cause:** Many PS4 games load assets at cadences timed to the PS4's stock HDD. On fast NVMe, the streaming logic breaks (texture pop-in, anti-cheat false positives).

**Proposed change:** Port `storage_scheduler.cpp/.h` from the fork. Wire into `core/file_sys/file.cpp`'s read path. Expose `--storage-bandwidth 75|100|125|0` CLI flag.

**Affected files (new):**
- `src/core/file_sys/storage_scheduler.cpp/.h`

**Affected files (modified):**
- `src/core/file_sys/file.cpp` — route reads through scheduler
- `src/main.cpp` — add `--storage-bandwidth` flag
- `src/core/devtools/widget/frame_graph.cpp` — display scheduler stats

**Acceptance criteria:**
- [ ] `--storage-bandwidth 75` throttles reads to PS4 HDD speed
- [ ] Stats (bytes_read, chunks, sequential_chunks, modeled_wait_ns) are visible in devtools
- [ ] Games that previously had texture pop-in glitches show improvement
- [ ] `--storage-bandwidth 0` (default) preserves current behavior (no regression)

---

### SHAD-011 · Memory compression for low-RAM systems

| Field | Value |
|---|---|
| **Severity** | 🟡 High |
| **Effort** | 2–3 weeks |
| **Source** | Shadlix fork `src/core/memory_compression.cpp/.h` (309 LOC); deep comparison P1.2 |
| **Status** | 🔴 TODO |
| **Depends on** | — |

**Root cause:** PS4 has ~5.5 GB usable RAM. On 8 GB host systems, large games (Bloodborne, RDR, Yakuza) can OOM.

**Proposed change:** Port `memory_compression.cpp/.h` from the fork. Integrate with `MemoryManager::TryWriteBacking`. Add `--memory-compression 0|1|2|3` CLI flag.

**Affected files (new):**
- `src/core/memory_compression.cpp/.h`

**Affected files (modified):**
- `src/core/memory.cpp` — call `MemoryCompression::TryCompressBlock` on cold pages
- `src/core/memory.cpp` — call `MemoryCompression::DecompressBlock` on access
- `src/main.cpp` — add `--memory-compression` flag

**Acceptance criteria:**
- [ ] `--memory-compression 2` reduces peak RSS by ≥30% on Bloodborne
- [ ] Stats (compression_ratio, memory_saved_bytes) are visible in devtools
- [ ] No measurable performance regression when compression is disabled (level 0)
- [ ] Games that previously OOM'd on 8 GB systems now boot

---

### SHAD-012 · Cubeb audio backend

| Field | Value |
|---|---|
| **Severity** | 🟡 High (audio quality) |
| **Effort** | 1 week |
| **Source** | Shadlix fork `src/core/libraries/audio/cubeb_audio.cpp`; deep comparison P1.3 |
| **Status** | 🔴 TODO |
| **Depends on** | — |

**Proposed change:** Port `cubeb_audio.cpp` from the fork. Add `externals/cubeb`. Extend `AudioBackend` enum in `emulator_settings.h`.

**Affected files (new):**
- `src/core/libraries/audio/cubeb_audio.cpp`
- `externals/cubeb/` (vendored)

**Affected files (modified):**
- `src/core/libraries/audio/audioout.cpp` — add Cubeb backend to the backend selector
- `src/core/emulator_settings.h` — extend `AudioBackend` enum
- `CMakeLists.txt` — add Cubeb dependency

**Acceptance criteria:**
- [ ] Cubeb backend selectable in settings
- [ ] Audio latency on Linux/PipeWire is measurably lower than SDL
- [ ] No regression in SDL/OpenAL backends

---

### SHAD-013 · IPC client

| Field | Value |
|---|---|
| **Severity** | 🟡 High (future automation) |
| **Effort** | 3–5 days |
| **Source** | Shadlix fork `src/core/ipc/ipc_client.cpp/.h` (351 LOC); deep comparison P1.8 |
| **Status** | 🔴 TODO |
| **Depends on** | — |

**Proposed change:** Port `ipc_client.cpp/.h` from the fork. Add `--ipc-client <server>` CLI flag for slave-mode launch.

**Affected files (new):**
- `src/core/ipc/ipc_client.cpp/.h`

**Affected files (modified):**
- `src/main.cpp` — add `--ipc-client` flag

**Acceptance criteria:**
- [ ] Two shadPS4 instances can coordinate via IPC
- [ ] `--ipc-client` flag enables slave mode
- [ ] Standalone `shadps4-ipc-cli` tool for testing (optional, depends on SHAD-006's tools framework)

---

### SHAD-014 · Standalone reverse-engineering tools

| Field | Value |
|---|---|
| **Severity** | 🟡 High (contributor UX) |
| **Effort** | 1–2 weeks |
| **Source** | fpPS4's `tools/`; deep comparison P1.4 |
| **Status** | 🔴 TODO |
| **Depends on** | SHAD-006 (NID database), SHAD-007 (PKG) |

**Proposed change:** New `tools/` top-level CMake subdirectory. Each tool is a small `main.cpp` that links against `core/loader`, `core/file_format`, `shader_recompiler/frontend`.

**Tools to create:**
- `shadps4-elf-info` — dump ELF/SELF info (port of fpPS4's `elf_sym/`)
- `shadps4-param-sfo` — dump `param.sfo` (port of fpPS4's `param_sfo_info/`)
- `shadps4-playgo` — dump playgo chunks (port of fpPS4's `playgo_info/`)
- `shadps4-spirv-dis` — disassemble compiled SPIR-V (port of fpPS4's `spirv_helper/`)
- `shadps4-pkg-extract` — extract PKG to folder (depends on SHAD-007)
- `shadps4-nids` — query NID database (from SHAD-006)

**Affected files (new):**
- `tools/CMakeLists.txt`
- `tools/elf_info/main.cpp`
- `tools/param_sfo/main.cpp`
- `tools/playgo/main.cpp`
- `tools/spirv_dis/main.cpp`
- `tools/pkg_extract/main.cpp` (depends on SHAD-007)
- `tools/nids/main.cpp` (depends on SHAD-006)

**Acceptance criteria:**
- [ ] Each tool builds as a standalone binary
- [ ] `shadps4-elf-info /path/to/eboot.bin` prints ELF header + program headers + section headers
- [ ] `shadps4-param-sfo /path/to/param.sfo` prints all key-value pairs
- [ ] `shadps4-pkg-extract /path/to/game.pkg /output/dir/` extracts the PKG
- [ ] `shadps4-nids <NID>` prints the function name

---

### SHAD-015 · Enhanced camera library

| Field | Value |
|---|---|
| **Severity** | 🟡 High (camera games) |
| **Effort** | 1 week |
| **Source** | Shadlix fork `src/core/libraries/camera/camera.cpp` (1,317 LOC); deep comparison P1.6 |
| **Status** | 🔴 TODO |
| **Depends on** | — |

**Proposed change:** Port `camera.cpp` from the fork. The fork's version has system memory mapping for camera frames and a `sceCameraGetCalibData` stub.

**Affected files (modified):**
- `src/core/libraries/camera/camera.cpp` — replace with fork's version (1,317 LOC vs current ~minor)

**Acceptance criteria:**
- [ ] `sceCameraOpen`, `sceCameraStart`, `sceCameraStop` all work
- [ ] `sceCameraGetCalibData` returns a valid (stub) calibration structure
- [ ] Camera-using games (Playroom, Just Dance) boot further

---

### SHAD-016 · Companion-app and game-streaming HLE stubs

| Field | Value |
|---|---|
| **Severity** | 🟡 High (game unlock) |
| **Effort** | 2–3 weeks per module (mostly RE) |
| **Source** | fpPS4's companion/streaming modules; deep comparison P1.5 |
| **Status** | 🔴 TODO |
| **Depends on** | — |

**Proposed change:** Port from fpPS4 (re-license to GPL-2.0 with attribution):
- `ps4_libscecompanionhttpd.pas` → `src/core/libraries/companion/companion_httpd.cpp`
- `ps4_libscecompanionutil.pas` → `src/core/libraries/companion/companion_util.cpp`
- `ps4_libscevideorecording.pas` → `src/core/libraries/video_recording/video_recording.cpp`
- `ps4_libsceshareplay.pas` → `src/core/libraries/share_play/share_play.cpp`
- `ps4_libscegamelivestreaming.pas` → `src/core/libraries/game_live_streaming/game_live_streaming.cpp`

Initial implementations can return `SCE_OK` for the common paths. Real RE work happens incrementally.

**Acceptance criteria:**
- [ ] Each module has a stub implementation registered in `libs.h`
- [ ] Second Screen titles (Just Dance, etc.) boot further
- [ ] No regression in games that don't use these modules

---

## Phase 3 — Shader Compiler & Networking Audits (Week 7–12)

### SHAD-017 · Shader opcode coverage report (CI gate)

| Field | Value |
|---|---|
| **Severity** | 🟡 High (prevents future RE3-style regressions) |
| **Effort** | 2 weeks |
| **Source** | RE3 root-cause analysis §5.2 |
| **Status** | 🔴 TODO |
| **Depends on** | SHAD-001 |

**Root cause:** SHAD-001 was a 30-minute fix that took 18 months to diagnose. The shader compiler has a "long tail" of unimplemented opcodes that's only discovered when a specific game fails.

**Proposed change:** Add a CI gate that iterates every opcode in `src/shader_recompiler/frontend/opcodes.h` and verifies the translator has a non-`UNREACHABLE()` handler.

```python
# scripts/check_shader_coverage.py
import re, sys

opcodes = parse_opcodes_h('src/shader_recompiler/frontend/opcodes.h')
dispatched = parse_dispatch_cases('src/shader_recompiler/frontend/translate/*.cpp')

missing = opcodes - dispatched
if missing:
    print(f'ERROR: {len(missing)} opcodes have no dispatch case:')
    for op in sorted(missing):
        print(f'  - {op}')
    sys.exit(1)
```

**Affected files (new):**
- `scripts/check_shader_coverage.py`
- `.github/workflows/shader-coverage.yml` — run on every PR

**Acceptance criteria:**
- [ ] CI fails if any opcode in `opcodes.h` lacks a dispatch case
- [ ] Existing coverage gaps (after SHAD-001, there should be none for U64) are surfaced as a baseline report
- [ ] PRs that add new opcodes to `opcodes.h` are forced to add dispatch cases

---

### SHAD-018 · Compile-time SPIR-V emission benchmark suite

| Field | Value |
|---|---|
| **Severity** | 🟡 High (long-term) |
| **Effort** | 2–4 weeks |
| **Source** | Deep comparison P2.1 |
| **Status** | 🔴 TODO |
| **Depends on** | SHAD-017 |

**Proposed change:** New `tests/shader_bench/` directory. CMake target that runs a corpus of captured shaders through (a) full pipeline, (b) pipeline minus one pass, and reports FPS delta + compile time.

**Acceptance criteria:**
- [ ] Corpus of ≥100 captured shaders from real games
- [ ] Per-pass delta report (which passes help, which hurt, per shader profile)
- [ ] CI runs benchmark on every PR and posts delta as a comment

---

### SHAD-019 · userfaultfd texture upload fast path

| Field | Value |
|---|---|
| **Severity** | 🟡 High (Linux) |
| **Effort** | 3–4 weeks |
| **Source** | Deep comparison P2.2; RE3 secondary crash |
| **Status** | 🔴 TODO |
| **Depends on** | SHAD-009 |

**Proposed change:** `src/video_core/texture_cache/image.cpp` — add a `UserfaultTextureUploader` strategy alongside the existing `TileManager`. Lazy-load texture memory pages from a host staging buffer only when the GPU actually reads them.

**Acceptance criteria:**
- [ ] On Linux with `--userfaultfd`, texture upload is lazy
- [ ] Peak VRAM usage drops for texture-heavy games
- [ ] No regression on Windows (signal-based path unchanged)

---

### SHAD-020 · Per-game shader cache export/import + skip list

| Field | Value |
|---|---|
| **Severity** | 🟡 High (UX) |
| **Effort** | 1–2 weeks |
| **Source** | Shadlix fork's `getShaderSkipsEnabled()` + `ShouldSkipShader(hash)`; deep comparison P2.3 |
| **Status** | 🔴 TODO |
| **Depends on** | — |

**Proposed change:**

1. Extend `vk_pipeline_serialization.cpp` to emit/load `shader_cache/<title_id>.bin`.
2. Add `--import-shader-cache <path>` CLI flag.
3. Add per-game skip list loaded from `data/shader_skips/<title_id>.json`.

**Affected files (modified):**
- `src/video_core/renderer_vulkan/vk_pipeline_serialization.cpp` — add export/import
- `src/main.cpp` — add `--import-shader-cache` flag
- `src/core/emulator_settings.h` — add `shader_skip_hashes` field

**Affected files (new):**
- `data/shader_skips/CUSA14168.json` — RE3 broken shader hashes (until SHAD-001 is verified)
- `data/shader_skips/README.md` — explains the format

**Acceptance criteria:**
- [ ] `--import-shader-cache /path/to/cache.bin` loads a pre-built cache
- [ ] Second launch of a game is significantly faster than the first
- [ ] `data/shader_skips/<title_id>.json` lets users mark specific shaders to skip
- [ ] Cache format is documented for community tooling

---

### SHAD-021 · Investigate fork's reverted PRs (regression audit)

| Field | Value |
|---|---|
| **Severity** | 🔴 Critical (FIFA 16 cascade root cause) |
| **Effort** | 1 day per PR (10 PRs = 2 weeks) |
| **Source** | Shadlix fork commit log; deep comparison P3.4; FIFA 16 root-cause analysis |
| **Status** | 🔴 TODO |
| **Depends on** | — |

**Root cause:** The Shadlix fork's 7+ selective reverts are direct evidence that these upstream PRs broke real games. Each revert is a regression report that should be bisected.

**PRs to investigate (in priority order):**

| PR | Title | Why reverted | Likely affected games |
|---|---|---|---|
| (unknown) | `Revert predication` (commit `79850ab`) | Broke predication handling | Games using stencil/draw predicates |
| #4782 | `shader_recompiler: split resource tracking and flatten load from buffer for sharp source` | Too aggressive optimization, broke shaders | RE Engine, possibly EA Frostbite |
| #4908 | `Http2 fixes` | Networking stack instability | FIFA 16, EA online titles |
| #4910 | `Net Fixes` | Networking stack instability | FIFA 16, online titles |
| #4914 | `Trophies online (shadNet)` | shadNet instability | Any game using trophies |
| #4933 | `Http module fixups and implementations` | Networking stack instability | FIFA 16, EA online titles |
| #4940 | `np_utility initial implementation` | NP stack instability | FIFA 16, online titles |
| #4930 | `Tss support` | Unknown | Unknown |
| #4927 | `Trigger a lightbar reset on controller connection` | Unknown | Possibly DualShock-specific |
| #4973 | `Fix IR dumping` | Debugging change interferes with fork workflow | Developers |

**Proposed workflow for each PR:**

1. Check out the PR's merge commit on a clean `main` branch.
2. Run the test corpus (Bloodborne, DS Remastered, RDR, RE3, FIFA 16).
3. Identify the regression.
4. Either:
   - Fix the PR's root cause and re-merge, OR
   - Confirm the revert is correct and document why.

**Acceptance criteria:**
- [ ] Each PR has a bisect report documenting the regression
- [ ] FIFA 16 boots further after networking PRs are investigated
- [ ] No new regressions introduced by the investigation

---

### SHAD-022 · Vulkan backend PR audit (ensure all fork-rebased PRs are in main)

| Field | Value |
|---|---|
| **Severity** | 🟡 High |
| **Effort** | 1 day per PR (7 PRs = 1 week) |
| **Source** | Shadlix fork rebased PRs; deep comparison P2.4 |
| **Status** | 🔴 TODO |
| **Depends on** | — |

**PRs to verify are in upstream `main`:**

- #4768 — Scaled min/max blending emulation
- #4957 — sRGB for A2R10G10B10 display buffers
- #4951 — H.265/HEVC video decode
- #4896 — Coherent storage buffer decoration
- #4941 — Shared memory barrier enhancement + divergent loop detection
- #4906 — Shader structurize-later pipeline
- #4904 — Do not auto-select software Vulkan device

**Acceptance criteria:**
- [ ] Each PR is verified present in `main` OR cherry-picked if missing
- [ ] No conflict between PRs

---

## Phase 4 — Distribution & UX (Week 13–16)

### SHAD-023 · Centralized config refactor with `ReadbackSpeed` enum

| Field | Value |
|---|---|
| **Severity** | 🟡 High (enables future features) |
| **Effort** | 3–4 weeks |
| **Source** | Shadlix fork `src/common/config.cpp/.h` (3,594 LOC); deep comparison P2.5 |
| **Status** | 🔴 TODO |
| **Depends on** | SHAD-004 (hack flags need a home) |

**Proposed change:** Refactor `EmulatorSettings` into a free-function-style `Config::` namespace matching the fork. Keep backward compatibility for `EmulatorSettings::GetInstance()` as a thin wrapper.

Key additions:
- `ReadbackSpeed` enum: `Disable | Unsafe | Low | Default | Fast`
- `AudioBackend` enum: `SDL | OpenAL | Cubeb` (depends on SHAD-012)
- `OpenALHrtfMode` enum
- `OpenALOutputMode` enum
- `HideCursorState` enum

**Acceptance criteria:**
- [ ] All `EmulatorSettings::Get*()` callers still compile (via thin wrapper)
- [ ] New `Config::get*()` free functions are the preferred API
- [ ] `ReadbackSpeed` setting works end-to-end
- [ ] Per-game config overrides work

---

### SHAD-024 · Screenshot module

| Field | Value |
|---|---|
| **Severity** | 🟢 Medium |
| **Effort** | 1 week |
| **Source** | Shadlix fork `src/video_core/screenshot.cpp/.h` (176 LOC); deep comparison P2.6 |
| **Status** | 🔴 TODO |
| **Depends on** | — |

**Proposed change:** Port `screenshot.cpp/.h` from the fork. Move inline screenshot logic from `vk_presenter.cpp` into the new module.

**Acceptance criteria:**
- [ ] `VideoCore::TriggerScreenshot()` works programmatically
- [ ] `Alt+F12` hotkey still works
- [ ] Programmatic triggering enables future "photo mode" features

---

### SHAD-025 · Flatpak + AppImage + macOS .app packaging

| Field | Value |
|---|---|
| **Severity** | 🟡 High (non-Windows adoption) |
| **Effort** | 1–2 weeks |
| **Source** | Shadlix fork dist files; deep comparison P2.7 |
| **Status** | 🔴 TODO |
| **Depends on** | — |

**Proposed change:** Port from the fork:
- `.github/linux-appimage-qt.sh`
- `dist/net.shadps4.shadPS4.metainfo.xml`
- `dist/MacOSBundleInfo.plist.in`
- `dist/qt.conf`
- `net.shadps4.shadPS4.yaml` (Flatpak manifest)
- `externals/MoltenVK` (vendored)

Add CI jobs for AppImage + Flatpak + macOS bundle.

**Acceptance criteria:**
- [ ] Linux users can install via `flatpak install shadps4`
- [ ] AppImage works on Ubuntu 22.04+ without dependencies
- [ ] macOS `.app` bundle works on Apple Silicon

---

### SHAD-026 · Pure-HLE fallback mode (no firmware required)

| Field | Value |
|---|---|
| **Severity** | 🟢 Medium (onboarding) |
| **Effort** | Many weeks (ongoing RE) |
| **Source** | fpPS4's pure-HLE approach; deep comparison P3.1 |
| **Status** | 🔴 TODO |
| **Depends on** | — |

**Proposed change:** New `src/core/libraries/audiodec_hle/`, `src/core/libraries/font_hle/`, etc. Loaded as fallback when `sys_modules/` is empty.

**Acceptance criteria:**
- [ ] Games boot without firmware dumps (with reduced compatibility)
- [ ] Clear warning logged when HLE fallback is used
- [ ] Documentation explains the trade-offs

---

## Phase 5 — Long-Term Architectural Work (Ongoing)

### SHAD-027 · Built-in Qt6 GUI (gradual upstream)

| Field | Value |
|---|---|
| **Severity** | 🟢 Medium (UX) |
| **Effort** | 4–8 weeks spread over multiple releases |
| **Source** | Shadlix fork `src/qt_gui/` (~100 files); deep comparison P3.3 |
| **Status** | 🔴 TODO |
| **Depends on** | SHAD-023 (centralized config), SHAD-004 (hack flags) |

**Proposed change:** Gradually upstream individual Qt widgets from the fork:
1. `settings_dialog` first (depends on SHAD-023)
2. `main_window`
3. `trophy_viewer`
4. `mod_manager_dialog`
5. `compatibility_info`
6. `cheats_patches`

Keep `shadps4-qtlauncher` as the recommended GUI in the meantime.

**Acceptance criteria:**
- [ ] Each widget upstreamed as a separate PR
- [ ] No regression in CLI/headless mode
- [ ] Single-binary Qt mode available behind a CMake flag

---

### SHAD-028 · Flat NP module layout refactor

| Field | Value |
|---|---|
| **Severity** | 🟢 Low (maintainability) |
| **Effort** | 2–3 weeks |
| **Source** | Shadlix fork flat NP layout; deep comparison P3.5 |
| **Status** | 🔴 TODO |
| **Depends on** | SHAD-021 (NP regression audit) |

**Proposed change:** Gradual refactor. Move each NP subdirectory up one level, then delete `np_handler.cpp` once all references are migrated.

**Affected files:**
- `src/core/libraries/np/np_matching2/` → `src/core/libraries/np/np_matching2.cpp`
- `src/core/libraries/np/np_score/` → `src/core/libraries/np/np_score.cpp`
- `src/core/libraries/np/np_signaling/` → `src/core/libraries/np/np_signaling.cpp`
- `src/core/libraries/np/np_web_api/` → `src/core/libraries/np/np_web_api.cpp`
- `src/core/libraries/np/np_web_api2/` → `src/core/libraries/np/np_web_api2.cpp`
- Delete `src/core/libraries/np/np_handler.cpp/.h` (3,578 LOC)

**Acceptance criteria:**
- [ ] Flat NP layout matches the fork
- [ ] No regression in NP functionality
- [ ] `np_handler.cpp` deleted

---

### SHAD-029 · Unified fault-handler library

| Field | Value |
|---|---|
| **Severity** | 🟢 Low (maintainability) |
| **Effort** | 2–3 weeks |
| **Source** | fpPS4's `rtl/seh64.pas` concept; deep comparison P3.2 |
| **Status** | 🔴 TODO |
| **Depends on** | — |

**Proposed change:** Refactor `common/signal_context.*` into a proper `common/fault_handler/` library with per-platform backends.

**Acceptance criteria:**
- [ ] Single `FaultHandler` API across Windows/Linux/macOS
- [ ] Documentation explains how to add a new platform
- [ ] No regression in signal handling

---

### SHAD-030 · Per-game flexible/direct memory configuration

| Field | Value |
|---|---|
| **Severity** | 🟢 Medium |
| **Effort** | 1 week |
| **Source** | Shadlix fork's `getExtraDmemInMbytes()` / `getExtraFmemInMbytes()` |
| **Status** | 🔴 TODO |
| **Depends on** | SHAD-023 (centralized config) |

**Proposed change:** Port the fork's `getExtraDmemInMbytes()` / `getExtraFmemInMbytes()` / `getUseHostMemoryFallback()` config options.

**Acceptance criteria:**
- [ ] Per-game extra direct memory works
- [ ] Per-game extra flexible memory works
- [ ] Host memory fallback works when guest RAM is exhausted (integrates with SHAD-011)

---

## Phase 6 — Documentation & Process (Ongoing)

### SHAD-031 · Document stub-return-value policy

| Field | Value |
|---|---|
| **Severity** | 🟡 High |
| **Effort** | 1 day |
| **Source** | SHAD-002 follow-up |
| **Status** | 🔴 TODO |
| **Depends on** | SHAD-002 |

**Proposed change:** Add `documents/stub_policy.md` explaining:
- Default stub return is `SCE_KERNEL_ERROR_ENOSYS`
- How to add an explicit "safe to no-op" stub
- How to test that a stub is safe to no-op

**Acceptance criteria:**
- [ ] Document exists and is linked from `CONTRIBUTING.md`
- [ ] New contributors understand the policy

---

### SHAD-032 · Game-specific hack documentation

| Field | Value |
|---|---|
| **Severity** | 🟡 High |
| **Effort** | 1 day |
| **Source** | SHAD-004/SHAD-005 follow-up |
| **Status** | 🔴 TODO |
| **Depends on** | SHAD-005 |

**Proposed change:** Add `documents/game_hacks.md` documenting each hack flag and known per-game CUSA → hack mappings.

**Acceptance criteria:**
- [ ] Each hack flag documented with effect and risk
- [ ] Known per-game mappings listed
- [ ] Instructions for contributing new mappings

---

### SHAD-033 · Compatibility issue triage workflow

| Field | Value |
|---|---|
| **Severity** | 🟡 High |
| **Effort** | 1 day |
| **Source** | RE3/FIFA16 analysis process |
| **Status** | 🔴 TODO |
| **Depends on** | SHAD-014 (standalone tools) |

**Proposed change:** Add `documents/triage_workflow.md` documenting the standard process for triaging a "game doesn't boot" compatibility issue:

1. Reproduce with `--log-append` and sync logging
2. `grep "Stub:" shadps4.log` — find unimplemented syscalls
3. `grep "UNREACHABLE\|Assertion" shadps4.log` — find ASSERT failures
4. Use `shadps4-elf-info` (SHAD-014) to inspect the game's ELF
5. Use `shadps4-spirv-dis` (SHAD-014) to inspect failing shaders
6. Cross-reference with `data/game_hacks.json` (SHAD-005) for known workarounds

**Acceptance criteria:**
- [ ] Document exists with worked examples (RE3, FIFA 16)
- [ ] Triage time for new issues reduced from hours to minutes

---

### SHAD-034 · Weekly compatibility regression test

| Field | Value |
|---|---|
| **Severity** | 🟡 High |
| **Effort** | 1 week setup + ongoing |
| **Source** | Best practice from RPCS3/yuzu |
| **Status** | 🔴 TODO |
| **Depends on** | SHAD-017 (shader coverage), SHAD-018 (shader bench) |

**Proposed change:** Weekly CI job that boots a corpus of known-working games and reports any regression. Corpus starts with Bloodborne, DS Remastered, RDR, Hatsune Miku, Yakuza 0, DRIVECLUB.

**Acceptance criteria:**
- [ ] Weekly CI job runs the corpus
- [ ] Regressions are reported as GitHub issues automatically
- [ ] Corpus grows over time as more games become playable

---

## Summary: Execution Order

### Phase 1 — Quick Wins & Game Unblockers (Week 1–2)

| ID | Title | Effort | Status |
|---|---|---|---|
| SHAD-001 | Complete `V_CMP_*_U64` dispatch table | 30 min | 🔴 |
| SHAD-002 | `CommonStub` → `ENOSYS` | 1 day | 🔴 |
| SHAD-003 | Implement `sceKernelMlockall` | 1 day | 🔴 |
| SHAD-004 | Per-game hack flags framework | 2–3 days | 🔴 |
| SHAD-005 | Per-game hack auto-detection | 2–3 days | 🔴 |
| SHAD-006 | Externalized NID database | 3–5 days | 🔴 |
| SHAD-007 | PKG file format + Crypto++ | 1–2 weeks | 🔴 |
| SHAD-008 | ZArchive filesystem | 1 week | 🔴 |

**Phase 1 deliverables:**
- ✅ RE3 Remake boots past the shader ASSERT (SHAD-001)
- ✅ FIFA 16's `sceKernelMlockall` cascade is fixed (SHAD-002 + SHAD-003)
- ✅ Users can load `.pkg` and `.zar` files directly (SHAD-007 + SHAD-008)
- ✅ Per-game hack framework in place for future unblockers (SHAD-004 + SHAD-005)
- ✅ Contributors can query the NID database without recompiling (SHAD-006)

### Phase 2 — Architectural Improvements (Week 3–6)

| ID | Title | Effort | Status |
|---|---|---|---|
| SHAD-009 | Fix `page_manager` ASSERT (RE3 secondary) | 2–3 weeks | 🔴 |
| SHAD-010 | Storage I/O Scheduler | 2–3 weeks | 🔴 |
| SHAD-011 | Memory compression | 2–3 weeks | 🔴 |
| SHAD-012 | Cubeb audio backend | 1 week | 🔴 |
| SHAD-013 | IPC client | 3–5 days | 🔴 |
| SHAD-014 | Standalone RE tools | 1–2 weeks | 🔴 |
| SHAD-015 | Enhanced camera library | 1 week | 🔴 |
| SHAD-016 | Companion/streaming HLE stubs | 2–3 weeks/module | 🔴 |

**Phase 2 deliverables:**
- ✅ RE3 Remake fully playable (SHAD-009)
- ✅ Low-RAM systems can run large games (SHAD-011)
- ✅ Streaming-related game bugs fixed (SHAD-010)
- ✅ Audio quality improved on Linux (SHAD-012)
- ✅ RE contributors have standalone tools (SHAD-014)

### Phase 3 — Shader Compiler & Networking Audits (Week 7–12)

| ID | Title | Effort | Status |
|---|---|---|---|
| SHAD-017 | Shader opcode coverage CI gate | 2 weeks | 🔴 |
| SHAD-018 | SPIR-V emission benchmark suite | 2–4 weeks | 🔴 |
| SHAD-019 | userfaultfd texture upload | 3–4 weeks | 🔴 |
| SHAD-020 | Per-game shader cache + skip list | 1–2 weeks | 🔴 |
| SHAD-021 | Regression audit (fork's reverted PRs) | 2 weeks | 🔴 |
| SHAD-022 | Vulkan backend PR audit | 1 week | 🔴 |

**Phase 3 deliverables:**
- ✅ Future shader-compiler regressions caught at build time (SHAD-017)
- ✅ FIFA 16's networking cascade root-caused (SHAD-021)
- ✅ First-launch shader compile time improved (SHAD-020)
- ✅ Linux texture upload is lazy (SHAD-019)

### Phase 4 — Distribution & UX (Week 13–16)

| ID | Title | Effort | Status |
|---|---|---|---|
| SHAD-023 | Centralized config refactor | 3–4 weeks | 🔴 |
| SHAD-024 | Screenshot module | 1 week | 🔴 |
| SHAD-025 | Flatpak/AppImage/macOS packaging | 1–2 weeks | 🔴 |
| SHAD-026 | Pure-HLE fallback mode | Ongoing | 🔴 |

**Phase 4 deliverables:**
- ✅ Non-Windows users have proper installers (SHAD-025)
- ✅ New users can boot without firmware dumps (SHAD-026)
- ✅ Future feature additions are easier (SHAD-023)

### Phase 5 — Long-Term Architectural Work (Ongoing)

| ID | Title | Effort | Status |
|---|---|---|---|
| SHAD-027 | Built-in Qt6 GUI (gradual) | 4–8 weeks | 🔴 |
| SHAD-028 | Flat NP module layout | 2–3 weeks | 🔴 |
| SHAD-029 | Unified fault-handler library | 2–3 weeks | 🔴 |
| SHAD-030 | Per-game memory configuration | 1 week | 🔴 |

### Phase 6 — Documentation & Process (Ongoing)

| ID | Title | Effort | Status |
|---|---|---|---|
| SHAD-031 | Stub return value policy doc | 1 day | 🔴 |
| SHAD-032 | Game-specific hack documentation | 1 day | 🔴 |
| SHAD-033 | Compatibility issue triage workflow | 1 day | 🔴 |
| SHAD-034 | Weekly compatibility regression test | 1 week + ongoing | 🔴 |

---

## Dependency Graph

```
SHAD-001 (V_CMP_U64) ──────┐
                            ├──► SHAD-009 (page_manager) ──► SHAD-019 (userfaultfd textures)
SHAD-002 (ENOSYS) ──────────┤
                            ├──► SHAD-003 (Mlockall)
SHAD-004 (hack flags) ─────┤
                            ├──► SHAD-005 (auto-detect) ──► SHAD-032 (hack docs)
SHAD-006 (NID DB) ──────────┤
                            ├──► SHAD-014 (RE tools) ──► SHAD-033 (triage doc)
SHAD-007 (PKG) ─────────────┤
SHAD-008 (ZAR) ─────────────┘

SHAD-023 (config refactor) ──► SHAD-027 (Qt GUI gradual)
                          ──► SHAD-030 (per-game memory config)
                          ──► SHAD-004 (hack flags need a home)

SHAD-017 (shader coverage) ──► SHAD-018 (shader bench)
                          ──► SHAD-034 (weekly regression test)

SHAD-021 (regression audit) ──► SHAD-028 (flat NP layout)
                            ──► FIFA 16 unblock

SHAD-011 (memory compression) ──► SHAD-030 (per-game memory config)
```

---

## Local Development Workflow

Each issue should follow this workflow before any PR is opened:

### 1. Create a local branch

```bash
cd /home/z/my-project/repos/shadPS4
git checkout main
git pull upstream main
git checkout -b fix/SHAD-001-vcmp-u64-dispatch
```

### 2. Implement the fix

Make the changes described in the issue. Use the `Edit` tool to modify specific files; do not regenerate entire files.

### 3. Build and test locally

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug -DENABLE_QT_GUI=OFF
make -j$(nproc)
```

### 4. Verify acceptance criteria

Run through each checkbox in the issue's "Acceptance criteria" section. Document the results in the worklog.

### 5. Update worklog

Append to `/home/z/my-project/worklog.md`:

```markdown
---
Task ID: SHAD-001
Agent: <your name>
Task: Complete the V_CMP_*_U64 dispatch table

Work Log:
- Audited src/shader_recompiler/frontend/opcodes.h — found 16 V_CMP_*_U64 + 16 V_CMP_*_I64 opcodes defined
- Audited src/shader_recompiler/frontend/translate/vector_alu.cpp — found only 7 of 32 dispatched
- Added 25 missing case entries
- Built successfully
- Tested with RE3 Remake (CUSA14168) — boots past shader ASSERT

Stage Summary:
- 25 new dispatch cases added to vector_alu.cpp:393-418
- RE3 Remake now reaches the page_manager ASSERT (SHAD-009 needed next)
- No regression in Bloodborne, DS Remastered, RDR
```

### 6. Commit and prepare for PR

```bash
git add -A
git commit -m "shader_recompiler: Complete V_CMP_*_U64/I64 dispatch table (SHAD-001)

Added 25 missing case entries to the outer opcode dispatcher in
vector_alu.cpp. The V_CMP_U64 translator function was already
complete (handles all 8 ConditionOp values), but the dispatcher
only had entries for 7 of the 32 U64/I64 opcodes.

This unblocks RE Engine games (RE3 Remake, RE2 Remake, RE7, etc.)
that use 64-bit integer compare opcodes in their compute shaders.

Fixes #299 (CUSA14168 - RESIDENT EVIL 3)
"
```

### 7. Only after all Phase 1 issues are verified locally, open PRs upstream

Phase 1 issues are independent enough to be PR'd individually. Phase 2+ issues may need to be batched.

---

## Appendix A — Source Code References

### A.1 Key files modified by Phase 1

| File | Issue | LOC changed |
|---|---|---|
| `src/shader_recompiler/frontend/translate/vector_alu.cpp` | SHAD-001 | +25 lines |
| `src/core/aerolib/stubs.cpp` | SHAD-002 | ~5 lines |
| `src/core/libraries/kernel/memory.cpp` | SHAD-003 | +10 lines |
| `src/core/aerolib/aerolib.inl` | SHAD-003 | -1 line (remove STUB) |
| `src/core/emulator_settings.h/.cpp` | SHAD-004 | +50 lines |
| `src/main.cpp` | SHAD-004 | +15 lines |
| `src/common/hack_features.cpp/.h` (new) | SHAD-005 | +50 lines |
| `src/common/nid_database.cpp/.h` (new) | SHAD-006 | +200 lines |
| `scripts/gen_nids_csv.py` (new) | SHAD-006 | +50 lines |
| `src/core/crypto/*` (new, from fork) | SHAD-007 | +583 lines |
| `src/core/file_format/pkg*` (new, from fork) | SHAD-007 | +1,288 lines |
| `src/common/zar_fs.cpp/.h` (new, from fork) | SHAD-008 | +497 lines |

### A.2 Verification commands

```bash
# Verify SHAD-001: all U64/I64 opcodes dispatched
grep -cE "V_CMP[X]?_.*_U64|V_CMP[X]?_.*_I64" src/shader_recompiler/frontend/translate/vector_alu.cpp
# Expected: 32 (was 7)

# Verify SHAD-002: CommonStub returns ENOSYS
grep "return 0" src/core/aerolib/stubs.cpp
# Expected: 0 matches (was 3)

# Verify SHAD-003: sceKernelMlockall is implemented
grep "sceKernelMlockall" src/core/aerolib/aerolib.inl
# Expected: 0 matches (was 1)

# Verify SHAD-007: PKG support exists
ls src/core/file_format/pkg.cpp src/core/crypto/crypto.cpp
# Expected: both files exist
```

---

## Appendix B — Game-Specific Issue Mapping

| Game | CUSA | Blocking issue(s) | Phase 1 fix | Phase 2+ fix |
|---|---|---|---|---|
| Resident Evil 3 Remake | CUSA14168 | SHAD-001, SHAD-009 | SHAD-001 (boots past shader) | SHAD-009 (fully playable) |
| FIFA 16 | CUSA02126 | SHAD-002, SHAD-003, SHAD-021 | SHAD-002 + SHAD-003 (boots past Mlockall) | SHAD-021 (networking cascade) |
| The Order: 1886 | CUSA00035/076/100 | (per fork) | SHAD-004 + SHAD-005 (auto-detect + hacks) | — |
| RE2 Remake | (TBD) | SHAD-001 (same as RE3) | SHAD-001 | — |
| RE7 | (TBD) | SHAD-001 (same as RE3) | SHAD-001 | — |
| DMC5 | (TBD) | SHAD-001 (same as RE3) | SHAD-001 | — |
| Any EA Frostbite title | (various) | SHAD-002, SHAD-003, SHAD-021 | SHAD-002 + SHAD-003 | SHAD-021 |

---

## Appendix C — Worklog Protocol

All work on this plan MUST be logged to `/home/z/my-project/worklog.md` using the standard format:

```markdown
---
Task ID: SHAD-XXX
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

Before starting any task, read `/home/z/my-project/worklog.md` to understand prior work. After completing a task, append (do NOT overwrite) using the format above.

---

**End of Grand Improvement Plan.** This document is the master reference for all local development on shadPS4 before any PR is opened. Update the status column as work progresses.
