# Compatibility Issue Triage Workflow

**Status:** Active (Kyty-030)
**Last updated:** 2026-09-23

## Overview

This document defines the process for triaging compatibility issues
reported by users or discovered during testing. The goal is to quickly
categorize the issue, identify the root cause, and either fix it or
apply a workaround.

## Triage Flowchart

```
Issue Report
    │
    ▼
┌─────────────────────────────────────┐
│ 1. Reproduce: Can you reproduce?    │──No──► Mark as "Cannot Reproduce",
│    (same game, same settings)       │       ask for more info
└─────────────────────────────────────┘
    │ Yes
    ▼
┌─────────────────────────────────────┐
│ 2. Categorize: What kind of issue? │
│    a) Crash (EXIT/assert/SIGSEGV)   │
│    b) Hang (infinite loop/deadlock)│
│    c) Graphics glitch              │
│    d) Audio glitch                  │
│    e) Input issue                   │
│    f) Performance                   │
└─────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────┐
│ 3. Isolate: Which subsystem?        │
│    - Check log (_kyty.txt)          │
│    - Check PM4 dump (Kyty-039)      │
│    - Check shader dump              │
│    - Check GpuPageTracker state     │
└─────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────┐
│ 4. Decide: Fix or Workaround?      │
│    Fix: root cause is clear         │
│    Workaround: add hack flag        │
└─────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────┐
│ 5. Document: Update game_hacks.md   │
│    + compatibility list              │
└─────────────────────────────────────┘
```

## Step 1: Reproduce

**Required info from the reporter:**
- Game title + serial (e.g., `PPSA01491` for Sifu)
- AbDoPS5 build version (git hash)
- OS + GPU (e.g., Linux + RTX 4070)
- Steps to reproduce
- Log file (`_kyty.txt`)

**If you can't reproduce:**
- Ask for the PM4 dump (`--dump-pm4 pm4.txt`)
- Ask for a screenshot or video of the issue
- Check if the reporter is using a different game region

## Step 2: Categorize

### a) Crash

Check the log for:
- `EXIT(...)` — emulator hit an assertion. Look at the file:line to
  identify the subsystem.
- `SIGSEGV` — null pointer or invalid memory access. Use the PM4 dump
  to see which command was being processed.
- `Vulkan validation error` — Vulkan API misuse. Enable
  `vulkan_validation_enabled = true` for detailed messages.

### b) Hang

- Enable `graphics_debug_dump_enabled = true` to see the last PM4
  packet before the hang.
- Check if the GPU thread is stuck in `ProcessPm4()` (infinite indirect
  buffer chain).
- Check if the render thread is waiting for an async pipeline that
  never finishes (Kyty-032 `PollAsyncResults()`).

### c) Graphics Glitch

- Enable `shader_log_direction = File` to dump SPIR-V for inspection.
- Check if the issue is a missing shader opcode (Kyty-001 coverage).
- Check if the issue is a layout transition (Kyty-033 per-subresource
  tracking).
- Check if the issue is a stale texture (Kyty-037 GpuPageTracker).

### d) Audio Glitch

- Check if cubeb backend is enabled (Kyty-011).
- Check `src/libs/audio.cpp` for stub calls.

### e) Input Issue

- Check SDL3 gamepad mapping (Kyty-021 SDL3 migration).
- Check `src/libs/controller.cpp` for the specific button/axis.

### f) Performance

- Check if the pipeline cache is being used (Kyty-031).
- Check if async compilation is working (Kyty-032).
- Profile with Tracy (`profiler_enabled = true`).

## Step 3: Isolate

### Using the PM4 Dump (Kyty-039)

```bash
# Enable PM4 dump
echo '{"pm4_dump_enabled": true, "pm4_dump_path": "_Pm4Dump.txt"}' > config.json
# Run the game until the issue occurs
./kyty_emulator game.elf
# Analyze the dump
grep "IT_DRAW_INDEX_AUTO\|IT_DISPATCH_DIRECT\|IT_EVENT_WRITE_EOP" _Pm4Dump.txt | tail -20
```

The last few packets before a crash usually identify the problematic
render path.

### Using the Shader Log

```bash
# Enable shader logging
echo '{"shader_log_direction": "File", "shader_log_folder": "_Shaders"}' > config.json
# Run the game
./kyty_emulator game.elf
# Check the last shader compiled before the issue
ls -lt _Shaders/ | head -5
# Validate with spirv-val
spirv-val --target-env vulkan1.3 _Shaders/0001_new_shader_ps_*.spv
```

## Step 4: Fix or Workaround

### Fix (Preferred)

If the root cause is clear and the fix is contained:
1. Create a branch: `fix/Kyty-XXX-description`
2. Implement the fix
3. Add a test case if possible
4. Update the grand plan status
5. Commit to `main-with-all-fixes`
6. Poll CI until green

### Workaround (Quick)

If the root cause is unclear or the fix is too large:
1. Add a hack flag to `hack_features.h` + `hack_features.cpp`
2. Gate the problematic path
3. Add to the hardcoded map for the affected game
4. Document in `docs/game_hacks.md`
5. Commit to `main-with-all-fixes`

## Step 5: Document

Update the compatibility list:
- If the game now boots: mark as "Playable" or "In-Game"
- If a hack was applied: note which hacks are needed
- Link the issue to the relevant Kyty task

## Worked Example: Sifu (PPSA01491)

1. **Reproduce:** Game crashes on boot with `EXIT` in the log.
2. **Categorize:** Crash (assertion in depth-texture path).
3. **Isolate:** PM4 dump shows the last packet was
   `IT_SET_CONTEXT_REG` for a depth format. Shader log shows a
   comparison-texture opcode.
4. **Root cause:** Kyty-006 depth/comparison-texture revert cluster.
5. **Decision:** Workaround — add `DisableAsyncCompute` +
   `ForceDepthRangeRestricted` hack flags.
6. **Document:** Added to hardcoded map + `game_hacks.md`.
7. **Result:** Game boots and reaches the main menu.

## Worked Example: Returnal (PPSA01256)

Same root cause as Sifu (depth-texture revert cluster). Same workaround
applied. See `docs/game_hacks.md` for the hack flags.
