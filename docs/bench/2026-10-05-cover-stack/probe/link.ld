OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY { RAM (rwx) : ORIGIN = 0x80000000, LENGTH = 512M }

SECTIONS
{
    .text   : { *(.text.start) *(.text .text.*) } > RAM
    .rodata : { *(.rodata .rodata.* .srodata .srodata.*) } > RAM
    .data   : { *(.data .data.*) } > RAM
    .sdata  : { __global_pointer$ = . + 0x800; *(.sdata .sdata.*) } > RAM
    .tdata  : { _tdata_start = .; *(.tdata .tdata.*) _tdata_end = .; } > RAM
    .tbss   : { _tbss_start = .; *(.tbss .tbss.*) _tbss_end = .; } > RAM
    .bss (NOLOAD) : { . = ALIGN(16); _bss_start = .; *(.sbss .sbss.* .bss .bss.* COMMON) . = ALIGN(16); _bss_end = .; } > RAM

    /* The stack under test: 256 KB, far more than anything here should need, painted
       before every call and scanned after it. */
    . = ALIGN(16);
    _stack_bottom = .;
    . += 0x40000;
    _stack_top = .;

    _heap_start = .;
    _heap_end = ORIGIN(RAM) + LENGTH(RAM);
}
