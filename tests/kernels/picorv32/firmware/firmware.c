//
// _firmware_c_
//
// Copyright (C) 2026 Tactical Computing Laboratories, LLC
// All Rights Reserved
// contact@tactcomplabs.com
//
// See LICENSE.txt in the top level directory for licensing details
//
// PicoRV32 test firmware (rv32im, freestanding).  Reads n and in[] from
// the kernel buffer, writes out[] and a struct prv_result, then returns to
// start.S, which traps the core.  No .data or .bss: every run starts from
// the image the host wrote.
//

#include "picorv32_test.h"

#define REG32( addr ) ( *(volatile uint32_t*) ( addr ) )

static uint32_t __attribute__( ( noinline ) ) fib( uint32_t n ) {
  return n < 2u ? n : fib( n - 1u ) + fib( n - 2u );
}

static inline uint32_t rdcycle( void ) {
  uint32_t v;
  __asm__ volatile( "rdcycle %0" : "=r"( v ) );
  return v;
}

static inline uint32_t rdinstret( void ) {
  uint32_t v;
  __asm__ volatile( "rdinstret %0" : "=r"( v ) );
  return v;
}

int main( void ) {
  volatile const uint32_t* in  = (volatile const uint32_t*) PRV_IN;
  volatile uint32_t*       out = (volatile uint32_t*) PRV_OUT;
  volatile struct prv_result* res = (volatile struct prv_result*) PRV_RESULT;

  uint32_t n = REG32( PRV_PARAMS );
  if( n > PRV_MAX_N )
    n = PRV_MAX_N;

  uint32_t c = 0;
  for( uint32_t i = 0; i < n; i++ ) {
    const uint32_t v = prv_element( in[i], i );
    out[i]           = v;
    c                = prv_checksum( c, v );
  }

  res->n        = n;
  res->checksum = c;
  res->fib      = fib( PRV_FIB_N );
  res->cycles   = rdcycle();
  res->instret  = rdinstret();
  res->magic    = PRV_MAGIC;
  return 0;
}

// EOF
