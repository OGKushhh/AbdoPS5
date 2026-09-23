# Contributing to AbDoPS5

Thank you for your interest in contributing to AbDoPS5! This document
covers the basics of getting started and points to the detailed
documentation for specific topics.

## Build

```bash
# Clone with submodules
git clone --recursive https://github.com/OGKushhh/AbdoPS5.git
cd AbdoPS5

# Configure (Linux)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Configure (Windows, with vcpkg)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build
```

See `README.md` for platform-specific build instructions.

## Development Workflow

1. **All work goes into `main-with-all-fixes`** — no feature branches.
2. **After every push, poll CI** — the workflow takes 7-8 minutes.
   Fix failures instantly; do not ask for permission.
3. **Use tabs for indentation** — the project uses `UseTab: ForIndentation`
   (see `src/.clang-format`).
4. **Run the static analyzer** before committing:
   ```bash
   python3 scripts/verify_kyty_0XX.py
   ```
5. **Update the grand plan** when a task is completed:
   - Change status from 🔴 to 🟢 in the summary table
   - Check the acceptance criteria boxes in the detail section

## Documentation

### Developer Guides

- [Stub Return Value Policy](docs/stub_policy.md) — when stubs should
  return `ENOSYS` vs `ORBIS_OK` (Kyty-002)
- [Per-Game Hack Flags](docs/game_hacks.md) — how to add and use hack
  flags to work around emulator bugs (Kyty-003)
- [Compatibility Issue Triage Workflow](docs/triage_workflow.md) — how
  to triage and fix compatibility issues (Kyty-030)

### Architecture Documents

- [Grand Improvement Plan](docs/KytyPS5_grand_improvement_plan.md) —
  master plan with 40 tasks, each with root cause, acceptance criteria,
  and status
- [PS5 Hardware Reference](docs/PS5_hardware_reference.md) — PS5 GPU
  architecture notes
- [PS5PCEM Deep Comparison](docs/PS5PCEM_deep_comparison.md) —
  comparison with the Zig-based PS5PCEM emulator

### Investigation Notes

- [Kyty-006: Depth Texture Investigation](docs/Kyty-006_depth_texture_investigation.md)
- [Kyty-007: VCC/EXEC Mask Investigation](docs/Kyty-007_vcc_exec_mask_investigation.md)
- [RE3 + FIFA16 Root Cause Analysis](docs/RE3_FIFA16_root_cause_analysis.md)

## Coding Standards

- **C++20** — the project uses `-std=c++20`
- **Tabs for indentation** — not spaces
- **No exceptions** — the project builds with `-fno-exceptions`; use
  `EXIT()` / `EXIT_IF()` for fatal errors
- **No RTTI** — the project builds with `-fno-rtti` for the emulator
  (the launcher uses RTTI for Qt)
- **Logging** — use `LOGF()` / `LOGF_COLOR()` / `LOG_TRACE` etc., not
  `printf` or `std::cout`

## Adding New Library Stubs

1. Check `docs/stub_policy.md` for the return value policy
2. Add the function to the appropriate library file (e.g., `src/libs/libKernel.cpp`)
3. Register it via `LIB_FUNC("NID", function)` or `LIB_FUNC_NAME("symbol_name", function)` (Kyty-035)
4. If the NID is known but the implementation is not, leave it as `STUB()` in `aerolib.inl`

## Reporting Issues

1. Check the [triage workflow](docs/triage_workflow.md) first
2. Include: game serial, AbDoPS5 build version, OS + GPU, log file (`_kyty.txt`)
3. If possible, include a PM4 dump (`--dump-pm4 pm4.txt`, Kyty-039)

## License

AbDoPS5 is licensed under the GNU General Public License v2 or later
(GPL-2.0-or-later). See [LICENSE](LICENSE) for details.
