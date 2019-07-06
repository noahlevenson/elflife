#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <elf.h>

// Corresponds to Linux kernel virtual address space - ie, at what base address should we store executable code and at what 
// base address should we store data values?
// a linker needs to evaluate the executable and data size of a given program and pick values that reside within user space
#define K_PROCESS_ADDR_EXEC 0x10000
#define K_PROCESS_ADDR_DATA 0x20000

// Executable program segments must be aligned at a virtual address such that virtual address = (offset in file % alignment)
// I don't super understand this very well
#define K_PROCESS_ALIGN 0x10000

// TODO: I think we'd want to have our own types which wrap the linux types? With an impl ptr?
// that way if the host machine doesn't have the linux type or the linux type is broken it's easier to patch?
void uconf_ehdr(Elf32_Ehdr* file_header, Elf32_Addr entry, Elf32_Off sh_off, uint16_t phnum, uint16_t shnum, uint16_t shstrndx) {
	// Ident array, magic numbers
	file_header->e_ident[EI_MAG0] = ELFMAG0;
	file_header->e_ident[EI_MAG1] = ELFMAG1;
	file_header->e_ident[EI_MAG2] = ELFMAG2;
	file_header->e_ident[EI_MAG3] = ELFMAG3;

	// Ident array, architecture class
	file_header->e_ident[EI_CLASS] = ELFCLASS32;

	// Ident array, data encoding
	// TODO: Do 32 bit ARM processes have switchable endianness?
	file_header->e_ident[EI_DATA] = ELFDATA2LSB;

	// Ident array, version number
	file_header->e_ident[EI_VERSION] = EV_CURRENT;

	// Ident array, OS and ABI
	// TODO: Is this correct in all cases? I copied it from an executable linked with ld on Raspbian
	file_header->e_ident[EI_OSABI] = ELFOSABI_SYSV;

	// Ident array, ABI version
	file_header->e_ident[EI_OSABI] = 0; // Should we not set a value here instead?

	// Ident array, start of padding
	file_header->e_ident[EI_PAD] = 0;

	// Type
	file_header->e_type = ET_EXEC;

	// Machine architecture
	file_header->e_machine = EM_ARM;

	// File version
	file_header->e_version = EV_CURRENT;

	// Entry point, aka first machine instruction - corresponds w addr of .text section
	file_header->e_entry = entry;

	// Program header table's file offset (program header table begins right after the elf file header)
	file_header->e_phoff = sizeof(Elf32_Ehdr); // TODO: WARNING - THIS ONLY WORKS BECAUSE WE'VE PACKED OUR PROGRAM HEADERS AFTER OUR ELF HEADER!

	// Section header table's file offset
	file_header->e_shoff = sh_off; 

	// Processor specific flags
	// file_header->e_flags = // TODO: Not sure how to set these, but some definitely appear in our dumps

	// This ELF header's size
	file_header->e_ehsize = sizeof(Elf32_Ehdr);

	// Program header size
	file_header->e_phentsize = sizeof(Elf32_Phdr);

	// Number of entries in the program header table
	// TODO: Handle the special case when we exceed PN_XNUM
	file_header->e_phnum = phnum;

	// Section header size
	file_header->e_shentsize = sizeof(Elf32_Shdr);

	// Number of entries in the section header table
	// TODO: Handle the special case where we exceed SHN_LORESERVE
	file_header->e_shnum = shnum;

	// Section header table index of the section header associated with the section name string table
	file_header->e_shstrndx = shstrndx;
}

