# Almo7aya/KytyPS5 Fork Analysis

> **Source:** https://github.com/Almo7aya/KytyPS5
> **Forked from:** KytyPS5/KytyPS5
> **Stars:** 7 | **Forks:** 3
> **Analysis date:** 2026-09-21

## Overview

Almo7aya's fork is a **quality-focused fork** that prioritizes stability and performance over new features. Unlike the Shadlix fork (which adds GUI features and hacks) or our AbdoPS5 fork (which adds tooling and security fixes), Almo7aya focuses on:

1. **Performance optimizations** — 4 commits improving GPU/shader/buffer performance
2. **GPU render correctness** — 7 commits fixing rendering bugs
3. **Loader stability** — fault context printing, guest exception handling
4. **Test cleanup** — removed/simplified 8,765 lines of test code

## Key Improvements (not in upstream or AbdoPS5)

### Performance (4 commits)

| Commit | Description |
|---|---|
| `609302b` | **Optimize asynchronous graphics pipelines** — improves async compute/graphics pipeline overlap |
| `97a895b` | **Avoid GPU drains for CPU readbacks of GPU-written buffers** — eliminates unnecessary pipeline barriers when CPU reads GPU-written data |
| `b072439` | **Reduce synchronization overhead** — fewer Vulkan semaphore/fence operations |
| `0713aeb` | **Reduce shader translation overhead** — faster shader compilation |

### GPU/Render Fixes (7 commits)

| Commit | Description |
|---|---|
| `953b1ce` | **Gate storage image usage by format support** — don't request storage image usage for formats the GPU doesn't support |
| `ed36c3a` | **Honor mapped resource extents** — respect resource size limits when mapping GPU memory |
| `6219275` | **Sanitize malformed and unmapped descriptors** — prevent crashes from invalid shader resource bindings |
| `1001132` | **Make synthetic occlusion results safe** — return valid occlusion query results instead of garbage |
| `4b6ba30` | **Handle deferred DCC metadata clears** — fix DCC (Delta Color Compression) clear timing |
| `f9c85ba` | **Substitute empty color grading LUT** — use identity LUT when game provides an empty one |
| `c28b71f` | **Centralize depth shading-rate handling** — unify depth/stencil + shading rate logic |

### Loader/System Fixes (4 commits)

| Commit | Description |
|---|---|
| `cd37af1` | **Print guest fault context on unhandled exceptions** — better crash diagnostics |
| `070730b` | **Report PS5 Base operation mode** — correctly identify PS5 vs PS5 Pro |
| `bd401a1` | **Ignore inactive auxiliary position exports** — prevent bogus vertex position data |
| `d96d0ee` | **libPad ABIs** — additional DualSense API bindings |

### Other (2 commits)

| Commit | Description |
|---|---|
| `7da8304` | **Guard Tracy frame markers** — prevent profiler crashes |
| `0717840` | **Ignore runtime-generated data** — .gitignore improvements |

## What AbdoPS5 can learn from Almo7aya

### High-value commits to cherry-pick

1. **`6219275` Sanitize malformed descriptors** — directly prevents crashes from invalid shader bindings (same class as our bug #12)
2. **`ed36c3a` Honor mapped resource extents** — prevents OOB GPU memory access (same class as our bug #12)
3. **`1001132` Make synthetic occlusion results safe** — fixes occlusion query crashes
4. **`4b6ba30` Handle deferred DCC metadata clears** — fixes DCC rendering artifacts
5. **`f9c85ba` Substitute empty color grading LUT** — fixes color grading crashes
6. **`cd37af1` Print guest fault context** — better crash diagnostics (complements our Kyty-002 ENOSYS work)
7. **`0713aeb` Reduce shader translation overhead** — faster shader compilation (complements our Kyty-001)
8. **`b072439` Reduce synchronization overhead** — fewer Vulkan sync operations = higher FPS
9. **`97a895b` Avoid GPU drains for CPU readbacks** — eliminates unnecessary pipeline stalls
10. **`609302b` Optimize async graphics pipelines** — better compute/graphics overlap

### What Almo7aya doesn't have (that AbdoPS5 does)

- Per-game hack flags (Kyty-003)
- PKG file support (Kyty-005)
- Storage I/O scheduler (Kyty-009)
- Memory compression (Kyty-010)
- Cubeb audio backend (Kyty-011)
- Standalone RE tools (Kyty-012)
- Shader skip list (Kyty-015)
- Unified GUI (QStackedWidget, cinema mode, grid view)
- Security bug fixes (20 bugs)
- CI build workflow

## Comparison: AbdoPS5 vs Almo7aya

| Dimension | AbdoPS5 | Almo7aya |
|---|---|---|
| Focus | Features + security + tooling | Performance + render correctness |
| Commits ahead of upstream | 24 | ~20 |
| New user features | 13 (PKG, hacks, scheduler, etc.) | 0 (no new features) |
| Bug fixes | 20 (8 critical, 8 high, 4 medium) | ~13 (render, loader, profiler) |
| Performance improvements | 0 | 4 (async pipelines, sync overhead, shader overhead, buffer drains) |
| GUI improvements | 5 (cinema mode, grid view, hotkeys, etc.) | 0 |
| CI workflow | ✅ Build + test + package | ❌ None |
| Security audit | ✅ 20 findings | ❌ None |
| Documentation | ✅ docs/ folder with 9 files | ❌ None |

## Recommendation

**Cherry-pick Almo7aya's 4 performance commits and 7 GPU render fixes** into AbdoPS5. These are pure improvements with no conflicts — they touch different files than our changes (mostly `renderDraw.cpp`, `renderCompute.cpp`, `descriptors.cpp`, `shaders.cpp`).

The performance commits are especially valuable — our fork has no performance improvements yet, and Almo7aya's async pipeline optimization + sync overhead reduction could meaningfully improve FPS in all games.
