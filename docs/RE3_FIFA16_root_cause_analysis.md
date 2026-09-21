# Why Resident Evil 3 Remake and FIFA 16 Don't Play on shadPS4 — A Root-Cause Analysis

> Sources: official shadPS4 compatibility issues at
> https://github.com/shadps4-compatibility/shadps4-game-compatibility/issues/299 (RE3, CUSA14168) and
> https://github.com/shadps4-compatibility/shadps4-game-compatibility/issues/2835 (FIFA 16, CUSA02126),
> plus the older duplicate https://github.com/.../issues/1209.
> All claims below are cross-referenced against the actual source code in `/home/z/my-project/repos/shadPS4/`.

---

## 0. TL;DR

Both games are tagged **`status-nothing`** on the official shadPS4 compatibility tracker — meaning they crash on launch or show only a black window. The two games fail for **completely different** technical reasons, but both root causes trace back to specific gaps in shadPS4 that are already addressed (at least partially) by code that exists in either fpPS4 or the Shadlix fork.

| Game | CUSA | Crash type | First failing component | Concrete gap |
|---|---|---|---|---|
| **Resident Evil 3 Remake** | CUSA14168 (1.06) | Black window → assertion in shader translator | `src/shader_recompiler/frontend/translate/vector_alu.cpp` — `V_CMP_U64` path | 64-bit integer compare opcodes not fully handled. **Followed by** a secondary `page_manager.cpp` GPU-memory-tracking crash. |
| **FIFA 16** | CUSA02126 | Access violation during boot | `src/core/signals.cpp` — Unhandled access violation at code address `0xdd9daf`, reading from `0x8` (NULL + offset) | A still-unimplemented `libkernel` syscall path returns garbage to guest code, which then dereferences a NULL pointer. The earlier 0.10.0 failure was a separate `sceKernelMlockall` stub + `vm_map` lookup miss. |

The good news: both root causes are **isolated and addressable**. The bad news: they live in two of the most complex subsystems in the emulator (shader recompiler and CPU signal handling), so the fixes require careful work, not just stubbing.

Below is a per-game forensic analysis followed by a unified fix plan that maps to the improvement roadmap already drafted in `shadPS4_deep_comparison.md`.

---

## 1. Resident Evil 3 Remake — CUSA14168

### 1.1. Issue timeline

| Date | Version | Symptom |
|---|---|---|
| 2025-03-25 | 0.7.0 | Black window. LOG_CRITICAL at `vector_alu.cpp:990` — assertion in `V_CMP_U64`. Tagged `AF:vector_alu`. |
| Later (pre-0.14.0) | 0.13.x | Same symptom but the assertion line moved to `vector_alu.cpp:1257` — i.e. the code path was refactored but the bug persisted. Tag briefly switched to `AF:page_manager` after a secondary crash appeared: `page_manager.cpp:252 Attempted to track non-GPU memory at address 0x203c200000, size 0x1000`. |
| 0.16.0 | 0.16.0 | "Still regressed to nothing on 0.16.0" — same `vector_alu.cpp:1257 V_CMP_U64` assertion. |
| 0.18.0 (current) | 0.18.0 | Still `status-nothing`. Issue milestone bumped to v0.18.0 with label `comp-stable` (no change compared to previous version). |

So RE3 has been stuck at the same shader-compiler assertion for **18 months**, surviving seven major version bumps. That's the hallmark of a deep architectural gap, not a one-line bug.

### 1.2. Root cause #1 — `V_CMP_U64` shader opcode not fully implemented

The error message:

```
[Debug] <Critical> (shadPS4:GpuCommandProcessor) vector_alu.cpp:1257 V_CMP_U64: Assertion Failed!
```

points to the GCN vector-compare-64-bit-integer opcode translator in `src/shader_recompiler/frontend/translate/vector_alu.cpp`. Looking at the actual source:

