# PS5PCEM Research: PM4-Centric GPU Model + Typed IR Pipeline with SSA

**Date**: 2026-09-23  
**Researcher**: AbdoPS5 project  
**Source**: https://github.com/iStark/PS5PCEM (Zig, by Artur Strazewicz)

---

## Part 1: PM4-Centric GPU Model

### What PS5PCEM Does

PS5PCEM does NOT emulate the AGC (Advance Graphics Core) library — instead it
**decodes the raw PM4 command stream** that the game builds in its own memory
and submits directly to the GPU.

On PS5, games don't call a high-level graphics API. They build **packets** —
state changes, register writes, draws, dispatches, fences — in their own memory
and submit the buffer. This PM4 stream is the real interface to emulate.

### Architecture

```
Game builds PM4 packets in memory
  → Game calls sceGnmSubmitCommandBuffers()
  → PS5PCEM intercepts the buffer
  → gpu/pm4.zig decodes packets (opcode + body)
  → gpu/state.zig retains register context
  → gpu/executor.zig executes commands
  → gpu/scheduler.zig manages graphics + compute FIFOs
  → vulkan/backend.zig renders
```

### Key Files

| File | Purpose |
|------|---------|
| `src/gpu/pm4.zig` | PM4 packet decoder — parses opaque word buffer into named commands |
| `src/gpu/state.zig` | Retained state across submissions (registers, shaders, configs) |
| `src/gpu/executor.zig` | Applies register lists, ACQUIRE_MEM, WAIT_REG_MEM, EVENT_WRITE, draws |
| `src/gpu/scheduler.zig` | Separate graphics + compute FIFO queues with snapshot/resume |
| `src/gpu/resources.zig` | Resource management (buffers, images, descriptors) |

### PM4 Packet Format

```zig
pub const Kind = enum(u2) {
    register_write = 0,  // Consecutive register writes
    reserved = 1,        // Unassigned (error if encountered)
    filler = 2,          // Padding word (no body)
    command = 3,         // Opcode + arguments
};
```

Each packet has:
- **Header** (1 word): kind (bits 31:30), predicate/compute flags, opcode or base register
- **Body** (0+ words): arguments specific to the command

### What the Executor Handles

1. **Direct register lists** (native Gen5 and legacy AGC indirect)
2. **`ACQUIRE_MEM`** — memory synchronization
3. **`RELEASE_MEM`** — memory release + fence signaling
4. **`WAIT_REG_MEM`** (32/64-bit) — blocks until a register matches a value
   - Returns `blocked` + exact resume word if unmet
   - Never changes guest memory to manufacture progress
5. **`WRITE_DATA`** (standard and AGC) — memory writes
6. **`EVENT_WRITE`** — event signaling
7. **`SetFlip`** — display flip
8. **`INDIRECT_BUFFER`** — recursive buffer following (4-dword + 14-dword conditional)
   - Chain packets end their parent stream
   - Conditional packets retain the branch selected before a wait
   - Nesting up to 16 stream frames
   - Blocked child returns fixed root-to-leaf continuation

### Scheduler Design

- **Separate graphics + compute FIFO queues** (PS5 has separate pipes)
- **Snapshot + resume**: copies each root DCB + reachable indirect buffers
  to a snapshot that remains readable at original guest addresses while other
  backend operations stay live
- A blocked queue head retains its exact continuation + payload
- Work on the other queue can still run while one queue is blocked
- When a `RELEASE_MEM` writes the awaited label, the scheduler rechecks
  guest memory and resumes the exact nested stream position

### Comparison with AbdoPS5

| Aspect | AbdoPS5 (KytyPS5) | PS5PCEM |
|--------|-------------------|---------|
| **GPU model** | AGC HLE (4455 lines in agc.cpp) | PM4 direct decode |
| **Command execution** | Through AGC library emulation | Direct PM4 packet execution |
| **Scheduler** | Single queue | Separate graphics + compute FIFOs |
| **WAIT_REG_MEM** | Not explicitly modeled | Full 32/64-bit with resume word |
| **INDIRECT_BUFFER** | Through AGC | Recursive with conditional + chain support |
| **Snapshot/resume** | No | Full snapshot at guest addresses |

### Why PM4 is Interesting (But Hard to Port)

**Advantages**:
- Sees exactly what the game sends (no HLE abstraction layer)
- More accurate for games that use non-standard PM4 patterns
- Doesn't need the 4455-line AGC library emulation
- Can detect and handle edge cases the AGC HLE misses

**Disadvantages**:
- Requires rewriting the entire GPU command path (months of work)
- AbdoPS5's AGC HLE is already mature (156 NIDs, all implemented)
- Most games use the AGC library, not raw PM4
- The PM4 decoder needs to handle AMD's command processor spec exactly

