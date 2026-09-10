/*
 * Keep enough read-only payload in the first Vita PT_LOAD to push the next
 * writable load segment onto the following normal linker boundary.  This
 * leaves vita-elf-create real inter-segment address space for its generated
 * .sce* metadata without changing global ELF page alignment.
 *
 * The symbol is forced live from CMake with --undefined so --gc-sections
 * cannot discard it.
 */
__attribute__((used, aligned(16), section(".rodata.vita_sce_metadata_pad")))
const unsigned char yabause_vita_sce_metadata_pad[8192] = { 0 };
