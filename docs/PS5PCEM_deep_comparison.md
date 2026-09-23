# PS5PCEM Deep Comparison (KytyPS5/AbdoPS5 vs PS5PCEM)

**Date**: 2026-09-23  
**Source repo**: https://github.com/iStark/PS5PCEM  
**Language**: Zig (vs KytyPS5's C++)  
**License**: GPL-3.0-or-later  
**Status**: Early prototype, but with **8 playable/completable games** and several more reaching gameplay  

---

## Executive Summary

PS5PCEM is a Zig-based PS5 emulator by iStark that has achieved surprising
compatibility for a solo project: 8 games confirmed playable/completable,
including Terminator 2D, Jets 'n' Guns 2, Asterix & Obelix, Cat Quest III,
Dreaming Sarah, Quake II (2023), Jurassic Park Classic Games, and others
reaching menu/gameplay. Ghost of Yōtei reaches in-game scenes.

Key architectural differences from KytyPS5:
- Written in **Zig** (memory-safe, no C++ templates/RTTI overhead)
- **No external dependencies** beyond Zig stdlib (no Qt, no SDL2, no vcpkg)
- **Runtime Vulkan loader** (dlopen/dlsym, no link-time loader)
- **PM4-centric GPU model** (decodes the raw command stream directly)
- **Optional typed IR pipeline** with SSA/constant folding/DCE
- **Async pipeline compiler** (background thread for first-use shaders)
- **Pipeline cache persistence** (vulkan_pipeline_cache.bin, 4 GiB limit)
- **Image alias registry** (coherency between color/depth/storage/sampled views)
- **Per-subresource Vulkan layout tracking** (per aspect, mip, layer)
- **Opt-in GPU page tracker** (16 KiB pages, generation-tracked)
- **Native HID input** (DualSense/DualShock 4 without translation layer)
- **Windows launcher** with game library, cover art, input profiles
- **NID computation** (computes SHA-1 NIDs from names, not hardcoded)

---

## Architecture Comparison

| Subsystem | KytyPS5/AbdoPS5 | PS5PCEM | Notes |
|-----------|-----------------|---------|-------|
| **Language** | C++20 | Zig 0.16 | Zig is memory-safe, compiles faster |
| **Build** | CMake + vcpkg + Qt + SDL2 | Zig build.zig (zero deps) | PS5PCEM is dramatically simpler to build |
| **GUI/Launcher** | Qt6 (QMainWindow, sidebar, grid view) | Native Win32 (launcher.zig) | PS5PCEM launcher is simpler but functional |
| **Vulkan** | Link-time loader (Vulkan-Hpp) | Runtime dlopen/dlsym | PS5PCEM doesn't need Vulkan SDK to build |
| **GPU model** | AGC HLE (emulates the Graphics5 library) | PM4 direct decode (raw command stream) | PS5PCEM decodes what the game actually sends |
| **Shader recompiler** | RDNA2 → IR → SPIR-V (mandatory) | RDNA2 → IR → SPIR-V (optional, default is direct) | PS5PCEM has an opt-in IR pipeline |
| **Shader cache** | skipList.cpp/h (Kyty-015) | vulkan_pipeline_cache.bin (4 GiB, persisted) | PS5PCEM persists the Vulkan driver cache |
| **Async compilation** | No | pipeline_compiler.zig (opt-in, single worker) | Big perf win for first-use shaders |
| **Memory** | GuestBackingStore (shared memory) | backing_store.zig (sparse shared object) | Both use sparse backing, PS5PCEM has explicit generation tracking |
| **Input** | SDL2 GameController | Native HID + XInput fallback | PS5PCEM reads DualSense directly |
| **Audio** | SDL2 audio (Kyty-011: cubeb opt-in) | Native 48 kHz audio device | Both work, PS5PCEM avoids SDL dependency |
| **ELF loader** | Custom ELF64 parser | Custom ELF64/SELF parser | Both have self.module loading |
| **NID resolution** | Externalized CSV database (Kyty-004) | Computed from names (SHA-1) | PS5PCEM computes NIDs at runtime, catches typos |
| **HLE modules** | C++ LIB_FUNC registrations | Zig symbol registry with version+library keys | PS5PCEM's lookup is keyed by ID + library + module + version |
| **Error handling** | EXIT() macro (crash on error) | Checked failures (explain + crash) | Both crash, PS5PCEM gives better diagnostics |
| **Testing** | CTest + custom test executables | Zig built-in test framework | Zig tests are inline (`test "name" {}`) |
| **Packaging** | ZIP from CI (no installer) | Inno Setup (.iss) + PowerShell scripts | PS5PCEM has a proper Windows installer |
| **Docs** | 11 .md files in docs/ | 47 .md files in docs/ (detailed architecture docs) | PS5PCEM has much better documentation |

---

## Key Improvements to Port to AbdoPS5

### 1. Pipeline Cache Persistence (HIGH PRIORITY)
**PS5PCEM**: `vulkan_pipeline_cache.bin` (4 GiB limit, persisted between runs)  
**AbdoPS5**: No Vulkan pipeline cache persistence  
**Impact**: First-use shader compilation is the #1 performance bottleneck. Persisting the Vulkan driver cache means second launches are dramatically faster.  
**Effort**: 1-2 days (vkPipelineCacheCreateInfo + file I/O)  
**Maps to**: New Kyty item or Kyty-015 enhancement

### 2. Async Pipeline Compiler (HIGH PRIORITY)
**PS5PCEM**: `pipeline_compiler.zig` — single-worker FIFO, creates pipelines on a background thread  
**AbdoPS5**: All pipeline creation is synchronous (blocks the render thread)  
**Impact**: First-use shaders cause frame hitches. Background compilation overlaps with rendering.  
**Effort**: 3-5 days (thread + queue + fallback to inline)  
**Maps to**: New Kyty item

### 3. Per-Subresource Vulkan Layout Tracking (MEDIUM PRIORITY)
**PS5PCEM**: Tracks image layout per aspect, mip, array layer  
**AbdoPS5**: Coarser layout tracking  
**Impact**: Correct barrier generation for complex render passes (mip chains, cube faces, layered rendering)  
**Effort**: 1 week  
**Maps to**: Enhancement to existing renderer

### 4. Image Alias Registry (MEDIUM PRIORITY)
**PS5PCEM**: Range-based alias registry across color/depth/storage/sampled views, tracks canonical writer + generation  
**AbdoPS5**: Separate cache invalidation paths  
**Impact**: Correct coherency when the same memory is used as render target + texture + storage image  
**Effort**: 1-2 weeks  
**Maps to**: Enhancement to existing image cache

### 5. NID Computation from Names (LOW PRIORITY)
**PS5PCEM**: Computes SHA-1(name + salt) at registration time, catches typos  
**AbdoPS5**: Hardcoded NID strings in LIB_FUNC registrations  
**Impact**: Catches registration bugs at startup instead of at game runtime  
**Effort**: 2-3 days  
**Maps to**: Enhancement to Kyty-004

### 6. Runtime Vulkan Loader (LOW PRIORITY)
**PS5PCEM**: dlopen/dlsym, no Vulkan SDK needed at build time  
**AbdoPS5**: Link-time Vulkan-Hpp dependency  
**Impact**: Simpler builds, no Vulkan SDK required  
**Effort**: 1 week (big refactor of all vk:: calls)  
**Maps to**: Build simplification

### 7. Windows Installer (LOW PRIORITY)
**PS5PCEM**: Inno Setup (.iss) + PowerShell scripts  
**AbdoPS5**: ZIP from CI  
**Impact**: Better user experience for Windows distribution  
**Effort**: 1 day  
**Maps to**: Kyty-021 (packaging)

### 8. Opt-in GPU Page Tracker with Generation (MEDIUM PRIORITY)
**PS5PCEM**: 16 KiB page tracking by generation, arms writable pages read-only, handles first write as invalidation fault  
**AbdoPS5**: Has fault buffer processing (Kyty-018) but no generation tracking  
**Impact**: More precise cache invalidation, fewer false positives  
**Effort**: 1 week  
**Maps to**: Enhancement to Kyty-018

### 9. PM4-Centric GPU Model (RESEARCH)
**PS5PCEM**: Decodes the raw PM4 command stream directly, doesn't HLE the AGC library  
**AbdoPS5**: HLE the AGC library (agc.cpp, 4455 lines)  
**Impact**: PM4 decoding is more accurate (sees exactly what the game sends) but harder to implement  
**Effort**: Months (fundamental architecture difference)  
**Maps to**: Research topic, not a port candidate

### 10. Typed IR Pipeline with SSA (RESEARCH)
**PS5PCEM**: Optional CFG/SSA optimization, constant folding, DCE  
**AbdoPS5**: Direct decode → SPIR-V (no optional IR optimization)  
**Impact**: Better shader optimization, smaller SPIR-V  
**Effort**: Months  
**Maps to**: Already in KytyPS5's scalar_provenance_tests (partial)

---

## Compatibility Comparison

PS5PCEM has **8 playable/completable games** vs KytyPS5's current state
(boots to various stages but AVX + Vulkan requirements limit testing):

| Game | PS5PCEM | AbdoPS5 | Notes |
|------|---------|---------|-------|
| Terminator 2D: No Fate | ✅ Playable | Untested | Simple 2D game |
| Jets 'n' Guns 2 | ✅ Playable | Untested | 2D shooter |
| Asterix & Obelix | ✅ Playable | Untested | 2D platformer |
| Cat Quest III | ✅ Playable | Untested | 2D RPG |
| Dreaming Sarah | ✅ Playable | Untested | 2D adventure |
| Quake II (2023) | ✅ Playable (dark) | Untested | 3D, deferred rendering |
| Jurassic Park Classic | ✅ Playable | Untested | Collection |
| Big Helmet Heroes | Menu only | Untested | 3D Unity |
| Ghost of Yōtei | Intro + scenes | Untested | AAA title |
| The Precinct | Menu + intro | Untested | 3D Unity |
| Tetris Effect | First frame | Untested | UE5 |

PS5PCEM targets simpler 2D/indie games first — the playable titles are mostly
2D. KytyPS5 targets AAA titles (RE2, Returnal, Spider-Man) which are harder.

---

## Recommendations for the Grand Plan

### New items to add:

1. **Kyty-031: Pipeline cache persistence** — Save/load vkPipelineCache between runs. ~1-2 days.
2. **Kyty-032: Async pipeline compiler** — Background thread for first-use shader compilation. ~3-5 days.
3. **Kyty-033: Per-subresource Vulkan layout tracking** — Per aspect/mip/layer barrier generation. ~1 week.
4. **Kyty-034: Image alias registry** — Coherency tracking across render target + texture + storage views. ~1-2 weeks.
5. **Kyty-035: NID computation from names** — SHA-1(name + salt) at registration, catches typos. ~2-3 days.
6. **Kyty-036: Windows installer** — Inno Setup for proper distribution. ~1 day.
7. **Kyty-037: GPU page generation tracking** — 16 KiB pages with generation-based invalidation. ~1 week.

### Priority order:
1. Kyty-031 (pipeline cache) — biggest bang-for-buck, trivial effort
2. Kyty-032 (async compiler) — eliminates first-use hitches
3. Kyty-021 (packaging) — already in the plan, add installer
4. Kyty-035 (NID computation) — easy win, catches bugs
5. Kyty-033/034/037 (rendering/memory) — deeper work

---

## What PS5PCEM Does Better

1. **Build simplicity**: `zig build` — zero dependencies, cross-compiles to Windows/Linux/macOS
2. **Documentation**: 47 .md files with detailed architecture docs per subsystem
3. **Shader pipeline**: Opt-in IR/SSA with constant folding and DCE
4. **Pipeline caching**: Persisted Vulkan driver cache between runs
5. **Input**: Native HID (DualSense without SDL translation layer)
6. **NID safety**: Computes NIDs from names, catches typos at registration
7. **Memory tracking**: Generation-based page tracking for precise invalidation
8. **PM4 decode**: Sees the actual command stream, not an HLE abstraction

## What AbdoPS5 Does Better

1. **AAA game focus**: Targets RE2/Returnal/Spider-Man (PS5PCEM targets indie 2D)
2. **Cross-platform GUI**: Qt6 launcher works on Windows + Linux + macOS
3. **IOMMU + TMR modeling**: Hardware devices modeled (Kyty-016/017)
4. **Cubeb audio backend**: Lower latency than SDL on Linux (Kyty-011)
5. **Shader opcode tracking**: Runtime opcode coverage tracking (Kyty-013)
6. **RE tools**: Standalone ELF/NID/PKG/SFO inspector tools (Kyty-012)
7. **PKG extraction**: Full FPKG extraction in the launcher (Kyty-005)
8. **Vulkan relax requirements**: Works on older GPUs (Kyty-016 vulkan-relax)
9. **DualSense haptics**: HapticPlayer for PCM audio playback (Kyty-014)
10. **Screenshot module**: Alt+F12 + ShareCaptureScreenshot (Kyty-022)
