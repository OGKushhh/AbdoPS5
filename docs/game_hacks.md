# Per-Game Hack Flags

**Status:** Active (Kyty-003)
**Last updated:** 2026-09-23

## Overview

AbDoPS5 supports per-game hack flags that gate render paths at runtime,
allowing users to work around emulator bugs that block specific games.
The framework was introduced by Kyty-003 and is inspired by fpPS4's CLI
hack flags and the shadPS4 Shadlix fork's `HackFeatures` class.

## Two Layers of Configuration

### 1. Hardcoded Map (`src/loader/hack_features.cpp`)

Quick fixes for known-broken games, compiled into the emulator binary:

```cpp
const std::unordered_map<std::string, std::string> kHardcodedGameHacks = {
    {"PPSA01491", "DisableAsyncCompute,ForceDepthRangeRestricted"}, // Sifu
    {"PPSA01256", "DisableAsyncCompute,ForceDepthRangeRestricted"}, // Returnal
    {"PPSA01325", "SkipShaderAssert"}, // Ghost of Tsushima
};
```

### 2. Data-Driven Overrides (`data/game_hacks.json`)

Community-editable overrides that are OR'd with the hardcoded map:

```json
{
  "PPSA01491": ["DisableAsyncCompute", "ForceDepthRangeRestricted", "SkipShaderAssert"]
}
```

The JSON overrides **add to** the hardcoded map — they don't replace it.
This lets users add `SkipShaderAssert` to Sifu without losing the
hardcoded `DisableAsyncCompute` + `ForceDepthRangeRestricted`.

## Available Hack Flags

| Flag | Effect | Use Case |
|---|---|---|
| `DepthDisable` | Disable depth buffer | Depth-texture crashes (Kyty-006) |
| `ComputeDisable` | Disable compute shaders | Debugging |
| `DisableAsyncCompute` | Force synchronous compute | Kyty-006 async-completion revert |
| `DisableSRGB` | Disable sRGB display | Color space issues |
| `DisableFMV` | Disable full-motion video | FMV playback crashes |
| `SkipUnknownTiling` | Skip unknown texture tiling | Unknown tile modes |
| `ForceDepthRangeRestricted` | Clamp depth to [0,1] | VK_EXT_depth_range_unrestricted unavailable |
| `UseColorImageForComparison` | Fall back to color images for comparison textures | Lossy workaround (Kyty-006) |
| `SkipShaderAssert` | Skip shader ASSERT failures | Kyty-001 missing opcodes |
| `ImageLoadNoReload` | Never reload textures | Performance hack |
| `MemoryBound` | Limit GPU-allocated memory | iGPU workaround |
| `ForcePs4ProMode` | Force PS4 Pro mode | PS4 Pro enhanced features |
| `ForceDevKitMode` | Force DevKit mode | Debug features |

## Worked Example: Sifu (PPSA01491)

**Problem:** Sifu crashes on boot due to the depth/comparison-texture revert
cluster (Kyty-006). The async-completion interrupt path was destabilized
by 5 reverts between Aug 25 and Sep 7.

**Hardcoded fix:**
```cpp
{"PPSA01491", "DisableAsyncCompute,ForceDepthRangeRestricted"},
```

**What it does:**
- `DisableAsyncCompute`: Forces synchronous compute, sidestepping the
  async-completion interrupt path that the reverted PRs destabilized.
- `ForceDepthRangeRestricted`: Clamps depth ranges to [0,1] — the safe
  path that doesn't require `VK_EXT_depth_range_unrestricted`.

**User override (adding SkipShaderAssert):**
```json
{
  "PPSA01491": ["DisableAsyncCompute", "ForceDepthRangeRestricted", "SkipShaderAssert"]
}
```

## Worked Example: Returnal (PPSA01256)

**Problem:** Same depth-texture revert cluster as Sifu.

**Hardcoded fix:**
```cpp
{"PPSA01256", "DisableAsyncCompute,ForceDepthRangeRestricted"},
```

## Adding a New Game Hack

1. **Identify the bug:** Reproduce the crash/hang and identify which
   render path is responsible.

2. **Add the hack flag** to `GameHack` enum in `hack_features.h`:
   ```cpp
   NewHackName = 1u << 13u,
   ```

3. **Add to the name map** in `hack_features.cpp`:
   ```cpp
   {"NewHackName", GameHack::NewHackName},
   ```

4. **Gate the render path** in the appropriate source file:
   ```cpp
   if (Loader::HackFeatures::HasHack(Loader::GameHack::NewHackName)) {
       // Take the safe path
   } else {
       // Take the default path
   }
   ```

5. **Add to the hardcoded map** if the game is known-broken:
   ```cpp
   {"PPSA_NEW_GAME", "NewHackName"},
   ```

6. **Update `data/game_hacks.json`** comment to list the new flag.

## CLI Override

Users can enable hacks at runtime without editing JSON:
```bash
./kyty_emulator --enable-hack "SkipShaderAssert" game.elf
```

Multiple hacks can be comma-separated:
```bash
./kyty_emulator --enable-hack "SkipShaderAssert,DisableAsyncCompute" game.elf
```