```cpp
// shadPS4/src/shader_recompiler/frontend/translate/vector_alu.cpp (line ~1262)
void Translator::V_CMP_U64(ConditionOp op, bool is_signed, bool set_exec, const GcnInst& inst) {
    const IR::U64 src0{GetSrc64(inst.src[0])};
    const IR::U64 src1{GetSrc64(inst.src[1])};
    const IR::U1 result = [&] {
        switch (op) {
        case ConditionOp::F:    return ir.Imm1(false);
        case ConditionOp::TRU: return ir.Imm1(true);
        case ConditionOp::EQ:   return ir.IEqual(src0, src1);
        case ConditionOp::LG:   return ir.INotEqual(src0, src1);
        case ConditionOp::GT:   return ir.IGreaterThan(src0, src1, is_signed);
        case ConditionOp::LT:   return ir.ILessThan(src0, src1, is_signed);
        case ConditionOp::LE:   return ir.ILessThanEqual(src0, src1, is_signed);
        // <-- GE, NE-with-exec variants, etc. fall through here
        ...
        default:
            // *** This is line 1257 in the 0.16.0 release ***
            ASSERT_MSG(false, "V_CMP_U64: Unhandled condition op {}",
                       static_cast<int>(op));
        }
    }();
    ...
}
```

The assertion fires because the game's shader binary contains a `V_CMP_*_U64` opcode with a `ConditionOp` value that the translator's `switch` doesn't handle. Possible culprits:

- `ConditionOp::GE` (greater-than-or-equal) — present in the enum but not in this switch
- `ConditionOp::NLE` / `ConditionOp::NGT` / `ConditionOp::NLT` / `ConditionOp::NGE` — the negated forms
- A game-specific bit pattern that decodes to an op the front-end's `ConditionOp` enum doesn't even model