int main (int argc, char* argv[]) {
	// First attempt: Can we make an executable that only has a .text, .data, and .shstrtab section?

	// Step 1: Create your .text and .data buffers (these will be emitted by the assembler)	
	uint32_t text_size = 0x20;
	uint8_t text[0x20] = {
		0x04, 0x70, 0xa0, 0xe3, 0x01, 0x00, 0xa0, 0xe3, 0x0d, 0x20, 0xa0, 0xe3, 0x08, 0x10, 0x9f, 0xe5, 0x00, 0x00, 
		0x00, 0xef, 0x01, 0x70, 0xa0, 0xe3, 0x00, 0x00, 0x00, 0xef, 0x94, 0x00, 0x02, 0x00
	};
	
	uint32_t data_size = 0x0d;
	uint8_t data[0x0d] = {0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x20, 0x57, 0x6f, 0x72, 0x6c, 0x64, 0x21, 0x0a}; 

	// Step 2: Construct your section header string table	
	// Here's where you probably want a hashmap or list of lists of all your section names and corresponding data	
	char* text_s = ".text";
	char* data_s = ".data";
	char* shstrtab_s = ".shstrtab";	
	
	uint32_t shstrtab_size = 1 + strlen(text_s) + 1 + strlen(data_s) + 1 + strlen(shstrtab_s) + 1;
	char shstrtab[shstrtab_size];
	sprintf(shstrtab, "\0%s%s%s", text_s, data_s, shstrtab_s); 				

	// Step 3: Figure out how we're packing this file and cache some offsets
	uint32_t elf_header_off = 0;
	uint32_t pheader_off = sizeof(Elf32_Ehdr);
	uint32_t text_off = pheader_off + sizeof(Elf32_Phdr) * 2; // TODO: replace this magic number
	uint32_t data_off = text_off + text_size;
	uint32_t shstrtab_off = data_off + data_size;
	uint32_t sheader_off = shstrtab_off + shstrtab_size; 
	
	// Step 3: Construct your section header table and populate each section header	
	Elf32_Shdr sheader_tab[4];

	// Create section header for .text section
	// Here's where you'd probably want to iterate through your data structure of section names and programmatically calculate offsets
	sheader_tab[1].sh_name = 1; // .text begins at the 1st index in shstrtab, since element 0 is a null byte
	sheader_tab[1].sh_type = SHT_PROGBITS; // executable sections are progbits :)
	sheader_tab[1].sh_flags = SHF_ALLOC & SHF_EXECINSTR; // these bits must be set for executable sections
	sheader_tab[1].sh_addr = K_PROCESS_ADDR_EXEC + text_off; // Exec process memory base address seems arbitrary - for small programs, GCC uses 0x10000
	sheader_tab[1].sh_offset = text_off;
	sheader_tab[1].sh_size = text_size;
	sheader_tab[1].sh_link = 0; // TODO: What is this?
	sheader_tab[1].sh_info = 0; // TODO: What is this?
	sheader_tab[1].sh_addralign = 2; // sh_addr % (2 ^ sh_addralign) must == 0, where sh_addralign must be a power of 2. 
					  // You can also just set to 0 or 1 - it's just an opportunity to maximize memory efficiency 
					  // if sh_addr is a higher power of 2   	 
	sheader_tab[1].sh_entsize = 0; // Only meaningful if this section holds a table of fixed-size entries, like a symbol table

	// Create section header for .data section
	sheader_tab[2].sh_name = sheader_tab[1].sh_name + strlen(text_s) + 1; // This is how you'd calculate it in a loop
	sheader_tab[2].sh_type = SHT_PROGBITS; // Data is also progbits :)
	sheader_tab[2].sh_flags = SHF_WRITE & SHF_ALLOC; // these bits must be set for data sections
	sheader_tab[2].sh_addr = K_PROCESS_ADDR_DATA + data_off; // Data process memory base address seems arbitrary - for small programs, GCC uses 0x20000
	sheader_tab[2].sh_offset = data_off;
	sheader_tab[2].sh_size = data_size;
	sheader_tab[2].sh_link = 0;
	sheader_tab[2].sh_info = 0;
	sheader_tab[2].sh_addralign = 1; // Our example file used zero, but 0x20094 % 2 == 0? Am I making it more efficient or breaking things?
	sheader_tab[2].sh_entsize = 0;

	// Create section header for .shstrtab section
	sheader_tab[3].sh_name = sheader_tab[2].sh_name + strlen(data_s) + 1;
	sheader_tab[3].sh_type = SHT_STRTAB;
	// sheader_tab[3]->sh_flags // Don't set flag bits because no attributes must be set
	sheader_tab[3].sh_addr = 0;
	sheader_tab[3].sh_offset = shstrtab_off;
	sheader_tab[3].sh_size = shstrtab_size;
	sheader_tab[3].sh_link = 0;
	sheader_tab[3].sh_info = 0;
	sheader_tab[3].sh_addralign = 0; // Doesn't appear in process memory, so set to zero
	sheader_tab[3].sh_entsize = 0;

	// Step 3: Construct your program header table and populate each program header
	Elf32_Phdr pheader_tab[2];

	// Create program header for first section which encapsulates the ELF header, program headers, and .text executable section
	pheader_tab[0].p_type = PT_LOAD;
	pheader_tab[0].p_offset = elf_header_off;
	pheader_tab[0].p_vaddr = K_PROCESS_ADDR_EXEC + elf_header_off; 
	pheader_tab[0].p_paddr = K_PROCESS_ADDR_EXEC + elf_header_off; // On Linux systems, physical address is ignored
	pheader_tab[0].p_filesz = data_off + data_size; // Here you'd programmatically calculate the size of the region you decided to include in this pheader
	pheader_tab[0].p_memsz = pheader_tab[0].p_filesz; // Not sure about a case where mem image size != file image size?
	pheader_tab[0].p_flags = PF_R & PF_X; // These flags must be set for your executable region - adding W would make the code self-modifiable :)
	pheader_tab[0].p_align = K_PROCESS_ALIGN;
 
	// Create program header for second section, which is just the .data section
	pheader_tab[1].p_type = PT_LOAD;
	pheader_tab[1].p_offset = data_off;
	pheader_tab[1].p_vaddr = K_PROCESS_ADDR_DATA + data_off; 
	pheader_tab[1].p_paddr = K_PROCESS_ADDR_DATA + data_off; // On Linux systems, physical address is ignored
	pheader_tab[1].p_filesz = data_size;
	pheader_tab[1].p_memsz = pheader_tab[1].p_filesz;
	pheader_tab[1].p_flags = PF_R & PF_W; // These flags must be set for your data region
	pheader_tab[1].p_align = K_PROCESS_ALIGN;	
	
	// Figure some things out for your ELF header
	Elf32_Addr entry = K_PROCESS_ADDR_EXEC + text_off; // Entry point = virt memory addr where your first executable instruction is - aka base address + offset of .text section
	Elf32_Off sh_off = sheader_off;	
	
	uint32_t num_pheaders = sizeof(pheader_tab) / sizeof(Elf32_Phdr);
	uint32_t num_sheaders = sizeof(sheader_tab) / sizeof(Elf32_Shdr);

	Elf32_Ehdr* file_header = malloc(sizeof(Elf32_Ehdr));
	uconf_ehdr(file_header, entry, sh_off, num_pheaders, num_sheaders, 3); 
	
	FILE* dest_file = fopen("./test", "w+");
	fwrite(file_header, 1, sizeof(Elf32_Ehdr), dest_file);
	//fwrite(&pheader_tab, 1, sizeof(pheader_tab), dest_file);
	//fwrite(&text, 1, text_size, dest_file);
	//fwrite(&data, 1, data_size, dest_file);
	//fwrite(&shstrtab, 1, shstrtab_size, dest_file);
	//fwrite(&sheader_tab, 1, sizeof(sheader_tab), dest_file); 
	
	fclose(dest_file);
	free(file_header);
	return 0;
}
