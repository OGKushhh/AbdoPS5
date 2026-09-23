# Bloodborne Compatibility Profile

**Status:** 🟡 In Progress — hack flags configured, needs testing
**Last updated:** 2026-09-23

## Game Info

| Field | Value |
|---|---|
| **Title** | Bloodborne |
| **Developer** | FromSoftware |
| **Engine** | Custom (older GCN, 32-bit integer compares) |
| **CUSA IDs** | CUSA00900 (US), CUSA00231 (EU), CUSA03173 (GOTY/Old Hunters) |
| **PS4 Pro Enhanced** | Yes (1080p → upscaled) |
| **PC Port** | ❌ None (console exclusive) |

## Why Bloodborne is a Good Emulation Target

From our root cause analysis (see `docs/RE3_FIFA16_root_cause_analysis.md`):

> Bloodborne works because From Software's shaders happen to only use the
> implemented subset [of GCN opcodes]. RE Engine games will fail because
> they push into the unimplemented corners.

- Uses **32-bit integer compares** (not 64-bit like RE Engine → no V_CMP_U64 issue)
- Uses **older GCN features** (well-documented, well-supported by the shader recompiler)
- Doesn't **bypass the Gnm driver** (uses standard rendering paths)
- Uses **standard texture tiling** (no exotic tile modes)

## Configured Hack Flags

From `src/loader/hack_features.cpp` (hardcoded):

```
CUSA00900: BloodborneAudioFix, Pm4Type0Fix, MemoryBound
CUSA00231: BloodborneAudioFix, Pm4Type0Fix, MemoryBound
CUSA03173: BloodborneAudioFix, Pm4Type0Fix, MemoryBound
```

Optional overrides in `data/game_hacks.json`:
```json
{
  "CUSA00900": ["ForcePs4ProMode"],
  "CUSA00231": ["ForcePs4ProMode"],
  "CUSA03173": ["ForcePs4ProMode"]
}
```

## Known Issues (from shadPS4 Shadlix fork analysis)

### 1. Audio Loss
**Problem:** Bloodborne loses audio after a few minutes of gameplay.
**Fix:** `BloodborneAudioFix` hack flag (ported from rainvmaker's shadPS4 fix).
**Implementation needed:** The hack flag is declared but not yet wired into
the audio path. Needs `if (HackFeatures::HasHack(GameHack::BloodborneAudioFix))`
gate in `src/libs/audio.cpp` or `src/libs/ngs2.cpp`.

### 2. PM4 Type 0 Packets
**Problem:** Bloodborne sends PM4 Type 0 packets that the emulator
doesn't handle correctly.
**Fix:** `Pm4Type0Fix` hack flag.
**Implementation needed:** Gate in `src/graphics/guest_gpu/graphicsRun.cpp`
`ProcessPm4()` to handle Type 0 packets differently.

### 3. High Memory Usage
**Problem:** Bloodborne is a large game (~5 GB working set). On hosts
with 8 GB RAM, the emulator can OOM.
**Fix:** `MemoryBound` hack flag + `--memory-compression 2` CLI flag (Kyty-010).
**Status:** Kyty-010 (memory compression) is already implemented.

### 4. Predication
**Problem:** The Shadlix fork reverts predication handling for Bloodborne.
**Status:** Our codebase has predication handling in `ProcessPm4()` (the
`ShouldSkipPredicatedPackets` path). May need a hack flag if predication
causes incorrect rendering.

## Debugging Workflow

When testing Bloodborne, use the debugging tools:

```bash
# Enable PM4 dump to capture the command stream
./kyty_emulator --dump-pm4 _Pm4Dump.txt --game /path/to/bloodborne

# Enable shader logging
./kyty_emulator --dump-pm4 _Pm4Dump.txt --game /path/to/bloodborne \
  --shader-log-direction File

# Enable memory compression (recommended for 8GB hosts)
./kyty_emulator --dump-pm4 _Pm4Dump.txt --game /path/to/bloodborne \
  --memory-compression 2

# Check the log for errors
grep -E "EXIT|EXIT_IF|error" _kyty.txt | head -20
```

## Triage Checklist

- [ ] Boot to main menu (crash point: ???)
- [ ] Start a new game (crash point: ???)
- [ ] First area loads (crash point: ???)
- [ ] Audio works for >5 minutes (needs BloodborneAudioFix implementation)
- [ ] No visual glitches (check depth textures, tiling)
- [ ] Performance ≥ 20 FPS (check with Tracy profiler)

## References

- shadPS4 Shadlix fork: `docs/shadPS4_deep_comparison.md` section on Bloodborne
- Root cause analysis: `docs/RE3_FIFA16_root_cause_analysis.md` (comparison with RE Engine)
- Hack flags: `docs/game_hacks.md`
- Triage workflow: `docs/triage_workflow.md`
