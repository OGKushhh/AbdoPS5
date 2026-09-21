# Shader Skip Lists

Place per-game shader skip lists here as `<PPSA_ID>.json`.

Format:
```json
{
  "skips": [
    "0x1234567890ABCDEF",
    "0xFEDCBA0987654321"
  ],
  "skip_all_compute": false,
  "skip_all_graphics": false
}
```

- `skips`: array of shader hash hex strings to skip during compilation
- `skip_all_compute`: if true, skip ALL compute shaders (debugging only)
- `skip_all_graphics`: if true, skip ALL graphics shaders (debugging only)

When a shader is skipped, the recompiler logs a warning and returns a
no-op shader. This is useful for:
- Known-broken shaders that crash the emulator
- Shaders using unimplemented opcodes (until Kyty-001 is verified)
- Debugging specific shader issues without recompiling

A global skip list can also be placed at `data/shader_skips.json` (applies to all games).