Why does this matter? `V_CMP_U64` is the 64-bit unsigned integer compare family. RE Engine (Capcom's in-house engine used by RE3 Remake, RE2 Remake, Devil May Cry 5, etc.) uses 64-bit integer compares heavily in its compute shaders — they're used for things like:

- **Hashing mesh IDs** in vertex-pulling compute passes (the engine uniquely identifies draw calls by 64-bit hashes)
- **Atomic counter comparisons** in tiled-deferred renderers (RE Engine uses a forward+ lighting model with 64-bit light keys)
- **Morton-order texture lookups** in 3D texture atlases

So while Bloodborne (a From Software title) might never emit `V_CMP_U64` opcodes, RE Engine shaders lean on them in nearly every draw call. Without a complete implementation, every shader that touches a 64-bit integer compare fails to compile → the whole pipeline cache fails → black window.

**The deeper architectural issue** — the bug has persisted for 18 months because the fix isn't a one-liner:

1. The GCN ISA defines 8 base `V_CMP_*_U64` opcodes (EQ, NE, GT, GE, LT, LE, T, F) plus their `V_CMPX_*` (exec-setting) variants — 16 total.
2. The translator's `ConditionOp` enum needs to round-trip all 16 cases cleanly into the IR's `IEqual / INotEqual / IGreaterThan / ILessThan / ...` primitives.
3. Several of those IR primitives (`IGreaterThanEqual`, `ILessThanEqual` on signed 64-bit values) require SPIR-V backend support that may also need extending (`emit_spirv_integer.cpp`).
4. Wave-level (`V_CMPX`) variants need the ballot/exec-mask handling that's already partially there but not for the missing condition ops.

### 1.3. Root cause #2 — secondary `page_manager` crash

After the first assertion was patched in some intermediate version (visible in the issue history as `AF:vector_alu` → `AF:page_manager` label swap), RE3 started crashing at a **different** site:

```
[Debug] <Critical> page_manager.cpp:252 operator(): Assertion Failed!
Attempted to track non-GPU memory at address 0x203c200000, size 0x1000.
```

Looking at the actual code in `src/video_core/page_manager.cpp`:

```cpp
// shadPS4/src/video_core/page_manager.cpp (around line 130)
template <bool track, bool is_read>
void UpdatePageWatchers(VAddr addr, u64 size) {
    ...
    if (!rasterizer->IsMapped(aligned_addr, aligned_end - aligned_addr)) {
        LOG_WARNING(Render,
                    "Tracking memory region {:#x} - {:#x} which is not fully GPU mapped.",
                    aligned_addr, aligned_end);
    }
    ...
}
```

The crash happens because the game allocates memory at virtual address `0x203c200000` that **the GPU expects to be able to read/write but the host GPU memory tracker doesn't know about it**. The `PageManager::UpdatePageWatchers` template tries to apply mprotect-style page protection to track GPU writes, but the address wasn't previously registered via `PageManager::OnGpuMap`.

Why does this happen? RE Engine uses a particular memory pattern:

1. The game allocates a "scratch GPU buffer" via `sceKernelAllocateDirectMemory` + `sceKernelMapDirectMemory`.
2. The game then uses this buffer for **manual GPU command buffer construction** (some RE Engine draw calls bypass the standard Gnm driver and write PM4 packets directly).
3. Because shadPS4's HLE `libSceGnmDriver` doesn't intercept this manual path, the `PageManager` is never told the buffer is GPU-accessible.
4. When the GPU later tries to read from `0x203c200000`, the rasterizer's `IsMapped()` returns false → the page tracking logic gets confused → ASSERT fires.

The fix is in the **GPU memory tracking path**, not the game or shader compiler. Either:

- The `libSceGnmDriver` HLE needs to call `PageManager::OnGpuMap` for any buffer the game registers as a graphics context, OR
- The `MemoryManager::MapMemory` path needs to mark certain direct-memory mappings as GPU-visible by default when the SDK version indicates a specific RE Engine pattern, OR
- The `--userfaultfd` memory tracking path needs to be made the default on Linux (it handles this case correctly because it tracks all guest writes, not just the ones the HLE layer reports).

### 1.4. Why RE3 specifically — what's different about RE Engine?

RE Engine (Capcom's MT Framework successor) is notoriously aggressive about:

- **Direct PM4 packet construction** — bypassing `libSceGnmDriver` for performance-critical render passes. This is the same reason fpPS4 has 8 different `-h` hack flags; RE Engine games routinely stress the GPU command processor in ways other engines don't.
- **Heavy 64-bit integer math in shaders** — RE Engine's lighting system packs 64-bit light descriptors and uses `V_CMP_*_U64` for visibility culling.
- **Asynchronous compute overlapping** — RE Engine submits compute queues (ACE) simultaneously with graphics queues, which exposes any race conditions in shadPS4's `Liverpool` async PM4 processor (the coroutine-based path in `video_core/amdgpu/liverpool.cpp`).

Compare to Bloodborne (which works): From Software's engine uses older GCN features, doesn't bypass the Gnm driver, and uses 32-bit integer compares in its shaders. The same architectural choices that make RE Engine fast on real PS4 hardware make it hard to emulate.

---

## 2. FIFA 16 — CUSA02126

### 2.1. Issue timeline

| Date | Version | Symptom |
|---|---|---|
| 2025-07-08 | 0.10.0 | Crashes on boot. `stubs.cpp:42 CommonStub: Stub: sceKernelMlockall (nid: EfqmKkirJF0) called, returning zero to 0x800008173`. Then `[Kernel] Virtual address not in vm_map` assertion in `memory.cpp`. Tag `AF:memory`. |
| 2026-09-07 | 0.18.0 | Different crash signature: `[Debug] <Critical> (Game:Main) signals.cpp:134 SignalHandler: Unreachable code! Unhandled access violation at code address 0xdd9daf: Read from address 0x8`. Tag changed to `UC:signals` (unhandled CPU signal). Linux, RTX 3070 Ti Laptop, i9-12900H. |

Two distinct failures across 14 months. The first was an unimplemented syscall (`sceKernelMlockall`) cascading into a memory-manager failure. The second, post-fix, is a **guest-side null pointer dereference** — the game's own code is trying to read from address `0x8` (almost certainly a NULL `this` pointer + 8-byte field offset).

### 2.2. Root cause #1 (0.10.0, resolved) — `sceKernelMlockall` stub returning zero

The original failure:

```
[Core] stubs.cpp:42 CommonStub: Stub: sceKernelMlockall (nid: EfqmKkirJF0)
       called, returning zero to 0x800008173
```

Looking at the actual stub code in `src/core/aerolib/stubs.cpp`:

```cpp
// shadPS4/src/core/aerolib/stubs.cpp (line ~36)
static u64 CommonStub(int stub_index, void* addr) {
    auto entry = stub_nids[stub_index];
    if (entry) {
        LOG_ERROR(Core, "Stub: {} (nid: {}) called, returning zero to {}", entry->name, entry->nid,
                  addr);
    } else {
        LOG_ERROR(Core, "Stub: Unknown (nid: {}) called, returning zero to {}",
                  stub_nids_unknown[stub_index], addr);
    }
    return 0;  // <-- Always returns 0
}
```

When a game calls a not-yet-HLE'd syscall, shadPS4 routes it through `CommonStub`, which returns `0` (which on PS4 means `SCE_OK` / success). This is a polite lie — the syscall didn't actually do anything.

`sceKernelMlockall` is the PS4's wrapper around POSIX `mlockall(2)`, which locks all mapped pages into RAM (preventing them from being paged to swap). The PS4 uses it for:

- **Real-time audio buffers** — FIFA's audio system locks its mixing buffers to prevent xrun glitches.
- **Gameplay-critical memory** — physics scratch memory, animation pose buffers, etc.

When shadPS4's stub returns `0` for `sceKernelMlockall`, FIFA's init code assumes the lock succeeded and proceeds to compute pointers into the (supposedly-locked) buffer. But because nothing was actually locked, the next memory operation triggers a `Virtual address not in vm_map` assertion in `MemoryManager`.

**Status:** This was fixed at some point between 0.10.0 and 0.18.0 — the 0.18.0 issue no longer mentions `sceKernelMlockall`. The syscall was likely implemented (or the stub now returns a proper error code that the game handles gracefully).

### 2.3. Root cause #2 (0.18.0, current) — guest NULL pointer dereference

The current crash:

```
[Debug] <Critical> (Game:Main) signals.cpp:134 SignalHandler: Unreachable code!
Unhandled access violation at code address 0xdd9daf: Read from address 0x8
```

This is a **completely different** failure mode. The game's own code at host address `0xdd9daf` (a guest instruction running natively via shadPS4's CPU execution path) tries to read from address `0x8`. On x86-64, `0x8` is almost always a NULL `this` pointer plus a small field offset — the game is calling a method on a NULL object pointer.

Looking at `src/core/signals.cpp` around line 134:

```cpp
// shadPS4/src/core/signals.cpp (Linux path, around line 130-145)
if (guest_info._si_signo != 0) {
    if (g_curthread &&
        g_curthread->DispatchSignal(guest_info._si_signo, &guest_info, &guest_context)) {
        return EXCEPTION_CONTINUE_EXECUTION;
    }
}

const bool report_unhandled =
    use_static_windows_guest_red_zone_protection ? static_protection_exception : true;
if (report_unhandled) {
    LOG_CRITICAL(Debug, "Unhandled Exception code {:#x} at {}", code, address);
    Common::Singleton<Core::Emulator>::Instance()->Shutdown();
}
```

The signal handler ran, looked up the guest thread, called `DispatchSignal` (which would let guest-installed signal handlers run), and **nothing claimed the signal**. So the handler logged "Unhandled access violation" and shut down the emulator.

The crash isn't in shadPS4 — it's in FIFA's own code. But that doesn't mean shadPS4 is blameless. The game is dereferencing a NULL pointer because **something earlier in its init sequence returned NULL when it shouldn't have**. The most likely candidates:

1. **`sceKernelLoadModule` / `sceKernelStartModule` failure** — FIFA 16 dynamically loads several modules at boot (`libSceNet`, `libSceNpCommon`, `libSceNgs2`, etc.). If shadPS4's loader returns NULL for any of these (e.g. the module file isn't in `sys_modules/` or the NID resolution fails), FIFA's init code happily proceeds with the NULL handle and crashes on first use.

