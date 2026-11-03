//
// _start_S_
//
// Copyright (C) 2026 Tactical Computing Laboratories, LLC
// All Rights Reserved
// contact@tactcomplabs.com
//
// See LICENSE.txt in the top level directory for licensing details
//
// PicoRV32 test firmware entry: picorv32_kernel releases the core at
// address 0.  ebreak traps the core, which ends the kernel run.
//

  .section .text.start
  .global _start
_start:
  li    sp, 0x10000         // PRV_STACK_TOP (picorv32_test.h)
  call  main
  ebreak
1:
  j     1b

// EOF
