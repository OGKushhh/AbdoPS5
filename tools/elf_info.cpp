// SPDX-FileCopyrightText: Copyright 2026 KytyPS5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Kyty-012: Standalone ELF/SELF info tool.
// Reads a PS5 ELF or SELF file and prints the ELF header, program headers,
// and section headers. Useful for reverse engineering without launching
// the full emulator.
//
// Usage:
//   kytyps5-elf-info <elf-or-self-file>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>
#include <string>

// PS5 SELF magic: 0x5414F5EE (distinct from PS4's 0x4F153D1D)
constexpr uint32_t SELF_MAGIC = 0x5414F5EE;
// Standard ELF magic: 0x7F454C46 ("\x7FELF")
constexpr uint32_t ELF_MAGIC = 0x464C457F;

#pragma pack(push, 1)

struct Elf64_Ehdr {
	uint8_t  e_ident[16];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct Elf64_Phdr {
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

struct Elf64_Shdr {
	uint32_t sh_name;
	uint32_t sh_type;
	uint64_t sh_flags;
	uint64_t sh_addr;
	uint64_t sh_offset;
	uint64_t sh_size;
	uint32_t sh_link;
	uint32_t sh_info;
	uint64_t sh_addralign;
	uint64_t sh_entsize;
};

#pragma pack(pop)

static const char* GetElfTypeName(uint16_t type) {
	switch (type) {
		case 0xFE10: return "ET_DYNEXEC (PS5 executable)";
		case 0xFE18: return "ET_DYNAMIC (PS5 shared library)";
		case 2: return "ET_EXEC (standard executable)";
		case 3: return "ET_DYN (standard shared library)";
		case 1: return "ET_REL (relocatable)";
		default: return "Unknown";
	}
}

static const char* GetPhdrTypeName(uint32_t type) {
	switch (type) {
		case 0: return "PT_NULL";
		case 1: return "PT_LOAD";
		case 2: return "PT_DYNAMIC";
		case 3: return "PT_INTERP";
		case 4: return "PT_NOTE";
		case 5: return "PT_SHLIB";
		case 6: return "PT_PHDR";
		case 7: return "PT_TLS";
		case 0x61000001: return "PT_OS_PROCPARAM (PS5 process params)";
		case 0x61000002: return "PT_OS_MODULEPARAM (PS5 module params)";
		case 0x6FFFFFFA: return "PT_LOSUNW";
		default: return "Unknown";
	}
}

int main(int argc, char* argv[]) {
	if (argc < 2) {
		std::printf("kytyps5-elf-info — PS5 ELF/SELF info tool (Kyty-012)\n\n");
		std::printf("Usage: kytyps5-elf-info <elf-or-self-file>\n");
		return 1;
	}

	std::ifstream file(argv[1], std::ios::binary | std::ios::ate);
	if (!file) {
		std::fprintf(stderr, "Error: cannot open %s\n", argv[1]);
		return 1;
	}

	const auto file_size = static_cast<uint64_t>(file.tellg());
	file.seekg(0, std::ios::beg);

	// Read first 4 bytes to check if it's SELF or ELF
	uint32_t magic = 0;
	file.read(reinterpret_cast<char*>(&magic), sizeof(magic));

	uint64_t elf_offset = 0;

	if (magic == SELF_MAGIC) {
		std::printf("File type: PS5 SELF (magic: 0x%08X)\n", magic);
		std::printf("File size: %llu bytes\n", file_size);
		// SELF header is 32 bytes, followed by ELF at offset 32
		// Actually, PS5 SELF has a more complex header. For now, search for ELF magic.
		std::vector<uint8_t> data(file_size);
		file.seekg(0);
		file.read(reinterpret_cast<char*>(data.data()), file_size);

		// Search for ELF magic within the first 4KB
		for (size_t i = 0; i < std::min(data.size() - 4, static_cast<size_t>(4096)); i++) {
			if (data[i] == 0x7F && data[i + 1] == 'E' && data[i + 2] == 'L' && data[i + 3] == 'F') {
				elf_offset = i;
				break;
			}
		}
		if (elf_offset == 0) {
			std::printf("Error: ELF header not found within SELF\n");
			return 1;
		}
		std::printf("ELF offset: 0x%llX\n", elf_offset);
		file.seekg(elf_offset);
	} else if (magic == ELF_MAGIC) {
		std::printf("File type: ELF (magic: 0x%08X)\n", magic);
		file.seekg(0);
	} else {
		std::fprintf(stderr, "Error: unknown magic: 0x%08X (expected SELF=0x%08X or ELF=0x%08X)\n",
		             magic, SELF_MAGIC, ELF_MAGIC);
		return 1;
	}

	// Read ELF header
	Elf64_Ehdr ehdr{};
	file.read(reinterpret_cast<char*>(&ehdr), sizeof(ehdr));
	if (!file) {
		std::fprintf(stderr, "Error: failed to read ELF header\n");
		return 1;
	}

	std::printf("\n=== ELF Header ===\n");
	std::printf("  e_type:      0x%04X (%s)\n", ehdr.e_type, GetElfTypeName(ehdr.e_type));
	std::printf("  e_machine:   0x%04X (%s)\n", ehdr.e_machine, ehdr.e_machine == 0x3E ? "x86-64" : "Unknown");
	std::printf("  e_version:   %u\n", ehdr.e_version);
	std::printf("  e_entry:     0x%016llX\n", ehdr.e_entry);
	std::printf("  e_phoff:     0x%016llX (%llu)\n", ehdr.e_phoff, ehdr.e_phoff);
	std::printf("  e_shoff:     0x%016llX (%llu)\n", ehdr.e_shoff, ehdr.e_shoff);
	std::printf("  e_flags:     0x%08X\n", ehdr.e_flags);
	std::printf("  e_ehsize:    %u\n", ehdr.e_ehsize);
	std::printf("  e_phentsize: %u\n", ehdr.e_phentsize);
	std::printf("  e_phnum:     %u\n", ehdr.e_phnum);
	std::printf("  e_shentsize: %u\n", ehdr.e_shentsize);
	std::printf("  e_shnum:     %u\n", ehdr.e_shnum);
	std::printf("  e_shstrndx:  %u\n", ehdr.e_shstrndx);

	// Read program headers
	if (ehdr.e_phnum > 0 && ehdr.e_phoff > 0) {
		std::printf("\n=== Program Headers (%u) ===\n", ehdr.e_phnum);
		file.seekg(elf_offset + ehdr.e_phoff);
		for (uint16_t i = 0; i < ehdr.e_phnum; i++) {
			Elf64_Phdr phdr{};
			file.read(reinterpret_cast<char*>(&phdr), sizeof(phdr));
			std::printf("  [%2u] type=0x%08X (%s) offset=0x%08llX vaddr=0x%016llX "
			            "filesz=0x%08llX memsz=0x%08llX flags=0x%X align=0x%llX\n",
			            i, phdr.p_type, GetPhdrTypeName(phdr.p_type),
			            phdr.p_offset, phdr.p_vaddr, phdr.p_filesz, phdr.p_memsz,
			            phdr.p_flags, phdr.p_align);
		}
	}

	// Read section headers
	if (ehdr.e_shnum > 0 && ehdr.e_shoff > 0) {
		std::printf("\n=== Section Headers (%u) ===\n", ehdr.e_shnum);
		file.seekg(elf_offset + ehdr.e_shoff);
		for (uint16_t i = 0; i < ehdr.e_shnum; i++) {
			Elf64_Shdr shdr{};
			file.read(reinterpret_cast<char*>(&shdr), sizeof(shdr));
			std::printf("  [%2u] name=%u type=0x%08X offset=0x%08llX size=0x%08llX "
			            "flags=0x%llX addr=0x%016llX\n",
			            i, shdr.sh_name, shdr.sh_type, shdr.sh_offset, shdr.sh_size,
			            shdr.sh_flags, shdr.sh_addr);
		}
	}

	std::printf("\nFile size: %llu bytes\n", file_size);
	return 0;
}
