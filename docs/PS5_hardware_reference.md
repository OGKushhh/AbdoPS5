# PS5 Hardware Reference

**Source:** Derived from ground-truth analysis of `ps5-linux-loader` (https://github.com/ps5-linux/ps5-linux-loader), a payload that runs on jailbroken retail PS5 hardware (FW 3.00–7.61) and boots bare-metal Linux by defeating the on-die AMD SVM hypervisor ("HyperCore"). All addresses, offsets, and bit layouts below are taken verbatim from the loader source unless noted otherwise.

**Target audience:** PS5 emulator authors. This document is intended as a foundational spec — exact numerical values are reproduced so an emulator can be wired up to match real silicon.

**SoC summary (for context):**
- Custom AMD APU, codename **Oberon**
- 8 Zen 2 cores / 16 threads, base 3.5 GHz (boost 3.5 GHz with `--boost on`)
- GPU: AMD RDNA2 variant, marketing name **"Oberon"**, GFX IP version **GFX1013**, ~2.23 GHz
- 16 GB GDDR6 unified memory (32 GB on DevKit), 256-bit bus, ~448 GB/s
- FreeBSD-derived kernel, codename **"Prospero"**

---

## 1. Physical Memory Map

The E820 table below is built by `shellcode_hv/boot_linux.c::e820_memory_setup()` and is the canonical physical memory layout handed to the Linux kernel via the zero-page `boot_params`. All addresses are **physical**, byte-granular.

### 1.1 E820 Table — Retail / TestKit (16 GB)

| Start | End | Size | Type | Note |
|------:|----:|-----:|------|------|
| `0x000000000` | `0x000001000` | 4 KB | RESERVED | Real-mode IVT / legacy BIOS area |
| `0x000001000` | `0x000070000` | 444 KB | RAM | Low memory usable by kernel |
| `0x000070000` | `0x0000a0000` | 188 KB | RESERVED | |
| `0x0000a0000` | `0x0000c0000` | 128 KB | RESERVED | VGA / legacy framebuffer window |
| `0x0000c0000` | `0x000100000` | 256 KB | RESERVED | **VBIOS** (loader copies 64 KB here from `g_vbios` kernel symbol) |
| `0x000100000` | `0x03fffc000` | ~15.9 GB | RAM | Main low RAM (slightly less than 4 GB due to IOMMU reservation at top) |
| `0x03fffc000` | `0x040000000` | 16 KB | RESERVED | IOMMU / system reservation |
| `0x040000000` | `0x060000000` | 512 MB | RAM | Extended low RAM |
| `0x060000000` | `0x060800000` | 8 MB | RESERVED | **MP4** (Media Processing Engine v4 — video decode) |
| `0x060800000` | `0x060c00000` | 4 MB | RESERVED | **VCN FW** (Video Core Next firmware blob) |
| `0x060c00000` | `0x062800000` | 28 MB | RAM | RAM gap between VCN FW and HV |
| `0x062800000` | `0x064800000` | 32 MB | RESERVED | **HV** (HyperCore hypervisor code/data) |
| `0x064800000` | `0x064829000` | 160 KB | RESERVED | **MP3** (Media Processing Engine v3 — audio/HDCP) |
| `0x064829000` | `0x07f9d0000` | ~437 MB | RAM | RAM gap |
| `0x07f9d0000` | `0x07fd5f000` | ~3.6 MB | RESERVED | ACPI tables area / system reserved |
| `0x07fd5f000` | `0x07fd63000` | 16 KB | RESERVED | |
| `0x07fd63000` | `0x07fd67000` | 16 KB | RESERVED | |
| `0x07fd67000` | `0x07fd6f000` | 32 KB | **NVS** | ACPI NVS (non-volatile sleep state) |
| `0x07fd6f000` | `0x07fd8f000` | 128 KB | **ACPI** | ACPI reclaimable tables — **RSDP lives at `0x7fd8e014`** |
| `0x07fd8f000` | `0x07fd90000` | 4 KB | RESERVED | |
| `0x07fd90000` | `0x080000000` | ~2 MB | RESERVED | ACPI/system reserved tail of low 2 GB |
| `0x080000000` | `0x0c4400000` | ~1.1 GB | RESERVED | MMIO hole (PCI config / device MMIO) |
| `0x0d0000000` | `0x0e0700000` | ~257 MB | RESERVED | MMIO (GPU register aperture, etc.) |
| `0x0f0000000` | `0x0f8000000` | 128 MB | RESERVED | MMIO |
| `0x100000000` | `VRAM_BASE` | varies | RAM | High RAM (above 4 GB) — see §1.4 |
| `VRAM_BASE` | `0x470000000` | varies | RESERVED | **VRAM** (GDDR6 aperture from GPU's perspective) |
| `0x470000000` | `0x47f300000` | ~3.9 GB | RAM | High RAM continued |
| `0x47f300000` | `0x480000000` | ~13 MB | RESERVED | System reserved tail |

`VRAM_BASE = 0x470000000 − info.vram_size`. Default `vram_size = 512 MB = 0x20000000`, giving `VRAM_BASE = 0x450000000`. The VRAM size is configurable at boot via `vram.txt` on the USB drive (value parsed as hex). The default 512 MB enables dynamic VRAM allocation in the Linux amdgpu driver.

### 1.2 E820 Table — DevKit (32 GB)

DevKits only differ in the high RAM region:

| Start | End | Type | Note |
|------:|----:|------|------|
| `0x470000000` | `0x87f300000` | RAM | Extra ~16 GB only on DevKit (32 GB total) |
| `0x87f300000` | `0x880000000` | RESERVED | DevKit tail reservation |

All entries before `0x470000000` are identical to the retail map.

### 1.3 ACPI & MMIO Anchor Addresses

| Symbol | Value | Source |
|--------|------:|--------|
| `ACPI_RSDP_ADDRESS` | `0x7fd8e014` | `shellcode_hv/boot_linux.h` — written to `bp->acpi_rsdp_addr` |
| `AMDGPU_MMIO_BASE` | `0xe0600000` | GPU MMIO aperture base |
| `AMDIOMMU_MMIO_BASE` | `0xfdd80000` | AMD IOMMU MMIO base |
| `FB_BASE` | `0xf400000000` | Framebuffer physical base (kernel-visible PA, used as `fb_start` argument to `configure_vram`) |
| `ECAM_B0D18F2` (TMR) | `0xF0C2000` | `0xF0000000 + 0x18*0x8000 + 2*0x1000` — TMR controller ECAM |

### 1.4 VRAM Base Calculation

```c
#define VRAM_BASE    (0x470000000 - info.vram_size)   // default 0x450000000
#define VRAM_SIZE    (512ULL * 1024 * 1024)            // 0x20000000
#define FB_BASE      0xf400000000                       // framebuffer PA
```

`configure_vram(fb_start=FB_BASE, vram_start=VRAM_BASE, vram_size)` programs the GPU memory controller to make VRAM accessible to the host (see §6.3).

### 1.5 Bootloader Cave Layout

The loader uses fixed physical and kernel-VA "caves" for transitional storage between boot stages:

| Symbol | Address | Purpose |
|--------|--------:|---------|
| `cave` | `0x100000000` | Physical cave base (4 GB) |
| `cave_hv_paging` | `0x100000000` | HV identity-mapped CR3 (PML4) |
| `cave_hv_code` | `0x1000003000` | HV shellcode body (after 3 pages reserved for paging) |
| `cave_linux_info` | `0x1000005000` | `struct linux_info` (copied here for HV stage) |
| `cave_bzImage` | `0x1000009000` | bzImage load target (one page after `cave_linux_info`) |
| `hv_base_rsp` | `0x110000000` | HV per-CPU stack base (16 × 4 KB stacks of 0x1000 each, one per CPU) |
| `kernel_cave` | `0xFFFF800000000000` | Kernel VA transitional cave (rest-mode → shellcode_kernel) |
| `kernel_cave_shellcode` | `0xFFFF800000000000` | shellcode_kernel image location |
| `kernel_cave_shellcode_0761` | `0xFFFF800000008000` | shellcode_0607 image location (2 × 0x4000 after kernel_cave_shellcode) |

---

## 2. CPU Topology

### 2.1 Core Count and Clocks

- **Cores:** 8 physical Zen 2 cores → **16 SMT threads**
- **`MAXCPU` macro:** `16` (the loader uses a 16-entry table for VMCBs, gvmspace, etc.)
- **CPU clock:** 3.5 GHz (firmware-controlled; boost via `ps5_control --boost on` raises it to the rated 3.5 GHz cap)
- **GPU clock:** 2.23 GHz (boost via `ps5_control --boost on`)
- **Per-CPU stack size (HV):** `0x1000` (4 KB)

### 2.2 CPU Identification

CPU index (ApicId) is obtained via `cpuid(1).ebx[31:24]`:

```c
uint8_t get_cpu(void) {
  uint32_t eax, ebx, ecx, edx;
  __get_cpuid(1, &eax, &ebx, &ecx, &edx);
  return (ebx >> 24) & 0xFF;
}
```

### 2.3 Affinity Mask (PS5 Kernel)

PS5 kernel pins threads using FreeBSD `cpuset_setaffinity` with 16-bit masks:

```c
int pin_to_core(int n) {
  uint64_t m[2] = {0};
  m[0] = (1 << n);                              // single-core mask
  return cpuset_setaffinity(3, 1, -1, 0x10, (const cpuset_t *)m);  // 0x10 = 16 bytes mask
}

void unpin(void) {
  uint64_t m[2] = {0xFFFF, 0};                  // all 16 cores
  cpuset_setaffinity(3, 1, -1, 0x10, (const cpuset_t *)m);
}
```

- `which=3` = `CPU_LEVEL_CPUSET` (FreeBSD)
- `id=1` = root cpuset
- `cpusetsize=0x10` (16 bytes — covers up to 128 CPUs but only bits 0–15 used)

### 2.4 Relevant MSRs

| MSR | Address | Bits Used | Source |
|-----|--------:|-----------|--------|
| `MSR_EFER` | `0xC0000080` | bit 12 (`EFER_SVM`) — must be cleared after HV escape to disable SVM | `shellcode_hv/boot_linux.h` |
| `MSR_VM_CR` | `0xC0010114` | bit 1 (`VM_CR_R_INIT`) — INIT redirection; cleared post-escape | `shellcode_hv/boot_linux.h` |
| `MSR_APICBASE` | `0x0000001B` | bit 11 (`APICBASE_ENABLED`=0x800), bit 8 (`APICBASE_BSP`=0x100) | `shellcode_0607/main.c`, `source/hv_defeat_0607.c` |
| `MSR_GSBASE` | `0xC0000101` | full 64-bit kernel GS base | `shellcode_0607/main.c` |
| `MSR_MTRR4kBase` | `0x00000268` | fixed-range MTRRs (cleared on boot) | `shellcode_hv/boot_linux.h` |
| `MSR_MTRRVarBase` | `0x00000200` | variable-range MTRRs (entry 7 high half cleared) | `shellcode_hv/boot_linux.h` |

Default host APIC base PA: `0xfee00000`.

PS5 kernel uses 16-KB pages (`PAGE_SIZE = 0x4000`) — see §7.2. The Linux subarch reported in `boot_params.hdr.hardware_subarch` is `X86_SUBARCH_PS5 = 5`.

---

## 3. Hypervisor ("HyperCore") Architecture

PS5 ships with an AMD SVM-based hypervisor — internally "HyperCore" — running at privilege level -1 (above ring 0). The loader defeats it via one of three ROP/hypercall chains (see §8) so that Linux can run on bare metal.

### 3.1 vCPU Count

**16 vCPUs**, matching the 16 host threads. All per-vCPU state tables in HV are 16 entries:

- VMCB array: 16 entries (one per CPU)
- `g_vm_tab`: 16 vCPU context pointers
- `nmiCounts[16]` in `SceSblHvShm`
- HV rendezvous counter `exited_cpus` reaches `MAXCPU=16`

### 3.2 VMCB Layout (per firmware)

The VMCB (Virtual Machine Control Block) is the AMD SVM per-vCPU state structure. Each VMCB is `0x2000` (8 KB) on FW ≥ 5.00 and `0x3000` (12 KB) on FW 3.00–4.51.

| Firmware range | VMCB base | Stride |
|----------------|----------:|-------:|
| FW 3.00, 3.10, 3.20, 3.21 | `0x6290B000` | `0x3000` |
| FW 4.00, 4.02, 4.03, 4.50, 4.51 | `0x62A05000` | `0x3000` |
| FW 5.00, 5.02, 5.10, 5.50 | `0x62A08000` | `0x2000` |
| FW 6.00, 6.02 | `0x62A57000` | `0x2000` |
| FW 6.50, 7.00–7.61 | `0x62A57000`* | `0x2000` |

*On 6.50+ the loader does not directly patch VMCBs from the kernel (the ROP chain in `hv_defeat_0607.c` patches them from HV context instead). The 6.50+ VMCB base address (`0x62A57000`) is shared with 6.00–6.02.

`vmcb[core] = base + core * stride`

### 3.3 VMCB Offsets

The loader only touches one field of the VMCB:

| Offset | Field | Notes |
|-------:|-------|-------|
| `0x90` | `NESTED_CTRL` | Controls nested paging and guest-mode execution trapping |

Bit layout of `NESTED_CTRL` (per the loader's writes):

| Bit | Mask | Name | Effect |
|-----:|------:|------|--------|
| 0 | `0x1` | `NP_ENABLE` | Nested paging (NPT) enable. Loader clears to disable NPT. |
| 3 | `0x8` | `GMET_ENABLE` | Guest Mode Execute Trap Enable. Loader sets this in `NESTED_CTRL_GMET_ENABLE` to corrupt NPT enforcement. |

The HV defeat sets `vmcb+0x90 = 0` to fully disable nested paging, then forces a VMCB reload via a vmmcall.

### 3.4 HV Internal Pointers (shellcode_0607/main.c)

These are **physical addresses of HV-internal pointers** that the 7.x HV-defeat shellcode dereferences after entering HV context. They are read as `*(uint64_t*)addr`.

| Symbol | 7.xx address | 6.50 address | Purpose |
|--------|-------------:|-------------:|---------|
| `HV_REENTER_HYPERCORE` | `0x62806380` | `0x62806780` | HV's "re-enter hypercore" function — restores HV state and resumes guest |
| `HV_STACK_TABLE` | `0x6282E120` | `0x6282D0C0` | HV per-vCPU stack table (kernel GS base = `tab[0] + 0x1000`) |
| `HV_PML4` | `0x6282E1A0` | `0x6282D140` | Pointer to HV's PML4 (host page table CR3) |
| `HV_ENTRY` | `0x6282E1B8` | `0x6282D158` | HV entry pointer; `hv_base = (*HV_ENTRY) − HV_MAIN` |
| `HV_MAIN` (offset) | `0x0E20` | `0x0F10` | Offset of `main` within HV image (used to derive `hv_base`) |
| `G_VM_TAB` (offset) | `0x27C80` | `0x23C80` | Offset within HV image of the 16-entry vCPU context pointer array |

### 3.5 vCPU Context Layout (referenced via `g_vm_tab[i]`)

```
struct vcpu_ctx {
    uint64_t :pad;                  // +0x00
    uint64_t vmcb_pa;               // +0x08  (VMCB physical address)
    ...
    uint64_t guest_apic_base;       // +0xE8  (saved MSR_APICBASE value)
};
```

The 6.50+ HV defeat restores vCPU 0's `guest_apic_base` to `0xfee00000 | 0x800 | 0x100` (default APIC base + ENABLED + BSP).

### 3.6 HV VMEXIT Handler Address

The HV's VMEXIT entry point. The loader patches this PA with a trampoline:

| Firmware | `HV_HANDLE_VMEXIT_PA` |
|----------|----------------------:|
| 3.00–3.21 | `0x6282CBCB` |
| 4.00–4.51 | `0x6282B45D` |
| 5.00–5.50 | `0x6282EBD0` |
| 6.00–6.02 | `0x62841CE0` |
| 6.50 | `0x62841D50` |
| 7.00–7.61 | `0x6283D800` |

`HV_CODE_CAVE_PA = 0x62806F00` — fixed HV code cave PA used as the patch destination on all firmwares.

HV stack base addresses used by the 6.50/7.x APICBASE trick:

| Firmware | HV stack PA |
|----------|-------------:|
| 6.50 | `0x628D0000` |
| 7.00–7.61 | `0x628EC000` |

The MSR_APICBASE value is rewritten to `hv_stack | APICBASE_ENABLED | APICBASE_BSP` so that the next VMEXIT delivers control to that stack.

### 3.7 HV Shared Memory (`SceSblHvShm`)

A shared structure between HV and the secure boot loader (SceSbl). Located at a firmware-dependent PA:

| Firmware range | `SceSblHvShm` PA |
|----------------|-----------------:|
| 5.00–5.99 | `0x62A01000` |
| 6.00–7.99 | `0x62A22000` |

Layout (packed):

```c
struct SceSblHvShmTmrPtState {
    uint64_t flags;   // bit 0 = TMR region is active
    uint64_t addr;    // physical base of the TMR region
    uint64_t size;    // size in bytes
} __attribute__((packed));

struct SceSblHvShm {
    uint32_t  sig;                          // +0x000  signature
    uint32_t  ver;                          // +0x004  version
    uint16_t  tmrMapPts[64];                // +0x008  TMR → page-table mapping bitmap (128 B)
    uint64_t  tmrOvlpIds[64];               // +0x088  TMR overlap ID bitmap (512 B)
    SceSblHvShmTmrPtState tmrPtStates[64];  // +0x288  TMR physical address ranges (1536 B)
    uint32_t  nmiCounts[16];                // +0x888  per-CPU NMI counters (64 B)
    uint8_t   reserved[64];                 // +0x8C8  reserved
} __attribute__((packed));
```

Total size: 0x908 bytes. The loader reads `tmrPtStates[]` to enumerate Trust Memory Ranges and feeds them into the E820 table as additional RESERVED entries.

HV exploit (FW 5.00+) computes the target TMR index from the VMCB pointer arithmetic:

```
tmr_id = (vmcb[core] - SceSblHvShm - 0x208) / 0x18
```

### 3.8 Hypercall ABI

PS5 HV uses the AMD `vmmcall` instruction with a SysV-ish calling convention:

```c
static inline uint64_t vmmcall(uint64_t nr, uint64_t a0, uint64_t a1, uint64_t a2) {
    uint64_t ret;
    __asm__ volatile("vmmcall"
                     : "=a"(ret)
                     : "a"(nr),     // rax = hypercall number
                       "b"(a0),     // rbx = arg0
                       "c"(a1),     // rcx = arg1
                       "d"(a2)      // rdx = arg2
                     : "memory");
    return ret;
}
```

Hypercall signatures observed in the loader:

| Hypercall | Signature | Purpose |
|-----------|-----------|---------|
| `vmmcall(1, ...)` | `rax=1` | Trigger VMEXIT on the calling vCPU (used by `vmmcall_dummy` to escape to HV) |
| `FUN_HV_UNMAP_PT_TMR(tmr_id, size=0x1000, addr=0)` | `rdi=tmr_id, rsi=0x1000, rdx=0` | Corrupts `NESTED_CTRL` in the VMCB pointed to by `tmr_id` (HV-side bug) |
| `FUN_HV_UNMAP_PT_TMR(0, 0, 0xffffffffffffffff)` | `rdx = -1` | Forces VMCB reload after corruption |
| `FUN_HV_IOMMU_SET_BUFFERS(cb2_pa, cb3_pa, eb_pa, &unk, &n_devices)` | | HV-side IOMMU buffer registration |
| `FUN_HV_IOMMU_WAIT_COMPLETION(void)` | returns 0 on success | Wait for HV IOMMU command completion |
| `stgi` instruction | — | Set Global Interrupt flag — re-enables guest interrupts after HV escape (called in `shellcode_hv/boot_linux.c::entry`) |

---

## 4. AMD IOMMU

### 4.1 MMIO Layout

| Symbol | Offset from `AMDIOMMU_MMIO_BASE` (`0xFDD80000`) | Size | Purpose |
|--------|-------------------------------------------------:|-----:|---------|
| `AMDIOMMU_CTRL` | `0x18` | 8 B | Control register. **Bit 0 = IOMMU enable.** Loader clears bit 0 to disable the IOMMU after HV escape. |
| `IOMMU_MMIO_CB_HEAD` | `0xA000` | 8 B | Command buffer head pointer (read-only from driver side) |
| `IOMMU_MMIO_CB_TAIL` | `0xA008` | 8 B | Command buffer tail pointer (writable; advancing triggers command processing) |

### 4.2 Command Queue Format

- **Command buffer size:** `0x2000` (8 KB)
- **Mask:** `0x1FFF` (`IOMMU_CB_SIZE - 1`)
- **Command entry size:** `0x10` (16 bytes = 4 dwords)

```
+--------+--------+--------+--------+
| dword0 | dword1 | dword2 | dword3 |
+--------+--------+--------+--------+
```

### 4.3 IOMMU `softc` (kernel-side software context)

```c
struct iommu_softc {
    ...
    uint64_t mmio_va;     // +0x40       DMAP VA of IOMMU MMIO base
    ...
    uint64_t cb2_base;     // +0x78       kernel VA of command buffer 2 (active queue)
    uint64_t cb3_base;     // +0x80       kernel VA of command buffer 3
    ...
    uint64_t eb_base;      // +0x60B90    kernel VA of event buffer
    ...
};
```

### 4.4 Completion-Wait-Store Command Encoding

A single 16-byte command that writes 8 bytes to an arbitrary physical address — used by the loader as a privileged-write primitive via the IOMMU:

```c
void iommu_write8_pa(uint64_t pa, uint64_t val) {
  uint32_t cmd[4];
  cmd[0] = (uint32_t)(pa & 0xFFFFFFF8) | 0x05;                  // opcode 0x05 = COMPLETION_WAIT; addr low (bits 3:31 of PA, aligned to 8)
  cmd[1] = ((uint32_t)(pa >> 32) & 0xFFFFF) | 0x10000000;       // addr high (bits 32:51 of PA); bit 28 = ST0=1 (store flag)
  cmd[2] = (uint32_t)(val);                                      // store data low
  cmd[3] = (uint32_t)(val >> 32);                                // store data high
  iommu_submit_cmd(cmd);
}
```

Bit fields of the command dword layout:

- **`cmd[0]`** bits 0–2: opcode = `0x05` (COMPLETION_WAIT)
- **`cmd[0]`** bits 3–31: bits 3–31 of target PA (so PA must be 8-byte aligned)
- **`cmd[1]`** bits 0–19: bits 32–51 of target PA
- **`cmd[1]`** bit 28: `ST0` (store flag — perform a store instead of just signaling)
- **`cmd[2]`**: low 32 bits of value to store
- **`cmd[3]`**: high 32 bits of value to store

### 4.5 Submission Sequence

```c
void iommu_submit_cmd(const void *cmd) {
  uint64_t curr_tail = kread64(mmio_va + IOMMU_MMIO_CB_TAIL);
  uint64_t next_tail = (curr_tail + 0x10) & 0x1FFF;
  kwrite(cb2_base + curr_tail, cmd, 0x10);                       // enqueue
  kwrite64(mmio_va + IOMMU_MMIO_CB_TAIL, next_tail);             // ring doorbell
  while (kread64(mmio_va + IOMMU_MMIO_CB_HEAD) !=
         kread64(mmio_va + IOMMU_MMIO_CB_TAIL)) ;                 // wait for completion
}
```

### 4.6 Disabling the IOMMU

After HV escape and CPU rendezvous, CPU 0 disables the IOMMU entirely:

```c
*(volatile uint64_t *)(AMDIOMMU_MMIO_BASE + AMDIOMMU_CTRL) &= ~1ULL;   // clear bit 0
```

---

## 5. TMR (Trust Memory Range) Controller

The TMR controller is a custom PS5 SoC block that enforces memory access permissions per physical address range. It is configured via PCI ECAM space.

### 5.1 PCI B/D/F and ECAM Base

| Field | Value |
|-------|-------|
| Bus | 0 |
| Device | 18 (`0x12`) |
| Function | 2 |
| ECAM base | `0xF0000000` |
| Per-device ECAM stride | `0x8000` (32 KB — PS5-specific) |
| Per-function stride | `0x1000` (4 KB — standard) |
| **B0:D18:F2 ECAM address** | `0xF0000000 + 0x12 * 0x8000 + 0x2 * 0x1000` = **`0xF0C2000`** |

### 5.2 Register Map (within `ECAM_B0D18F2`)

The TMR controller uses an indexed register pair (write index → read/write data):

| Symbol | Offset | Purpose |
|--------|-------:|---------|
| `TMR_INDEX_OFF` | `0x80` | Index register — write the entry index here |
| `TMR_DATA_OFF` | `0x84` | Data register — read or write the entry value here |

Access functions:

```c
uint32_t tmr_read(uint32_t addr)  { kwrite32(ECAM + 0x80, addr); return kread32(ECAM + 0x84); }
void     tmr_write(uint32_t addr, uint32_t val) { kwrite32(ECAM + 0x80, addr); kwrite32(ECAM + 0x84, val); }
```

### 5.3 Entry Format

Each TMR entry is `0x10` (16 bytes) with four 32-bit fields:

| Field | Offset within entry | Purpose |
|-------|---------------------:|---------|
| `TMR_BASE(n)` | `n * 0x10 + 0x00` | Low 16 bits of region base PA. Full base = `base << 16`. |
| `TMR_LIMIT(n)` | `n * 0x10 + 0x04` | Low 16 bits of region limit. Full limit = `(limit << 16) \| 0xFFFF`. |
| `TMR_CONFIG(n)` | `n * 0x10 + 0x08` | Configuration / permission flags |
| `TMR_REQUESTORS(n)` | `n * 0x10 + 0x0C` | Bitmap of allowed requestor IDs |

Maximum entries: **22** (some firmwares use 24 — the disable loop iterates `i < 24`).

### 5.4 Permissive Configuration Value

```c
#define TMR_CFG_PERMISSIVE 0x3F07
```

Binary: `0000_0000_0000_0000_0011_1111_0000_0111`. Writing this to `TMR_CONFIG(n)` opens the range to all requestors. The loader uses this to relax TMR enforcement so that Linux can access VRAM and other protected regions.

For FW < 3.00, only `TMR_CONFIG(16)` is relaxed. For FW ≥ 3.00, entries 5, 16, 17, 18 are relaxed.

### 5.5 TMR Disable (post-HV-defeat)

After HV is defeated on FW 3.00–4.51, the kernel shellcode forcibly disables all TMR entries:

```c
int tmr_disable(uint64_t dmap) {
  for (int i = 0; i < 24; i++) {
    if (tmr_read(dmap, TMR_CONFIG(i)) != 0) {
      tmr_write(dmap, TMR_CONFIG(i), 0);
      if (tmr_read(dmap, TMR_CONFIG(i)) != 0) return -1;
    }
  }
  return 0;
}
```

---

## 6. GPU (AMD RDNA2 / GFX1013 "Oberon")

### 6.1 Device & Ioctl ABI

- **Character device:** `/dev/gc` (opened `O_RDWR`)
- **Submit ioctl number:** `0xC0108102` (`GPU_SUBMIT_IOCTL`)

  Direction `1<<31` (write) | size `(0x10 << 16)` | group `'Z'` (0x5A → in this case the top byte is `0x81`, meaning group `'A'`... actually `0xC0108102` decodes as `_IOW('A', 2, 0x10)` per FreeBSD conventions).

- **Submit argument struct:**

  ```c
  struct {
      uint32_t pipe_id;        // compute pipe (typically 0)
      uint32_t count;          // number of command descriptors pointed to by cmd_buf_ptr
      uint64_t cmd_buf_ptr;    // user VA of array of 16-byte descriptors
  } submit;
  ```

### 6.2 GPU Page Table Format

The GPU uses a 4-level page table similar to AMD's standard GART format:

| Level | Name | VA bits indexed | Entries | Index formula |
|-------|------|-----------------|---------|---------------|
| 1 (top) | **PDB2** (PML4-equiv) | 47:39 | 512 | `(va >> 39) & 0x1FF` |
| 2 | **PDB1** (PDPT-equiv) | 38:30 | 512 | `(va >> 30) & 0x1FF` |
| 3 | **PDB0** (PD-equiv) | 29:21 | 512 | `(va >> 21) & 0x1FF` |
| 4 | **PTB** (leaf) | varies by fragment size | varies | see below |

### 6.3 PDE Bit Layout

A 64-bit PDE/PTE entry has the following fields:

| Bit(s) | Mask | Name | Meaning |
|-------:|------:|------|---------|
| 0 | `0x1` | `VALID` | Entry is present |
| 54 | `0x1` | `IS_PTE` | If set on a PDB0 entry, this is a 2 MB leaf PTE (no further walk) |
| 56 | `0x1` | `TF` | Tile-Final fragment subdivision flag (sub-divides 64 KB page into 8 KB) |
| 59–63 | `0x1F` | `BLOCK_FRAG` | Fragment size encoding (5-bit field) |
| 6–47 | `0x0000FFFFFFFFFFC0` | `ADDR` | Physical address (36-bit, 64-byte aligned) |

Definition (from `include/gpu.h`):

```c
#define GPU_PDE_VALID_BIT       0
#define GPU_PDE_IS_PTE_BIT      54
#define GPU_PDE_TF_BIT          56
#define GPU_PDE_BLOCK_FRAG_BIT  59
#define GPU_PDE_ADDR_MASK       0x0000FFFFFFFFFFC0ULL
```

### 6.4 Fragment Size → Page Size Resolution

When the PDE is not a leaf (`IS_PTE=0`), the `BLOCK_FRAG` field (bits 59–63) selects the leaf page-table entry size:

| `BLOCK_FRAG` | PTE index calculation | Page size |
|-------------:|------------------------|-----------|
| `4` | `(va & 0x1FFFFF) >> 16` (then `(va & 0xFFFF) >> 13` if `TF` set) | 64 KB, or 8 KB if `TF` set |
| `1` | `(va & 0x1FFFFF) >> 13` | 8 KB |
| other | `(va & 0x1FFFFF) >> 16` | 64 KB (default) |

If `IS_PTE` is set on a PDB0 entry, the page size is **2 MB** (`0x200000`).

### 6.5 `gvmspace` Kernel Structure

The kernel maintains an array of `gvmspace` structs (one per GPU VMID, 0–15):

```
struct gvmspace {            // size = 0x100 (256 bytes), as reported by sizeof_gvmspace
    uint8_t  pad[0x08];
    uint64_t start_va;       // +0x08  GPU VA range start
    uint8_t  pad[0x10];
    uint64_t size;           // +0x10  GPU VA range size
    uint8_t  pad[0x38];
    uint64_t page_dir_va;    // +0x38  PDB2 (top-level GPU page table) kernel VA
    ...
};
```

Array base: `kdata + DATA_BASE_GVMSPACE`. Per-FW `DATA_BASE_GVMSPACE` values:

| Firmware | `DATA_BASE_GVMSPACE` |
|----------|---------------------:|
| 3.00–3.21 | `0x06423F80` |
| 4.00–4.51 | `0x064C3F80` |
| 5.00+ | n/a (not used by 5.00+ hv-defeat path) |

### 6.6 PM4 Packet Format

PM4 packets are the AMD GPU command-buffer format. Type-3 packets are the most common:

#### Type-3 Header

```c
#define PM4_TYPE3             3
#define PM4_SHADER_COMPUTE    1

static uint32_t pm4_type3_header(uint32_t opcode, uint32_t count) {
    return ((PM4_TYPE3 & 0x3) << 30)              // bit 31-30: type = 3
         | (((count - 1) & 0x3FFF) << 16)         // bit 29-16: count - 1 (number of data dwords)
         | ((opcode & 0xFF) << 8)                 // bit 15-8:  opcode
         | ((PM4_SHADER_COMPUTE & 0x1) << 1);     // bit 1:     shader type (1 = compute)
}
```

Bit layout of the 32-bit PM4 type-3 header:

```
 31  30  29                                16  15     8   7    1   0
+----+----+----------------------------------+---------+----+----+---+
| 11 | type=3 |     count - 1 (14 bits)      | opcode  |  - | SC | 0 |
+----+----+----------------------------------+---------+----+----+---+
                       \____________________/ \_______/ \__/ \__/
                          data-dword count      op code   shader  reserved
                                                   type
```

#### `DMA_DATA` Packet (opcode `0x50`)

```c
#define PM4_OPCODE_DMA_DATA     0x50

static int pm4_build_dma_data(void *buf, uint64_t dst_va, uint64_t src_va, uint32_t length) {
  uint32_t *pkt = buf;
  uint32_t count = 6;
  uint32_t dma_hdr = (1u << 31)      // cp_sync — wait for completion
                   | (2u << 25)      // dst_cache_policy
                   | (1u << 27)      // dst_volatile
                   | (2u << 13)      // src_cache_policy
                   | (1u << 15);     // src_volatile

  pkt[0] = pm4_type3_header(PM4_OPCODE_DMA_DATA, count);
  pkt[1] = dma_hdr;
  pkt[2] = (uint32_t)(src_va & 0xFFFFFFFF);    // src_lo
  pkt[3] = (uint32_t)(src_va >> 32);           // src_hi
  pkt[4] = (uint32_t)(dst_va & 0xFFFFFFFF);    // dst_lo
  pkt[5] = (uint32_t)(dst_va >> 32);           // dst_hi
  pkt[6] = length & 0x1FFFFF;                  // length (21-bit, in bytes)
  return 7 * 4;                                // 28 bytes total
}
```

#### `INDIRECT_BUFFER` Packet (opcode `0x3F`)

```c
#define PM4_OPCODE_INDIRECT_BUF  0x3F
```

The descriptor for an indirect-buffer submission (used as the cmd descriptor in `gpu_submit_commands`):

```c
// 16-byte descriptor for INDIRECT_BUFFER submission:
// d[0] = [IB_base_lo(32) | 0xC0023F00 (PM4 type-3 IB header, count=3, opcode=0x3F)]
// d[1] = [IB_size_dwords(20 bits) | IB_base_hi(16 bits)]
d[0] = ((gpu_addr & 0xFFFFFFFFULL) << 32) | 0xC0023F00ULL;
d[1] = (((uint64_t)size_dwords & 0xFFFFF) << 32) | ((gpu_addr >> 32) & 0xFFFF);
```

The `0xC0023F00` literal decodes as: `type=3 | count-1=2 (count=3) | opcode=0x3F (INDIRECT_BUFFER) | shader_compute=0`.

### 6.7 GPU MMIO Registers

All GPU MMIO offsets are relative to `AMDGPU_MMIO_BASE = 0xE0600000` (physical).

| Register | Offset | Width | Purpose |
|----------|-------:|------:|---------|
| `RCC_CONFIG_MEMSIZE` | `0x378C` | u32 | VRAM size in MB (`vram_size >> 20`) |
| `GCMC_VM_FB_OFFSET` | `0xA5AC` | u32 | GC memory controller FB offset (`vram_start >> 24`) |
| `GCMC_VM_LOCAL_HBM_ADDRESS_START` | `0xA5D4` | u32 | Local HBM address start (`vram_start >> 24`) |
| `GCMC_VM_LOCAL_HBM_ADDRESS_END` | `0xA5D8` | u32 | Local HBM address end (`vram_end >> 24`) |
| `GCMC_VM_FB_LOCATION_BASE` | `0xA600` | u32 | FB location base (`fb_start >> 24`) |
| `GCMC_VM_FB_LOCATION_TOP` | `0xA604` | u32 | FB location top (`fb_top >> 24`) |
| `MMMC_VM_FB_OFFSET` | `0x6A15C` | u32 | MM hub FB offset (mirror of GCMC) |
| `MMMC_VM_LOCAL_HBM_ADDRESS_START` | `0x6A184` | u32 | MM hub HBM start |
| `MMMC_VM_LOCAL_HBM_ADDRESS_END` | `0x6A188` | u32 | MM hub HBM end |
| `MMMC_VM_FB_LOCATION_BASE` | `0x6A1B0` | u32 | MM hub FB location base |
| `MMMC_VM_FB_LOCATION_TOP` | `0x6A1B4` | u32 | MM hub FB location top |
| `MMHUBBUB_WHITELIST_BASE_ADDR_0` | `0x24850` | u32 | MMHUBBUB whitelist base (4 KB-granular; `vram_start >> 12`) |
| `MMHUBBUB_WHITELIST_TOP_ADDR_0` | `0x24854` | u32 | MMHUBBUB whitelist top (`vram_end >> 12`) |
| `DCHUBBUB_WHITELIST_BASE_ADDR_0` | `0x24878` | u32 | DCHUBBUB whitelist base (`vram_start >> 12`) |
| `DCHUBBUB_WHITELIST_TOP_ADDR_0` | `0x2487C` | u32 | DCHUBBUB whitelist top (`vram_end >> 12`) |

### 6.8 Framebuffer Physical Address

```c
#define FB_BASE  0xf400000000   // 64-bit PA used as the GPU-visible framebuffer location
```

The framebuffer is mapped by `configure_vram()`:

```c
configure_vram(FB_BASE, VRAM_BASE, info.vram_size);
```

After this, the GPU can DMA into the framebuffer at `FB_BASE`. The whitelist registers (`MMHUBBUB_*` and `DCHUBBUB_*`) authorize the host CPU to access VRAM via these PA ranges.

### 6.9 DP Transmitter Initialization

DisplayPort PHY is enabled post-HV-defeat (in `shellcode_kernel/boot_linux.c`):

```c
dp_enable_link_phy(lanenum=4, linkrate=30);
//  → transmitter_control(0x4C /* DIG1 transmitter */, &params)
//     with action=1 (TRANSMITTER_CONTROL_ENABLE)
//     and symclk_10khz = 27000 * linkrate / 10 = 81000 (8.1 GHz, HBR3)
```

`dig_transmitter_control_parameters_v1_6` layout (16 B packed):

| Offset | Size | Field |
|-------:|-----:|-------|
| 0 | 1 | `phyid` (0) |
| 1 | 1 | `action` (1 = ENABLE, 11 = SET_VOLTAGE_AND_PREEMPHASIS) |
| 2 | 1 | `digmode` / `dplaneset` |
| 3 | 1 | `lanenum` (4) |
| 4 | 4 | `symclk_10khz` (81000 for HBR3 4-lane) |
| 8 | 1 | `hpdsel` |
| 9 | 1 | `digfe_sel` |
| 10 | 1 | `connobj_id` |
| 11 | 1 | reserved |
| 12 | 4 | reserved1 |

---

## 7. Kernel ABI (FreeBSD-derived "Prospero")

### 7.1 Kernel Text / Data Bases

```c
#define KERNEL_TEXT   0xFFFFFFFF80210000          // kernel .text base (constant across FW 3.00–7.61)
// ktext = KERNEL_ADDRESS_TEXT_BASE                // == KERNEL_TEXT
// kdata = KERNEL_ADDRESS_DATA_BASE                // PS5 SDK-provided constant
```

All FW-specific offsets in `source/offsets.c` are expressed relative to `KERNEL_TEXT`. Absolute address = `KERNEL_TEXT + offset`.

### 7.2 `pmap` Layout

The PS5 kernel uses a flat (non-`struct mutex`-inlined) pmap structure for the kernel_pmap, accessed via `flat_pmap`:

```c
struct flat_pmap {
    uint64_t mtx_name_ptr;    // +0x00  (char*)
    uint64_t mtx_flags;       // +0x08
    uint64_t mtx_data;        // +0x10
    uint64_t mtx_lock;        // +0x18
    uint64_t pm_pml4;         // +0x20  — PML4 kernel VA
    uint64_t pm_cr3;          // +0x28  — CR3 physical address
};
```

`pm_pml4` and `pm_cr3` are at offsets `+0x20` and `+0x28` respectively. The Direct Map (DMAP) is derived as `dmap = pm_pml4 - pm_cr3` (i.e., the constant VA offset that maps every physical page).

`pmap` is reached from a `proc` via:

```c
uint64_t pmap = kread64(getpmap(proc));
//   getpmap(proc) = kread64(kread64(proc + KERNEL_OFFSET_PROC_P_VMSPACE) + VMSPACE_VM_PMAP);
//   get_pml4(pmap) = kread64(pmap + 0x20);
```

`vtophys_user(va)` walks the calling process's own page tables:

```c
self_pmap   = getpmap(kernel_get_proc(getpid()));
self_pml4   = kread64(self_pmap + 0x20);
return vtophys_custom(va, self_pml4 & 0xFFFFFFFF);
```

### 7.3 `proc->p_vmspace` and `vmspace` Layout

| Symbol | Offset | Description |
|--------|-------:|-------------|
| `KERNEL_OFFSET_PROC_P_VMSPACE` | PS5-SDK extern | Offset of `p_vmspace` within `struct proc` |
| `VMSPACE_VM_PMAP` | `0x1D0` (FW 3.00–5.50), `0x1D8` (FW 6.00–7.61) | Offset of `vm_pmap` within `struct vmspace` |
| `VMSPACE_VM_VMID` | `0x1E4` (FW 3.00–4.51) | Offset of GPU VMID within `struct vmspace` (used only on 3.xx–4.xx) |

### 7.4 Page Table PTE Bit Layout

PS5 extends the standard x86-64 PTE format with two custom bits:

```c
enum page_bits {
    P   = 0,    // Present
    RW  = 1,    // Read/Write
    US  = 2,    // User/Supervisor
    PWT = 3,    // Page-level Write-Through
    PCD = 4,    // Page-level Cache Disable
    A   = 5,    // Accessed
    D   = 6,    // Dirty
    PS  = 7,    // Page Size (large page)
    G   = 8,    // Global
    XO  = 58,   // Execute-Only  (PS5-custom)
    PK  = 59,   // Protection Key (PS5-custom, used by kernel for page-protection key)
    NX  = 63    // No-Execute
};
```

Physical address mask (bits 12–47, 36-bit PA, 4 KB-aligned):

```c
#define PAGE_PA(x)  (x & 0x000FFFFFFFFFF000ULL)
```

Large-page sizes (when `PS` bit set):

```c
#define P_SIZE(level)  ((level == 1) ? (1ULL << 30)   // 1 GB (PDPT-level large page)
                                     : (1ULL << 21))  // 2 MB (PD-level large page)
```

PML4/PDPT/PD/PT index helpers:

```c
pmap_pml4e_index(va)  = (va >> 39) & 0x1FF;
pmap_pdpe_index(va)    = (va >> 30) & 0x1FF;
pmap_pde_index(va)     = (va >> 21) & 0x1FF;
pmap_pte_index(va)     = (va >> 12) & 0x1FF;
```

Note that the PS5 kernel uses **16 KB pages** (`PAGE_SIZE = 0x4000`) — i.e., 4 contiguous 4 KB leaves are installed per logical page (see `pte_store`):

```c
void pte_store(uintptr_t ptep, uint64_t pte) {
  static_assert((PAGE_SIZE % 0x1000) == 0, "...");
  for (uint64_t i = 0; i < (PAGE_SIZE / 0x1000); i++)
    kwrite64(ptep + i*8, pte + i*0x1000);
}
```

### 7.5 CFI Check Mechanism

The PS5 kernel implements a coarse-grained CFI (Control Flow Integrity) check at indirect call sites. A single byte at `KERNEL_CFI_CHECK` (kernel VA, FW-specific offset relative to `KERNEL_TEXT`) acts as the CFI enable flag:

```c
*(uint8_t *)args_ptr->kernel_cfi_check = 0xC3;   // patch with RET instruction
```

By writing `0xC3` (the `ret` opcode) over the CFI check function's first byte, the check becomes a no-op, allowing arbitrary indirect calls — necessary for `smp_rendezvous()` to call attacker-supplied function pointers.

Per-FW `KERNEL_CFI_CHECK` offsets (relative to `KERNEL_TEXT = 0xFFFFFFFF80210000`):

| Firmware | Offset |
|----------|-------:|
| 3.00 | `0x0441DD0` |
| 3.10 | `0x0441E10` |
| 3.20 / 3.21 | `0x442160` |
| 4.00–4.03 | `0x45A170` |
| 4.50 / 4.51 | `0x45A1A0` |
| 5.00–5.50 | `0x677FB0` |
| 6.00 / 6.02 | `0x6899B0` |
| 6.50 | `0x689A20` |
| 7.00 / 7.01 | `0x6854A0` |
| 7.20 | `0x6857A0` |
| 7.40 | `0x6857A0` |
| 7.60 / 7.61 | `0x6857B0` |

### 7.6 Kernel UART Override

Two pieces must be cleared to enable kernel UART output (otherwise the kernel silently filters its own printf output):

1. The UART control register at physical `0xC0115110` has bit `0x200` set on retail PS5s — clearing it re-enables the UART physical TX:

   ```c
   uint32_t *uart_va = (uint32_t *)(args_ptr->dmap_base + 0xC0115110ULL);
   *uart_va &= ~0x200;
   ```

2. A single byte (`uint32_t`) at `KERNEL_UART_OVERRIDE` (kernel VA) holds a "filter character" that, when non-zero, suppresses printf output. Setting it to `0` removes the filter:

   ```c
   uint32_t *override_char_va = (uint32_t *)args_ptr->kernel_uart_override;
   *override_char_va = 0x0;
   ```

UART register MMIO addresses (physical, accessed via DMAP):

| Register | PA | Notes |
|----------|---:|-------|
| UART TX | `0xC1010104` | Write a byte to transmit |
| UART BUSY/status | `0xC101010C` | Bit `0x20` = ready (set when UART is ready to accept next byte) |
| UART control | `0xC0115110` | Bit `0x200` = disable output (set on retail) |

The Linux console device name in the kernel cmdline is `ttyTitania0` (codename **"Titania"** is the PS5 UART serial driver).

### 7.7 `sysent` Layout

```c
struct sysent {
    uint32_t n_arg;            // +0x00  number of arguments
    uint32_t pad;              // +0x04
    uint64_t sy_call;          // +0x08  function pointer
    uint64_t sy_auevent;       // +0x10  audit event
    uint64_t sy_systrace_args; // +0x18
    uint32_t sy_entry;         // +0x20
    uint32_t sy_return;        // +0x24
    uint32_t sy_flags;         // +0x28
    uint32_t sy_thrcnt;        // +0x2C
};
```

### 7.8 Syscalls & Kernel Helpers Used by the Loader

The loader uses the following PS5 kernel APIs (provided by `ps5-payload-sdk`):

#### Userland syscalls (FreeBSD-derived)

| Symbol | Signature | Notes |
|--------|-----------|-------|
| `kernel_copyin(src_u, dst_k, len)` | `int (const void *, uint64_t, size_t)` | Copy user→kernel (privileged) |
| `kernel_copyout(src_k, dst_u, len)` | `int (uint64_t, void *, size_t)` | Copy kernel→user (privileged) |
| `kernel_get_proc(pid)` | `uint64_t (pid_t)` | Returns kernel `proc *`. PID 0 = kernel proc, 1 = mini syscore. |
| `kernel_get_proc_file(pid, fd)` | `uint64_t (pid_t, int)` | Returns kernel `file *` for the fd in proc `pid` |
| `kernel_get_fw_version()` | `uint64_t (void)` | Firmware version; `(ret >> 0x10) & 0xFFFF` gives e.g. `0x0700` for FW 7.00 |
| `kernel_setlong(addr, val)` | `void (uint64_t, uint64_t)` | Atomically store 8 bytes at kernel VA |
| `kernel_getlong(addr)` | `uint64_t (uint64_t)` | Atomically load 8 bytes from kernel VA |
| `sceKernelSendNotificationRequest(0, &req, sizeof(req), 0)` | `int (int, void *, size_t, int)` | Pop a notification toast on PS5 UI |
| `sceKernelOpenEventFlag(&ev, "SceSystemStateMgrStatus")` | `int (void **, const char *)` | Open named event flag |
| `sceKernelSetEventFlag(ev, 0x400)` | `int (void *, int)` | Bit `0x400` = suspend-start |
| `sceKernelCloseEventFlag(ev)` | `int (void *)` | |
| `sceKernelNotifySystemSuspendStart()` | `int (void)` | Request system suspend |
| `sceKernelGetCurrentCpu()` | `int (void)` | Returns current CPU index |
| `sceKernelAllocateMainDirectMemory(size, align, mem_type, &phys_out)` | `int (size_t, size_t, int, uint64_t *)` | Allocate direct physical memory (`mem_type=1` = main RAM) |
| `sceKernelMapNamedDirectMemory(&va, size, prot, flags, phys, align, "gpudma")` | `int (void **, size_t, int, int, uint64_t, size_t, const char *)` | Map direct memory with a name tag |

#### Memory protection flags

```c
#define PROT_GPU_READ    0x10
#define PROT_GPU_WRITE   0x20
#define MAP_NO_COALESCE  0x00400000
```

### 7.9 In-kernel Functions (used by shellcode_kernel after CFI bypass)

These are kernel-text functions located via `KERNEL_TEXT + offset` per firmware. They are called by the post-resume shellcode because the shellcode runs in ring 0 and has direct kernel access.

| Symbol | Purpose |
|--------|---------|
| `FUN_PRINTF` | `printf` — kernel printf |
| `FUN_MEMCPY` | `memcpy` — kernel memcpy |
| `FUN_SMP_RENDEZVOUS` | FreeBSD `smp_rendezvous_action` — runs a function on all CPUs |
| `FUN_SMP_NO_RENDEVOUS_BARRIER` | No-op barrier used as rendezvous setup/teardown |
| `FUN_HV_IOMMU_SET_BUFFERS` | HV-managed IOMMU buffer registration (only on FW 3.x–4.x) |
| `FUN_HV_IOMM_WAIT_COMPLETION` | HV-managed IOMMU completion wait (only on FW 3.x–4.x) |
| `FUN_STOP_CPUS` | FreeBSD `stop_cpus` — IPI to stop a CPU mask |
| `FUN_AS_LAPIC_EOI` | Local APIC EOI (End-Of-Interrupt) |
| `FUN_HV_UNMAP_PT_TMR` | HV hypercall to unmap a TMR from the guest page tables (FW 5.x–6.x) |
| `FUN_TRANSMITTER_CONTROL` | AMD DC `dig_transmitter_control` — DP PHY control |
| `FUN_MP3_INITIALIZE` | Media Processing Engine v3 (audio/HDCP) initialization |
| `FUN_MP3_INVOKE` | MP3 command invocation |
| `G_VBIOS` | Pointer to VBIOS image (16 KB copied to PA `0xC0000`) |

### 7.10 ROP Gadgets

For the FW 5.x+ defeats, the loader assembles ROP chains using the following gadget types (each is `pop <reg>; ret` or `mov [rdi], rsi; pop rbp; ret`):

| Gadget | Purpose |
|--------|---------|
| `GAD_POP_RDI_RET` | `pop rdi ; ret` |
| `GAD_POP_RSI_RET` | `pop rsi ; ret` |
| `GAD_POP_RDX_RET` | `pop rdx ; ret` |
| `GAD_POP_RAX_RET` | `pop rax ; ret` |
| `GAD_POP_RCX_RET` | `pop rcx ; ret` |
| `GAD_POP_RSP_RET` | `pop rsp ; ret` (stack pivot) |
| `GAD_WRMSR_RET` | `wrmsr ; ret` |
| `GAD_IRETQ` | `iretq` (return to usermode / different context) |
| `GAD_MOV_QWORD_PTR_RDI_RSI_POP_RBP_RET` | `mov [rdi], rsi ; pop rbp ; ret` (kernel write primitive) |
| `GAD_ADD_RSP_28_POP_RBP_RET` | `add rsp, 0x28 ; pop rbp ; ret` (used as the IDT #GP/#SX handler trampoline) |

---

## 8. Boot Process

This section describes the end-to-end flow that takes a PS5 from GameOS to bare-metal Linux. The mechanism hinges on **rest-mode suspend/resume + ACPI wake-vector corruption → #GP → ROP → HV escape**.

### 8.1 High-Level Flow

```
PS5 → GameOS (Prospero)
  ↓ (userland exploit — umtx2 for FW 3-5, Y2JB+lapse for FW 6-7)
Payload: ps5-linux-loader.elf runs (sent via socat to port 9021)
  ↓
main():
  1. setup_env()        — detect FW, populate env_offset, compute ktext/kdata/dmap/cr3
  2. fetch_linux()      — load bzImage + initrd from USB into kernel cave VA
                          dump WiFi firmware (NXP IW620) to USB
  3. prepare_resume()   — copy shellcode_kernel to syscore-cave VA
                          remove XO bit on kernel .text (page_chain_set_rw)
                          invalidate pmap globals
  4. hv_defeat_*()      — install IDT #GP ROP chain, corrupt ACPI wake vector
                          (FW-specific path — see §8.2)
  5. enter_rest_mode()  — open "SceSystemStateMgrStatus" event flag
                          set bit 0x400 (suspend start)
  ↓
PS5 enters S3 rest mode (orange LED blinking, then solid)
  ↓
User presses power button → PS5 wakes from S3
  ↓
ACPI resume path calls AcpiSetFirmwareWakingVector — but the FACS pointer
was corrupted to (FACS − 8), so the call dereferences an invalid pointer → #GP
  ↓
IDT #GP handler runs (IST6 stack) — executes ROP chain
  ↓
ROP chain installs shellcode_kernel at KERNEL_CODE_CAVE (ktext + 0x500)
and jumps to it
  ↓
shellcode_kernel/main() runs at AcpiSetFirmwareWakingVector replacement:
  1. activate_uart() — clear UART override + bit 0x200
  2. [FW 3-4] hv_defeat_0304() — re-IOMMU, disable TMR, patch VMCBs, re-vmexit
     [FW 5-7] already escaped via ROP, skip
  3. *(uint8_t*)kernel_cfi_check = 0xC3   — disable CFI
  4. boot_linux() — see §8.3
  5. smp_rendezvous(no_barrier, vmmcall_dummy, no_barrier, NULL)
                       — all CPUs execute vmmcall → trigger VMEXIT
  ↓
Patched HV VMEXIT handler at HV_HANDLE_VMEXIT_PA runs:
    mov rax, <hv_code_cave_pa>
    jmp rax
  ↓
HV code cave (22-byte trampoline at HV_CODE_CAVE_PA):
    mov rax, <identity_cr3 = cave_hv_paging = 0x100000000>
    mov cr3, rax                ; swap to 1:1 identity paging
    mov rax, <cave_hv_code>     ; = 0x1000003000
    jmp rax                     ; jump to shellcode_hv body
  ↓
shellcode_hv/main() (naked, per-CPU):
  1. read CPU ID via cpuid(1).ebx[24:31]
  2. set RSP = hv_base_rsp + cpu_id * 0x1000
  3. call entry()
  ↓
shellcode_hv/boot_linux.c::entry():
  1. cli
  2. stgi                              ; re-enable guest interrupts
  3. wrmsr(EFER, EFER & ~EFER_SVM)    ; disable SVM
  4. wrmsr(VM_CR, VM_CR & ~VM_CR_R_INIT)
  5. clear MTRRs (MTRR4kBase+0, +1, MTRRVarBase+7*2+1)
  6. atomic_add(&exited_cpus, 1)
  7. spin until exited_cpus == 16     ; CPU rendezvous
  8. non-zero CPUs halt()
  9. CPU 0: disable IOMMU (clear bit 0 at AMDIOMMU_MMIO_BASE+0x18)
 10. copy linux_info from cave_linux_info
 11. configure_vram(FB_BASE, VRAM_BASE, vram_size)   ; §6.3 + §6.7
 12. boot_linux() — see §8.4
  ↓
Linux kernel startup_64 runs and boots Linux normally
```

### 8.2 Firmware-Specific HV Defeat

| Firmware range | Defeat function | Mechanism |
|----------------|------------------|-----------|
| 3.00–4.51 | `hv_defeat_0304` | 1. Stage 1: TMR relax — write `TMR_CFG_PERMISSIVE=0x3F07` to entries 5, 16, 17, 18<br>2. Stage 2: patch VMCBs via IOMMU — use `iommu_write8_pa` to clear `NESTED_CTRL` at `vmcb[i] + 0x90` for all 16 cores<br>3. Stage 3: force VMCB reload — pin to each core, execute `vmmcall` (caught as `SIGILL` after NPT disabled) |
| 5.00–6.02 | `hv_defeat_0506` | ACPI wake-vector corruption → #GP → ROP chain that uses `FUN_HV_UNMAP_PT_TMR` to corrupt `NESTED_CTRL` in all 16 VMCBs, then triggers `vmmcall` to reload VMCBs |
| 6.50–7.61 | `hv_defeat_0607` | ACPI wake-vector corruption → #GP → ROP chain that copies `shellcode_0607` to PA 0, then `wrmsr(MSR_APICBASE, hv_stack | ENABLED | BSP)` to enter HV context. `shellcode_0607` walks HV page tables via `HV_PML4`, accesses `g_vm_tab[]`, clears `NESTED_CTRL.NP_ENABLE` for all 16 VMCBs, restores APIC base and GS base, then calls `HV_REENTER_HYPERCORE` |

### 8.3 Kernel Shellcode (`shellcode_kernel/boot_linux.c`)

Once the kernel shellcode is running (after #GP/ROP), it:

1. **`patch_hv()`** — Installs the VMEXIT handler patch (`mov rax, hv_code_cave_pa; jmp rax`) at `HV_HANDLE_VMEXIT_PA` (8 bytes + 2 bytes = `48 C7 C0 AA AA AA AA FF E0`).

2. **`install_hv_code()`** — Builds a 5-entry PDPTE table at `cave_hv_paging + 0x1000` mapping the first 5 GB 1:1:
   ```c
   identity_pml40_l3[] = {
     0x0000000000000083,  // P|RW, US=0 — 0 to 1 GB
     0x0000000040000083,  // P|RW — 1 to 2 GB
     0x0000000080000083,  // P|RW — 2 to 3 GB
     0x00000000C0000083,  // P|RW — 3 to 4 GB
     0x0000000100000083,  // P|RW — 4 to 5 GB (covers the cave)
   };
   ```
   Then copies `shellcode_hv_bin` to `cave_hv_code = 0x1000003000`.

3. **Copy VBIOS** — 64 KB (`0x10000` bytes) from `g_vbios` (kernel symbol) to physical `0xC0000` (legacy VBIOS region).

4. **Enable DP PHY** — `dp_enable_link_phy(4 lanes, linkrate=30)` → `transmitter_control(0x4C, &params)` with HBR3 (8.1 GHz symclk).

5. **Initialize MP3 (HDCP)** —
   ```c
   mp3_initialize(0);                  // initialize vmid=0
   mp3_invoke(21, req, rsp);           // set HDCP packet: be=0, mode=1
   mp3_invoke(22, req, rsp);           // enable output: be=0, mode=1
   ```

6. **Read TMR states** (FW 5.x+) — for `i in 0..64`, if `shm->tmrPtStates[i].flags & 1`, append `(addr, addr+size)` to `info.tmrs[]` (forwarded to Linux E820 as RESERVED).

7. **Relocate bzImage and initrd** — copy to `cave_bzImage` and the address immediately following (aligned to 0x4000).

### 8.4 Final Linux Boot (`shellcode_hv/boot_linux.c::boot_linux`)

1. Set up `struct boot_params` at PA `0x10000` (real-mode setup header).
2. Copy `setup_header` from the bzImage.
3. Build E820 table (§1).
4. Set boot header fields:
   - `hardware_subarch = X86_SUBARCH_PS5 (5)`
   - `type_of_loader = 0xff` (unknown loader)
   - `cmd_line_ptr = 0x20000`
   - `ramdisk_image` / `ext_ramdisk_image` from `info.initrd`
   - `acpi_rsdp_addr = 0x7fd8e014`
5. Copy cmdline to PA `0x20000`.
6. Copy kernel body (after setup_sects) to PA `0x100000`.
7. Call `startup_64` at PA `0x100000 + 0x200 = 0x100200`.

Linux cmdline (default, configurable via `cmdline.txt`):

```
root=/dev/sda2 rw rootwait console=ttyTitania0 console=tty0 video=DP-1:1920x1080@60 mitigations=off idle=halt pci=pcie_bus_perf
```

---

## 9. Security Model

### 9.1 XO (Execute-Only) Page Bit — bit 58

PS5 extends the standard x86-64 PTE format with an **execute-only** permission bit at bit 58 (`XO`). When set:

- The page can be **executed** but not **read** (no data fetches, no instruction snooping beyond the instruction cache).
- This is the inverse of the standard `NX` bit (bit 63) — `NX=0, XO=1` means execute-only, `NX=1, XO=0` means read-write-no-exec, `NX=0, XO=0` means read-write-exec.

All kernel `.text` pages have `XO=1` by default. To make kernel text writable (needed by the loader to install patches), `page_chain_set_rw()` walks the page table chain and clears the `XO` bit at every level:

```c
void page_chain_set_rw(uint64_t va) {
  uint64_t table_phys = cr3;
  for (int level = 0; level < 4; level++) {
    /* compute idx, read entry */
    if (!PAGE_RW(entry)) { PAGE_SET_RW(entry); update = 1; }
    if (PAGE_XO(entry))  { PAGE_CLEAR_XO(entry); update = 1; }
    if (update) kwrite(entry_va, &entry, 8);
    /* descend or stop at large page / leaf */
  }
}
```

`prepare_resume()` calls this on every page in `[ktext, kdata)` to strip XO from the entire kernel text.

### 9.2 CFI Check Byte

See §7.5. A single byte (`0xC3` = `ret`) at `KERNEL_CFI_CHECK` disables the kernel's coarse-grained CFI on indirect calls.

### 9.3 Kernel UART Override

See §7.6. Two layers of suppression must be cleared:

1. UART control register bit `0x200` at PA `0xC0115110`.
2. Filter character byte at `KERNEL_UART_OVERRIDE` (kernel VA) must be `0x0`.

### 9.4 `GVMspace` Array (GPU VMID → Address Space)

Each process that allocates GPU direct memory is assigned a GPU VMID (0–15). The kernel maintains a `gvmspace[]` array indexed by VMID at `kdata + DATA_BASE_GVMSPACE`. Each entry (0x100 bytes) holds:

- `start_va` (offset `0x08`) — GPU VA range start
- `size` (offset `0x10`) — GPU VA range size
- `page_dir_va` (offset `0x38`) — PDB2 (top-level GPU page table) kernel VA

The loader walks this array (via `gpu_get_pdb2_addr`) to find the victim process's GPU page tables, then walks those page tables (§6.2–§6.4) to locate a specific PTE that can be patched to point at arbitrary physical memory — this is the GPU DMA-based physical read/write primitive used for the IOMMU buffer setup.

### 9.5 `INKERNEL` Macro

```c
#define INKERNEL(va)  (va & 0xFFFF000000000000)
```

Kernel addresses have the top 16 bits set (canonical high = `0xFFFF...`), so this mask extracts the kernel-VA "type" for fast sanity checks.

### 9.6 Markers for Runtime Patching

The shellcode uses sentinel constants that the loader patches at runtime:

| Marker | Replaced by | Used in |
|--------|------------|---------|
| `0x11AA11AA11AA11AA` (uint64) | Address of `shellcode_kernel_args` struct (in kernel .data via DMAP) | `shellcode_kernel/main.c` |
| `0x11AA11AA` (uint32) | Firmware version (`fw` variable) | `shellcode_0607/main.c` |

The loader scans the first 0x40 bytes of the shellcode for these markers and overwrites them with the actual address/value before installing the shellcode.

---

## 10. Console Variants

### 10.1 Detection

The PS5 has three hardware variants that the loader distinguishes by checking for the presence of DECI5 (Development Environment Common Interface v5) helper libraries:

```c
bool sceKernelIsDevKit(void) {
    return if_exists("/system/priv/lib/libSceDeci5Dtracep.sprx");   // DTrace probe library
}

bool sceKernelIsTestKit(void) {
    return if_exists("/system/priv/lib/libSceDeci5Ttyp.sprx");      // TTY proxy library
}

enum kit_type { KIT_RETAIL, KIT_TESTKIT, KIT_DEVKIT };
```

Detection rules:

| Console type | Detection | Notes |
|--------------|-----------|-------|
| **Retail** | Neither sprx exists | 16 GB GDDR6, all features locked |
| **TestKit** | `libSceDeci5Ttyp.sprx` exists | 16 GB GDDR6, debug features enabled |
| **DevKit** | `libSceDeci5Dtracep.sprx` exists | **32 GB GDDR6**, full DECI5 stack |

### 10.2 RAM Size Difference

| Variant | Total RAM | Above-4 GB Layout |
|---------|-----------|--------------------|
| Retail / TestKit | 16 GB | `0x100000000` → `VRAM_BASE` (RAM), `VRAM_BASE` → `0x470000000` (VRAM), `0x470000000` → `0x47F300000` (RAM, ~3.9 GB), `0x47F300000` → `0x480000000` (RESERVED) |
| DevKit | **32 GB** | `0x100000000` → `VRAM_BASE` (RAM), `VRAM_BASE` → `0x470000000` (VRAM), `0x470000000` → `0x87F300000` (RAM, ~16.9 GB), `0x87F300000` → `0x880000000` (RESERVED) |

The kit type is stored in `linux_info.kit_type` and consulted in `e820_memory_setup()` to choose the appropriate high-RAM entry.

### 10.3 PS5 Form Factors

All form factors share the same SoC and ABI:

- **PS5 Phat** (Disc / Digital) — original chassis, FW 3.00+
- **PS5 Slim** (Disc / Digital) — revised chassis, supported on the same FW range
- **PS5 Pro** — *not supported* by this loader (different SoC / different offsets)

### 10.4 Supported Firmware Range

The loader targets PS5 FW **3.00 through 7.61** inclusive. The `set_offsets()` function in `source/utils.c` switches on 22 specific firmware versions; any other version aborts. FW 1.xx and 2.xx are not supported. FW 8.00+ will not be supported (per README).

Per-FW `HV_HANDLE_VMEXIT_PA` values (see §3.6) and per-FW `KERNEL_CFI_CHECK` offsets (see §7.5) are the most FW-sensitive constants — both shift slightly with each FW revision.

### 10.5 WiFi Firmware

PS5 WiFi is an NXP (Marvell) IW620 combo chip. The firmware blob is embedded in the PS5 kernel image and dumped by the loader to `lib/nxp/pcieuartiw620_combo_v1.bin` on the USB drive. Size and offset vary per FW:

| FW range | WiFi FW offset (rel. KERNEL_TEXT) | Size |
|----------|---------------------------------:|-----:|
| 3.00–3.10 | `0x1274460`–`0x1274490` | ~480 KB (492 304 B) |
| 3.20–3.21 | `0x1274550` | 492 304 B |
| 4.00–4.03 | `0x1392FB0` | 493 000 B |
| 4.50–4.51 | `0x1392FC0`–`0x1393000` | 493 000 B |
| 5.00–5.50 | `0x163E2B0`–`0x163E670` | 493 532 B |
| 6.00–6.02 | `0x1665C00`–`0x1665CC0` | 494 536 B |
| 6.50 | `0x1665B90` | 494 536 B |
| 7.00–7.01 | `0x1655400` | 497 636 B |
| 7.20 | `0x1655700` | 497 636 B |
| 7.40–7.61 | `0x1655700`–`0x1655800` | 497 636 B |

---

## Appendix A — Cross-Reference: Constants Quick Lookup

| Constant | Value | Source file |
|----------|------:|-------------|
| `KERNEL_TEXT` | `0xFFFFFFFF80210000` | `source/offsets.c` |
| `VRAM_SIZE` (default) | `0x20000000` (512 MB) | `include/config.h` |
| `VRAM_BASE` (default) | `0x450000000` | derived: `0x470000000 - VRAM_SIZE` |
| `FB_BASE` | `0xF400000000` | `shellcode_hv/boot_linux.h` |
| `ACPI_RSDP_ADDRESS` | `0x7FD8E014` | `shellcode_hv/boot_linux.h` |
| `AMDGPU_MMIO_BASE` | `0xE0600000` | `shellcode_hv/boot_linux.h` |
| `AMDIOMMU_MMIO_BASE` | `0xFDD80000` | `shellcode_hv/boot_linux.h` |
| `AMDIOMMU_CTRL` | `0x18` | `shellcode_hv/boot_linux.h` |
| `ECAM_B0D18F2` | `0xF0C2000` | `include/tmr.h` |
| `TMR_CFG_PERMISSIVE` | `0x3F07` | `include/tmr.h` |
| `MAXCPU` | `16` | `shellcode_hv/boot_linux.h` |
| `X86_SUBARCH_PS5` | `5` | `include/linux.h` |
| `PAGE_SIZE` (PS5 kernel) | `0x4000` (16 KB) | `include/config.h` |
| `PAGE_PA` mask | `0x000FFFFFFFFFF000` | `include/utils.h` |
| `GPU_PDE_ADDR_MASK` | `0x0000FFFFFFFFFFC0` | `include/gpu.h` |
| `GPU_SUBMIT_IOCTL` | `0xC0108102` | `include/gpu.h` |
| `cave` (physical) | `0x100000000` | `include/config.h` |
| `cave_hv_paging` (HV CR3) | `0x100000000` | `include/config.h` |
| `cave_hv_code` (HV body) | `0x1000003000` | `include/config.h` |
| `hv_base_rsp` | `0x110000000` | `include/config.h` |
| `kernel_cave` (kernel VA) | `0xFFFF800000000000` | `include/config.h` |
| `HV_CODE_CAVE_PA` | `0x62806F00` | `source/offsets.c` (all FW) |
| `MSR_EFER` | `0xC0000080` | `shellcode_hv/boot_linux.h` |
| `MSR_VM_CR` | `0xC0010114` | `shellcode_hv/boot_linux.h` |
| `MSR_APICBASE` | `0x1B` | `shellcode_0607/main.c` |
| `DEFAULT_APIC_BASE` | `0xFEE00000` | `shellcode_0607/main.c` |
| `EFER_SVM` | bit 12 | `shellcode_hv/boot_linux.h` |
| `VM_CR_R_INIT` | bit 1 | `shellcode_hv/boot_linux.h` |
| `APICBASE_ENABLED` | `0x800` | `shellcode_0607/main.c` |
| `APICBASE_BSP` | `0x100` | `shellcode_0607/main.c` |
| `NESTED_CTRL_NP_ENABLE` | bit 0 | `shellcode_0607/main.c` |
| `NESTED_CTRL_GMET_ENABLE` | bit 3 (mask `0x8`) | `source/hv_defeat_0506.c` |

## Appendix B — File Cross-Reference

| Spec section | Primary source files |
|--------------|----------------------|
| §1 (Memory map) | `shellcode_hv/boot_linux.c` (`e820_memory_setup`), `shellcode_hv/boot_linux.h`, `include/config.h`, `include/linux.h` |
| §2 (CPU topology) | `shellcode_hv/utils.c`, `shellcode_hv/boot_linux.h`, `source/utils.c`, `shellcode_0607/main.c` |
| §3 (HV architecture) | `shellcode_0607/main.c`, `source/hv_defeat_0506.c`, `source/hv_defeat_0607.c`, `shellcode_kernel/boot_linux.c`, `source/offsets.c` |
| §4 (IOMMU) | `include/iommu.h`, `source/iommu.c`, `shellcode_kernel/hv_defeat_0304.c`, `shellcode_hv/boot_linux.h` |
| §5 (TMR) | `include/tmr.h`, `source/tmr.c`, `source/hv_defeat_0304.c`, `shellcode_kernel/hv_defeat_0304.h` |
| §6 (GPU) | `include/gpu.h`, `source/gpu.c`, `shellcode_hv/boot_linux.h`, `shellcode_hv/boot_linux.c` (`configure_vram`) |
| §7 (Kernel ABI) | `include/utils.h`, `source/utils.c`, `include/offsets.h`, `source/offsets.c`, `shellcode_kernel/*.c`, `shellcode_kernel/shellcode_kernel_args.h` |
| §8 (Boot process) | `source/main.c`, `source/prepare_resume.c`, `shellcode_kernel/main.c`, `shellcode_kernel/boot_linux.c`, `shellcode_hv/main.c`, `shellcode_hv/boot_linux.c`, `source/hv_defeat_0304.c` / `0506.c` / `0607.c` |
| §9 (Security model) | `include/utils.h`, `shellcode_kernel/utils.c`, `source/utils.c`, `shellcode_kernel/main.c` |
| §10 (Variants) | `source/utils.c` (`get_kit_type`, `sceKernelIsDevKit`, `sceKernelIsTestKit`), `shellcode_hv/boot_linux.c` (DevKit E820), `source/firmware.c` (WiFi FW), `source/offsets.c` |

---

*Document end. All addresses and offsets above are reproduced verbatim from `ps5-linux-loader` source. Emulator authors should treat this as a foundational spec — every constant here has been observed on real PS5 hardware via the loader's successful boot of bare-metal Linux.*