### Verdict

**Not a port candidate** — the architectural difference is too fundamental.
AbdoPS5's AGC HLE approach works and is complete. PS5PCEM's PM4 approach is
more accurate in theory but harder to implement. The two are complementary:
if we ever find a game that uses raw PM4 (bypassing AGC), we can add a PM4
decoder as a fallback path. For now, the AGC HLE covers all known games.

**However**, the PM4 decoder is valuable as a **debugging tool** — it can
dump and inspect the raw command stream a game sends, even if we don't
execute through it. This could be a future enhancement to the RE tools
(Kyty-012).

---

## Part 2: Typed IR Pipeline with SSA

### What PS5PCEM Does

PS5PCEM has an **optional typed IR (Intermediate Representation) pipeline**
that sits between the RDNA2 decoder and the SPIR-V backend:

```
RDNA2 machine code
  → rdna2/decoder.zig (decode)
  → rdna2/ir.zig (optional: typed IR)
    → PipelineStage.typed (operands typed, metadata retained)
    → PipelineStage.validated (basic blocks, reachability)
    → PipelineStage.optimized (SSA, constant folding, DCE)
    → PipelineStage.legalized (SPIR-V-compatible form)
  → rdna2/spirv.zig (SPIR-V 1.5 emission)
```

### Pipeline Stages

```zig
pub const PipelineStage = enum {
    decoded,      // Raw decoded instructions
    typed,        // Operands typed, memory metadata retained
    validated,    // Basic blocks built, reachability verified
    optimized,    // SSA construction, constant folding, DCE
    legalized,    // SPIR-V-compatible form
};
```

### Pipeline Options

```zig
pub const PipelineOptions = struct {
    enable_typed_ir: bool = true,        // Use typed IR (vs direct decode→SPIR-V)
    enable_ssa_optimization: bool = true, // SSA + constant folding + DCE
};
```

When `enable_typed_ir = false`, the backend emits SPIR-V directly from
decoded instructions (like AbdoPS5 does). When `true`, it goes through
the IR pipeline.

### IR Operations

```zig
pub const Operation = enum {
    nop, move,
    integer_add, shift_left_add, integer_subtract,
    float_add, float_subtract, float_multiply,
    bit_and, bit_or, bit_xor,
    compare, branch, branch_conditional,
    memory, image, interpolation,
    export_value, synchronization,
    end, opaque_instruction,
};
```

Each IR node retains:
- `pc` — original program counter (for branch targets)
- `operation` — typed operation (API-neutral)
- `value_type` — bits32/uint32/sint32/float32/mask64/none
- `opcode` — original RDNA2 opcode
- `dst`, `dst2` — destination operands
- `sources` — source operands
- `branch_target` — target PC for branches
- `memory_access` — addressing info (byte offset, data words/bits, resource, etc.)

### Control Flow Graph

`rdna2/control_flow.zig` builds a proper CFG:

1. **Splits decoded programs** at direct branch targets and terminators
2. **Validates** that every direct target begins an instruction
3. **Emits typed branch/fallthrough edges** with predicate domains:
   - `none` — unconditional
   - `scalar_uniform` — SCC (scalar condition code)
   - `wave_mask` — VCC/EXEC (per-lane predicates)
