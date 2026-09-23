# Stub Return Value Policy

**Status:** Active (Kyty-002)
**Last updated:** 2026-09-23

## Overview

This document defines the default return value policy for unimplemented
syscalls (stubs) in AbDoPS5. The policy was established by Kyty-002 and
governs how `CommonStub`, `UnresolvedStub`, and `UnknownStub` behave
when a game calls a function that the emulator has not yet implemented.

## The Policy

**Unimplemented syscalls return `ORBIS_KERNEL_ERROR_ENOSYS` (`0x8002004E`,
`-2147352498`), not `0`.**

This is a deliberate departure from the previous behavior (returning `0`
for success). The rationale:

1. **Loud failure beats silent success.** When a stub returns `0`, the game
   believes the call succeeded and proceeds to use the (non-existent)
   result. This typically manifests as a NULL pointer dereference or
   invalid memory access far from the root cause, making debugging
   extremely difficult.

2. **ENOSYS is the correct POSIX semantics.** The `ENOSYS` error code
   ("Function not implemented") is the standard way for a system to
   signal that a syscall exists in the ABI but has no implementation.

3. **Games that gracefully handle ENOSYS will degrade gracefully.** Many
   games check for `ENOSYS` and fall back to a different code path
   (e.g., a software renderer when the GPU API is unavailable).

## Where the Policy Lives

- **`src/loader/runtimeLinker.cpp`**: The `EmitUnresolvedImportStub()`
  function generates the x86 trampoline that returns
  `KERNEL_ERROR_ENOSYS` (`0xFFFFFFFF8002004E`) for unresolved imports.
- **`src/libs/errno.h`**: Defines `KERNEL_ERROR_ENOSYS`,
  `POSIX_ENOSYS`, and `NET_ERROR_ENOSYS` with their full hex values.

## When to Override the Policy (Explicit No-Ops)

Some syscalls are **known-safe to no-op** — they have no observable side
effect the game depends on, and returning `ENOSYS` would cause the game
to fail unnecessarily. For these, implement an explicit no-op that
returns `ORBIS_OK` (`0`):

```cpp
// Example: sceKernelMlockall (Kyty-003)
// The game calls this to lock all mapped pages into RAM, but the
// emulator's page manager doesn't evict pages anyway, so the call is
// a true no-op. Returning ENOSYS would make the game think memory
// locking is unsupported and fall back to a slower path.
s32 PS4_SYSV_ABI sceKernelMlockall(s32 flags) {
    LOG_TRACE("sceKernelMlockall(flags=0x%x) → ORBIS_OK (no-op)\n", flags);
    return ORBIS_OK;
}
```

**Criteria for an explicit no-op:**
- The syscall has no side effect the emulator needs to model
- Returning `ENOSYS` would cause the game to fail or degrade
- The behavior is verified on real hardware (the call truly is a no-op)
- The implementation is documented with a comment explaining why

## Adding a New Stub

1. **Default path**: If the syscall is not yet implemented, leave it as
   a `STUB()` entry in `aerolib.inl`. It will return `ENOSYS` automatically.

2. **Explicit no-op**: If the syscall is known-safe to no-op, move it
   out of `aerolib.inl` and implement it in the appropriate library
   file (e.g., `src/libs/libKernel.cpp`), returning `ORBIS_OK` with a
   `LOG_TRACE` (not `LOG_ERROR`, since it's expected behavior).

3. **Full implementation**: If the syscall has real semantics,
   implement it properly and register it via `LIB_FUNC(nid, function)`.

## Debugging Stub Calls

To see which stubs are being called during a game session:

```bash
# Enable verbose logging (config or --log-level=trace)
# Look for "returning ENOSYS" messages in the log
grep "returning ENOSYS" _kyty.txt | sort | uniq -c | sort -rn
```

The most frequently-called stubs are the highest-priority targets for
real implementation.