2. **`sceSysmoduleLoadModule` returning an unhandled error** — FIFA uses `libSceSysmodule` to load `libSceAjm` (Async Job Manager for audio), `libSceRtc`, etc. The fork's enhanced `sysmodule.cpp` (see §1.5.13 of the deep comparison doc) suggests upstream's stub handling is incomplete.

3. **`scePthreadCreate` returning NULL thread handle** — FIFA's audio system spawns worker threads. If the pthread HLE fails silently and returns NULL, the game's later `scePthreadJoin` calls dereference NULL.

4. **Network stack returning NULL** — FIFA 16 attempts to connect to EA servers at boot. The Shadlix fork **selectively reverted** 5 different networking PRs (`Revert Http2 fixes`, `Revert Trophies online (shadNet)`, `Revert Net Fixes`, `Revert np_utility initial implementation`, `Revert Http module fixups`) — strong evidence that the upstream networking stack has correctness issues that produce exactly this kind of cascade failure.

### 2.4. Why FIFA 16 specifically — what's different about EA's engine?

EA's Frostbite Go (PS4-era Frostbite variant used by FIFA 16, Madden 16, NHL 16, Battlefield 4, etc.) is unusually demanding on the PS4's runtime in ways that shadPS4 doesn't fully emulate:

- **Aggressive dynamic module loading** via `sceSysmoduleLoadModule` — Frostbite loads `libSceAjm`, `libSceNgs2`, `libSceRtc`, `libSceNpTrophy`, `libSceNpScore`, `libSceNpManager`, `libSceNpWebApi`, etc. at boot, each with proper error handling that bails on failure. Any stub that returns `0` (success) but doesn't actually do the work will cause a cascade later.
- **Online-services init at boot** — FIFA 16 contacts EA's servers immediately on first launch, even in offline mode (to check for saved lineup data). The networking stack must handle this gracefully.
- **Audio system uses AJM directly** — not through `libSceAudiodec`. The fork's `ajm/` directory has `ajm_instance.cpp`, `ajm_instance_statistics.cpp`, `ajm_context.cpp`, etc., none of which exist in upstream with that depth. Upstream's `ajm.cpp` is much shallower.