4. **Discovers forward selection merges** (if/else merge points)
5. **Derives a dominator hierarchy** for nested and shared merge regions
6. **Records backward edges** separately (loop back-edges)
7. **Detects irreducible cycles** (can't be structured as a single loop)

### SSA Optimization

When `enable_ssa_optimization = true`:
- **Phi node construction** at merge points (hierarchical OpPhi state merging)
- **Constant folding** — evaluate constant expressions at compile time
- **Dead code elimination (DCE)** — remove non-leader scalar/vector NOPs
  - Preserves every PC and branch destination needed by control-flow emission
- **Dead definition elimination** — remove definitions that are never used

### SPIR-V Emission

The SPIR-V 1.5 writer:
- Translates 32-bit move, integer/bitwise/floating-point ALU
- Handles SDWA extraction and DPP/VOP3 modifiers
- Includes exact lowering for:
  - `V_SAD_U32` → `abs(src0 - src1) + src2`
  - `V_MUL_HI_I32` → two's-complement correction over unsigned 64-bit product
  - `V_CVT_FLR_I32_F32` → GLSL Floor before signed conversion
  - `V_LDEXP_F32` → GLSL Ldexp
  - `S_BFM_B32`, `S_BFE_U32`, `S_BFE_U64` → scalar field widths
- **Control flow lowering**:
  - Acyclic scalar selections → `OpSelectionMerge` + `OpBranchConditional`
  - Natural loops → `OpLoopMerge`
  - Irreducible cycles → block-index dispatcher (`OpLoopMerge` + `OpSwitch`)
  - Preserves VCC/EXEC per-lane predicates

### Real-world Impact

From PS5PCEM's project status:
- **Tetris Effect**: The compositor exercises the SSA path with 2,401
  instructions, 131 basic blocks, and 76 selections
- **Rita's Rewind**: Uses `V_SAD_U32`, `V_MUL_HI_I32`, `V_CVT_FLR_I32_F32`
  in vertex shader address/index arithmetic
- **Ghost of Yōtei**: Streamed scene loading advanced substantially with
  the IR pipeline's shader analysis

### Comparison with AbDoPS5

| Aspect | AbDoPS5 (KytyPS5) | PS5PCEM |
|--------|-------------------|---------|
| **IR pipeline** | Direct decode → SPIR-V (no IR) | Optional typed IR → SSA → SPIR-V |
| **Control flow** | Handled per-instruction | Full CFG with dominator hierarchy |
| **SSA** | No (scalar_provenance_tests has partial) | Yes (phi nodes, constant folding, DCE) |
| **Branch lowering** | Per-instruction | Structured (SelectionMerge, LoopMerge, Switch) |
| **Irreducible loops** | Not handled | Block-index dispatcher fallback |
| **Dead code elimination** | No | Yes (preserves branch targets) |
| **Constant folding** | No | Yes |

### Verdict

**Partial port candidate** — the typed IR pipeline is architecturally sound
and proven on real games. AbDoPS5 already has pieces of this:

1. **`scalar_provenance_tests`** — exists in KytyPS5's test suite, suggesting
   some scalar analysis was started
2. **`ShaderInfoCollection.cpp`** — collects shader metadata (partially typed IR)
3. **`spirvEmitterFlow.cpp`** — emits SPIR-V control flow (but not structured)

**What we could port**:
1. **Structured control flow lowering** — convert per-instruction branching to
   `OpSelectionMerge`/`OpLoopMerge`/`OpSwitch` (biggest impact — correct
   SPIR-V for complex shaders)
2. **Constant folding** — evaluate constant expressions at compile time
3. **Dead code elimination** — remove unused instructions

**What's too hard to port**:
- Full SSA construction with phi nodes (requires rewriting the IR layer)
- Dominator hierarchy analysis (requires a new pass system)
- The full typed IR (would need to replace the existing decode→SPIR-V path)

**Recommendation**: Add structured control flow lowering as a new pass in
the existing shader recompiler. This is the highest-impact piece — it fixes
incorrect SPIR-V for complex shaders with nested branches and loops. The
existing per-instruction approach works for simple shaders but fails on
games with complex control flow (like Tetris Effect's 131-block compositor).

---

## Summary

| Item | Port Feasibility | Impact | Effort | Recommendation |
|------|-----------------|--------|--------|----------------|
| PM4 decoder | Low (architecture change) | Medium (debugging tool) | Months | Use as RE tool only |
| Typed IR pipeline | Medium (partial port) | High (shader correctness) | 2-4 weeks | Port structured CFG + constant folding |
| SSA + DCE | Medium | Medium (perf + correctness) | 1-2 weeks | Port after structured CFG |
| PM4 dump tool | High (standalone tool) | Low (debugging) | 2-3 days | Add to RE tools (Kyty-012) |

### New Grand Plan Items Proposed

| ID | Title | Effort | Priority |
|----|-------|--------|----------|
| Kyty-038 | Structured control flow lowering (OpSelectionMerge/OpLoopMerge) | 2-3 weeks | 🟡 Medium |
| Kyty-039 | PM4 command stream dump tool (debugging) | 2-3 days | 🟢 Low |
| Kyty-040 | Constant folding + dead code elimination in shader recompiler | 1 week | 🟡 Medium |

### Additional Architecture Insights from PS5PCEM Docs

1. **FS base restoration** (cpu.md): Windows doesn't preserve FS base across
   context switches — PS5PCEM uses a vectored exception handler to restore
   it. This is a known issue that could affect AbDoPS5 too (we use the same
   native execution model). Worth investigating.

2. **Module graph** (runtime.md): PS5PCEM recursively indexes `.prx`/`.sprx`
   files, follows `DT_NEEDED` + PS5 needed-module declarations, maps the
   complete reachable graph, then relocates. This is more thorough than
   KytyPS5's loader.

3. **Diagnostics** (diagnostics.md): PS5PCEM has address attribution
   (module + offset + nearest export) and classified fault reports
   (call-through-null-pointer detection with stack scan). This is better
   than AbDoPS5's raw EXIT() dumps.

4. **Sparse direct-memory backing** (memory.md): Both use sparse shared
   objects, but PS5PCEM has explicit generation tracking for GPU page
   invalidation (tracked in the PS5PCEM comparison doc as Kyty-037).
