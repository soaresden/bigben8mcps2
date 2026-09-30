ENTRY(res_hdr)
SECTIONS {
  . = 0x000C0000;
  .text : { *(.text.start) *(.text*) }
  .rodata : { *(.rodata*) }
  .data : { *(.data*) *(.sdata*) }
  .bss : { *(.sbss*) *(.bss*) *(COMMON) }
  . = ALIGN(16);
  res_stack_top = res_stack + 2048;
  _res_end = .;
  /DISCARD/ : { *(.comment) *(.MIPS.*) *(.reginfo) *(.pdr) *(.mdebug*) *(.gnu.attributes) }
}