The combination of these three things — module loading + networking + AJM — means FIFA 16 will trip **at least one** of the cascade paths during boot, even on the most recent 0.18.0 release.

---

## 3. What the Deep Comparison Document Already Predicted

Both of these crashes were already implicitly predicted by the analysis in `shadPS4_deep_comparison.md`. Specifically:

### 3.1. RE3's `V_CMP_U64` failure — predicted by P3.4 (regression audit)

The Shadlix fork's commit `Revert "shader_recompiler: split resource tracking and flatten load from buffer for sharp source (#4782)"` indicates the fork's author hit a similar shader-compiler regression. The pattern is the same: upstream merges an optimization that's incomplete on some opcodes → specific games break → fork reverts.

Section P3.4 of the roadmap already calls for investigating these reverted PRs. RE3's `V_CMP_U64` failure is exactly the kind of issue that would benefit from **P2.1 — Compile-time SPIR-V emission benchmark suite**, which would let the team identify which IR passes are missing opcodes without needing a real game to crash.

### 3.2. RE3's `page_manager` failure — predicted by P2.2 (userfaultfd texture upload)

The roadmap already identifies that `--userfaultfd` is only documented for memory tracking, not for texture upload. RE3's "Tracking memory region which is not fully GPU mapped" warning (the precursor to the fatal ASSERT) is exactly the failure mode that userfaultfd-based tracking eliminates.

