/*
 * _firmware_ld_
 *
 * Copyright (C) 2026 Tactical Computing Laboratories, LLC
 * All Rights Reserved
 * contact@tactcomplabs.com
 *
 * See LICENSE.txt in the top level directory for licensing details
 *
 * PicoRV32 test firmware: code and read-only data from address 0, below
 * PRV_CODE_LIMIT (0x8000).  Writable static data is not allowed.
 */

OUTPUT_ARCH( "riscv" )
ENTRY( _start )

MEMORY {
  code ( rx ) : ORIGIN = 0x00000000, LENGTH = 0x8000
}

SECTIONS {
  .text : {
    KEEP( *(.text.start) )
    *(.text .text.*)
    *(.rodata .rodata.* .srodata .srodata.*)
  } > code

  .writable : { *(.data .data.* .sdata .sdata.* .bss .bss.* .sbss .sbss.* COMMON) } > code

  /DISCARD/ : { *(.comment) *(.riscv.attributes) *(.note*) *(.eh_frame*) }
}

ASSERT( SIZEOF( .writable ) == 0, "picorv32 firmware must not have writable static data" )

/* EOF */
