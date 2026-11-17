OUTPUT_ARCH(arm)
ENTRY(_start)
MEMORY {
  rom   (rx)  : ORIGIN = 0x08000000, LENGTH = 32M
  ewram (rwx) : ORIGIN = 0x02000000, LENGTH = 256K
  iwram (rwx) : ORIGIN = 0x03000000, LENGTH = 31K
}
SECTIONS {
  .text : {
    KEEP(*(.crt0))
    *(.text .text.*)
    *(.rodata .rodata.*)
    . = ALIGN(4);
  } > rom
  .ARM.exidx : { *(.ARM.exidx*) } > rom
  __data_lma = ALIGN(4);
  .data : AT(__data_lma) {
    __data_start = .;
    *(.iwram .iwram.*)
    *(.data .data.*)
    . = ALIGN(4);
    __data_end = .;
  } > iwram
  .bss (NOLOAD) : {
    __bss_start = .;
    *(.bss .bss.* COMMON)
    . = ALIGN(4);
    __bss_end = .;
  } > iwram
  .ewram (NOLOAD) : {
    __ewram_start = .;
    *(.ewram .ewram.*)
    . = ALIGN(4);
    __ewram_end = .;
  } > ewram
  /DISCARD/ : { *(.comment) *(.ARM.attributes) }
}