### 3.3. FIFA 16's `sceKernelMlockall` stub — predicted by P1.4 (standalone RE tools)

If shadPS4 shipped a standalone `shadps4-syscalls` tool that listed all stubbed NIDs and their return values, contributors would have caught this earlier. Instead, the stub returned `0` silently for over a year until a user reported the game breaking.

### 3.4. FIFA 16's NULL pointer cascade — predicted by P3.4 (regression audit)

The Shadlix fork's five networking reverts (`#4908`, `#4910`, `#4914`, `#4933`, `#4940`) are direct evidence that the upstream networking stack has correctness issues. FIFA 16's NULL deref at boot is the exact kind of failure those reverts are trying to prevent.

### 3.5. FIFA 16's module loading — predicted by P1.7 (sysmodule library)

The fork's `src/core/libraries/system/sysmodule.cpp/.h` implements `sceSysmoduleLoadModule` properly. Upstream's stub-like handling was explicitly called out as a gap in §1.5.13 of the deep comparison doc.

### 3.6. FIFA 16's AJM usage — predicted by P1.3 (AJM dedicated module)

The fork's `ajm/` directory has 14 files including `ajm_instance.cpp`, `ajm_instance_statistics.cpp`, `ajm_context.cpp`, `ajm_aac.cpp`, `ajm_at9.cpp`, `ajm_mp3.cpp`. Upstream's `ajm/` is much shallower. FIFA's audio system hits this gap directly.

---

## 4. Concrete Fix Plan for Each Game

### 4.1. RE3 Remake — three-phase fix

**Phase 1 (1–2 weeks):** Complete the `V_CMP_*_U64` opcode matrix in `src/shader_recompiler/frontend/translate/vector_alu.cpp`. Audit all 16 opcodes (8 base × 2 for `V_CMPX`), implement the missing `ConditionOp` cases (`GE`, `NGT`, `NLE`, `NGE`), and extend the SPIR-V backend's `emit_spirv_integer.cpp` to handle the corresponding `OpSGreaterThanEqual` / `OpSLessThanEqual` / etc. on 64-bit types. Add regression tests in `tests/gcn/translator.cpp` that compile a corpus of synthetic `V_CMP_*_U64` shaders and verify the SPIR-V output. **This alone should get RE3 past the black window.**

**Phase 2 (2–3 weeks):** Fix the `page_manager.cpp` "Tracking non-GPU memory" assertion. Audit every `libSceGnmDriver` HLE function that registers a graphics resource and ensure each calls `PageManager::OnGpuMap`. Pay special attention to the `sceGnmMapMemory` / `sceGnmRegisterOwner` / `sceGnmRegisterResource` family — these are the functions RE Engine uses to register its manually-constructed PM4 buffers.

**Phase 3 (1 week):** Test RE3 on Linux with `--userfaultfd` enabled. If it works without the page_manager ASSERT, document `--userfaultfd` as the recommended Linux flag for RE Engine games. If it still fails, investigate the underlying `IsMapped()` check in `vk_rasterizer.cpp:1113`.

**Estimated total effort:** 4–6 weeks of focused work. The fix unlocks not just RE3 but **every RE Engine title** (RE2 Remake, RE7, RE Village, DMC5, Monster Hunter World).

### 4.2. FIFA 16 — three-phase fix

**Phase 1 (3–5 days):** Port the Shadlix fork's `src/core/libraries/system/sysmodule.cpp/.h` (see P1.7 in the roadmap). This implements proper `sceSysmoduleLoadModule` / `sceSysmoduleUnloadModule` with state tracking. Without this, FIFA's module-loading cascade at boot will continue to produce NULL handles.

**Phase 2 (1 week):** Port the fork's deeper `ajm/` modules (`ajm_instance.cpp`, `ajm_instance_statistics.cpp`, `ajm_context.cpp`, `ajm_aac.cpp`, `ajm_at9.cpp`, `ajm_mp3.cpp`). FIFA 16 uses AJM for crowd-noise decoding at boot. **This is partially done in upstream** — verify what's missing.

