#ifndef KYTY_LOADER_X64_INSTRUCTION_EMULATOR_H_
#define KYTY_LOADER_X64_INSTRUCTION_EMULATOR_H_

#include <Zydis/DecoderTypes.h>
#include <cstdint>

namespace Loader::X64InstructionEmulator {

[[nodiscard]] bool IsReciprocalSquareRoot(const ZydisDecodedInstruction& instruction,
                                         const ZydisDecodedOperand* operands);
uint64_t           PatchReciprocalSquareRoots(uint64_t address, uint64_t size);
[[nodiscard]] bool TryEmulate(void* native_context);

// Kyty-016: Try to handle an MMIO access that caused a page fault.
//
// When the guest CPU accesses an MMIO address (e.g. 0xFDD80000 for the
// IOMMU), the address isn't mapped in the host address space, causing
// a SIGSEGV / EXCEPTION_ACCESS_VIOLATION. This function:
//   1. Decodes the faulting x86 instruction using Zydis
//   2. If it's a MOV to/from memory, extracts the access info (read/write,
//      size, register)
//   3. Translates the faulting VA to a physical address (heuristic: mask
//      to lower 32 bits — works because PS5's DMAP base is 4GB-aligned)
//   4. Checks if the physical address is in any registered MMIO handler
//   5. Dispatches the read/write to the handler
//   6. For reads: writes the result back to the guest register
//   7. Advances RIP past the faulting instruction
//   8. Returns true if handled
//
// native_context: the platform-specific context (PCONTEXT on Windows,
//                 ucontext_t* on Linux/macOS)
// fault_addr: the virtual address that caused the fault
// is_write: true if the fault was a write, false if a read
//
// Returns true if the access was handled (instruction emulated, RIP
// advanced). Returns false if the instruction couldn't be decoded
// or the address isn't in any MMIO range.
[[nodiscard]] bool TryHandleMmioAccess(void* native_context, uint64_t fault_addr, bool is_write);

} // namespace Loader::X64InstructionEmulator

#endif /* KYTY_LOADER_X64_INSTRUCTION_EMULATOR_H_ */
