/* Measurement link: oversized regions so nothing overflows; the real
   CH32V203C8 budget (64 KB flash / 20 KB RAM) is checked by the script. */
MEMORY { FLASH (rx) : ORIGIN = 0x00000000, LENGTH = 4M
         RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 4M }
ENTRY(_start)
SECTIONS {
  .text : { KEEP(*(.init)) *(.text .text.*) *(.rodata .rodata.* .srodata .srodata.*) } > FLASH
  .data : { __data_start = .; *(.data .data.* .sdata .sdata.*) __data_end = .; } > RAM AT > FLASH
  .bss (NOLOAD) : { __bss_start = .; *(.bss .bss.* .sbss .sbss.* COMMON) __bss_end = .; } > RAM
  end = .; _end = .;
  __global_pointer$ = __data_start + 0x800;
}
