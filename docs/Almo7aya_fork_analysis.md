# Almo7aya/KytyPS5 Fork Analysis

> **Source:** https://github.com/Almo7aya/KytyPS5
> **Forked from:** KytyPS5/KytyPS5
> **Stars:** 7 | **Forks:** 3
> **Analysis date:** 2026-09-21

## Overview

Almo7aya's fork is a quality-focused fork that prioritizes stability and performance over new features. Unlike the Shadlix fork (which adds GUI features and hacks) or our AbdoPS5 fork (which adds tooling and security fixes), Almo7aya focuses on:

1. **Performance optimizations** — 4 commits improving GPU/shader/buffer performance
2. **GPU render correctness** — 7 commits fixing rendering bugs
3. **Loader stability** — fault context printing, guest exception handling
4. **Test cleanup** — removed/simplified 8,765 lines of test code

## Key Improvements

### Performance (4 commits)
- Optimize asynchronous graphics pipelines
- Avoid GPU drains for CPU readbacks of GPU-written buffers
- Reduce synchronization overhead
- Reduce shader translation overhead

### GPU/Render Fixes (7 commits)
- Gate storage image usage by format support
- Honor mapped resource extents
- Sanitize malformed and unmapped descriptors
- Make synthetic occlusion results safe
- Handle deferred DCC metadata clears
- Substitute empty color grading LUT
- Centralize depth shading-rate handling

### Note
Cherry-picks were reverted due to cascading compilation dependencies.
The Almo7aya fork modifies shared headers that require ALL of their
changes to be applied together — partial cherry-picks don't compile.
To use Almo7aya's improvements, merge their entire branch instead.