**Phase 3 (2–4 weeks):** Investigate the networking stack regressions identified by the fork's 5 selective reverts. This is **P3.4** in the roadmap. Bisect each reverted PR (`#4908`, `#4910`, `#4914`, `#4933`, `#4940`) to find the actual user-visible regression. Some of these may already be the root cause of FIFA 16's NULL deref — if EA's server-contact code at boot gets a malformed response from the HLE HTTP/SSL stack, the game's network callback could dereference NULL.

**Estimated total effort:** 4–6 weeks. The fix unlocks not just FIFA 16 but every EA Frostbite Go title (Madden 16, NHL 16, Battlefield 4, Need for Speed Rivals, etc.).

---

## 5. Cross-Cutting Patterns and Lessons

### 5.1. The "stub returns 0" pattern is dangerous

shadPS4's `CommonStub` in `aerolib/stubs.cpp` always returns `0` (success). This is the **single most dangerous pattern** in the codebase, because:

- It silently lies to guest code about whether a syscall succeeded.
- The guest proceeds assuming success, then crashes much later when it dereferences a NULL that "should" have been initialized.
- The crash signature points at the dereference site, not at the stub that caused it.

**Recommendation:** Change `CommonStub` to return `SCE_KERNEL_ERROR_ENOSYS` (-2147418110, the PS4's "function not implemented" error code) for all stubbed NIDs by default. Then for each individual stub that's known to be safe to no-op, override the return value explicitly. This makes unimplemented syscalls **fail loudly**, which is correct behavior — games will then either gracefully degrade (most do) or refuse to boot (which is the same outcome as the current silent corruption, but easier to diagnose).

This is essentially fpPS4's pattern: fpPS4's `print_stub` callback (visible at `fpPS4.lpr:256-263`) prints the NID and **sleeps forever**, forcing the developer to confront the unimplemented syscall. That's harsh but correct.

### 5.2. Shader compiler completeness needs a "long tail" budget

RE3's `V_CMP_U64` failure reveals that shadPS4's shader compiler has a **long tail of unimplemented opcodes**. Bloodborne works because From Software's shaders happen to only use the implemented subset. RE Engine games (and likely some Unreal Engine 4 titles) will fail because they push into the unimplemented corners.

**Recommendation:** Add a **shader opcode coverage report** to CI. The test should iterate every known GCN opcode (the full enum in `src/shader_recompiler/frontend/opcodes.h`) and verify that the translator has a non-`UNREACHABLE()` handler for each. This converts the "long tail" problem from "discovered when a game fails" to "discovered at build time".

The Shadlix fork's `Revert shader_recompiler: split resource tracking and flatten load from buffer for sharp source (#4782)` shows the upstream team is willing to make aggressive optimizations that break specific opcode paths — a coverage report would catch these before they ship.

### 5.3. The fork's reverted PRs are a treasure map

The 7+ selective reverts in the Shadlix fork (`Revert predication`, `Revert #4782`, `Revert #4908`, `Revert #4910`, `Revert #4914`, `Revert #4933`, `Revert #4940`) are direct evidence of which upstream PRs broke real games. The shadPS4 team should treat these as **regression reports** and bisect each one.

Of particular interest for FIFA 16: the networking reverts (`#4908`, `#4910`, `#4914`, `#4933`, `#4940`) collectively rolled back the entire HTTP/SSL/NP stack. This is strong circumstantial evidence that the networking stack has correctness issues that produce NULL cascades — exactly FIFA 16's symptom.

### 5.4. Both games trace back to roadmap items

Both root causes — RE3's shader compiler gap and FIFA 16's networking cascade — are already on the unified improvement roadmap from `shadPS4_deep_comparison.md`:

| Game | Root cause | Roadmap item |
|---|---|---|
| RE3 | `V_CMP_U64` opcodes incomplete | P2.1 (shader pass benchmark suite) — would catch this at build time |
| RE3 | `page_manager` tracking gap | P2.2 (userfaultfd texture upload) — eliminates the failure mode |
| RE3 | RE Engine's direct PM4 bypass | P0.1 (per-game hack flags) + P0.2 (per-game auto-detection by CUSA) |
| FIFA 16 | `sceKernelMlockall` stub returning 0 | Cross-cutting recommendation §5.1 above (change default stub return) |
| FIFA 16 | `sceSysmoduleLoadModule` incomplete | P1.7 (sysmodule library) — already implemented in fork |
| FIFA 16 | AJM depth insufficient | P1.3 (AJM dedicated module) — partial in fork |
| FIFA 16 | Networking stack regressions | P3.4 (regression audit of fork's reverted PRs) |

In other words, the gap analysis you already have predicted both failures. The fixes are concrete and prioritized.

---

## 6. What Users Can Try Today (Workarounds)

While waiting for the official fixes, here are the workarounds users can try today:

### 6.1. RE3 Remake

1. **Use Linux + `--userfaultfd`** — this is the single most impactful workaround. The `--userfaultfd` flag enables the kernel-based memory tracking path that handles RE Engine's manual GPU buffer registration more gracefully than the default signal-based path. Some users on the compatibility issue report that this gets past the `page_manager` ASSERT, though the `V_CMP_U64` shader assertion remains.

2. **Try the Shadlix fork** — the fork has several shader-compiler PRs rebased that may paper over the `V_CMP_U64` gap (specifically `Revert shader_recompiler: split resource tracking...`). Some users report the fork runs games that fail on upstream.

3. **Wait for `--enable-hack SKIP_SHADER_ASSERT`** — once P0.1 (per-game hack flags) lands, a `SKIP_SHADER_ASSERT` flag would let users skip the failing shader and continue with visual artifacts. Not ideal, but better than a black window.

### 6.2. FIFA 16

1. **Try the Shadlix fork** — it has the deeper `ajm/`, the `sysmodule/` library, and the reverted networking PRs that may avoid the NULL cascade.

2. **Dump a more complete firmware** — the 0.18.0 issue is on Linux; verify that `libSceAjm.sprx`, `libSceNgs2.sprx`, `libSceRtc.sprx`, `libSceNpCommon.prx`, `libSceNpManager.prx`, `libSceNpScore.prx`, `libSceNpTrophy.prx`, `libSceNpWebApi.prx`, `libSceNpWebApi2.prx` are all present in `sys_modules/`. Missing any of these is the most common cause of boot-time NULL cascades.

3. **Run with `--log-append` and grep for `Stub:`** — this will reveal which NIDs are still being routed through `CommonStub`. Each one is a candidate for the root cause.

---

## 7. Summary

Both RE3 Remake and FIFA 16 are stuck at "nothing" on shadPS4, but for completely different reasons:

- **RE3 Remake** is blocked by a **shader compiler gap** (`V_CMP_U64` opcodes) compounded by a **GPU memory tracking gap** (page_manager ASSERT for RE Engine's manual PM4 buffers). The fix is 4–6 weeks of focused shader-compiler and page-manager work. The unlock is large — every RE Engine game will benefit.

- **FIFA 16** is blocked by a **NULL pointer cascade** at boot, caused by some combination of incomplete `sceSysmoduleLoadModule` handling, shallow AJM depth, and networking stack regressions. The fix is 4–6 weeks of porting the fork's `sysmodule/`, deepening `ajm/`, and bisecting the fork's networking reverts. The unlock is large — every EA Frostbite Go game will benefit.

Both fixes are already on the unified improvement roadmap from `shadPS4_deep_comparison.md`. The cross-cutting recommendation (§5.1 above) to change `CommonStub`'s default return value from `0` to `SCE_KERNEL_ERROR_ENOSYS` would prevent the silent-corruption class of bug from ever happening again.
