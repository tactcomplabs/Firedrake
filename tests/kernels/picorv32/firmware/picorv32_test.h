//
// _picorv32_test_h_
//
// Copyright (C) 2026 Tactical Computing Laboratories, LLC
// All Rights Reserved
// contact@tactcomplabs.com
//
// See LICENSE.txt in the top level directory for licensing details
//
// Memory map and reference computation shared by the PicoRV32 test
// firmware (C, rv32im) and the SST test host (C++).  Addresses are core
// addresses, which are also byte offsets into the kernel's `mem` buffer.
//

#ifndef _PICORV32_TEST_H_
#define _PICORV32_TEST_H_

#define PRV_MEM_BYTES   0x10000u  /* core address window (picorv32_kernel MEM_BYTES) */
#define PRV_CODE_LIMIT  0x8000u   /* firmware image must end below this */
#define PRV_PARAMS      0x8000u   /* uint32_t n: elements to process */
#define PRV_RESULT      0x8100u   /* struct prv_result */
#define PRV_IN          0x9000u   /* uint32_t in[n] */
#define PRV_OUT         0xA000u   /* uint32_t out[n] */
#define PRV_MAX_N       1024u
#define PRV_STACK_TOP   0x10000u
#define PRV_MAGIC       0x7069636fu /* "pico" */
#define PRV_FIB_N       12u

/* picorv32_kernel `status` register bits */
#define PRV_STATUS_TRAP    1u
#define PRV_STATUS_TIMEOUT 2u

#ifndef __ASSEMBLER__

#include <stdint.h>

struct prv_result {
  uint32_t magic;    /* PRV_MAGIC, written last */
  uint32_t n;        /* n as read by the firmware */
  uint32_t checksum; /* prv_checksum over out[0..n) */
  uint32_t fib;      /* prv_fib(PRV_FIB_N), recursive */
  uint32_t cycles;   /* rdcycle at the end of main */
  uint32_t instret;  /* rdinstret at the end of main */
};

/* out[i] for in[i] = x: exercises mul, mulhu, div, divu, remu and shifts */
static inline uint32_t prv_element( uint32_t x, uint32_t i ) {
  uint32_t d = ( i & 7u ) + 1u;
  uint32_t v = x * ( x + i );
  v += x / d;
  v -= x % 13u;
  v ^= (uint32_t) ( ( (uint64_t) x * 0x9e3779b9u ) >> 32 );
  v += (uint32_t) ( (int32_t) x / -7 );
  return v ^ ( i << 3 );
}

static inline uint32_t prv_checksum( uint32_t c, uint32_t v ) {
  return ( ( c << 5 ) | ( c >> 27 ) ) ^ v;
}

static inline uint32_t prv_fib( uint32_t n ) {
  return n < 2u ? n : prv_fib( n - 1u ) + prv_fib( n - 2u );
}

/* input value i of run r */
static inline uint32_t prv_input( uint32_t i, uint32_t r ) {
  return i * 0x01000193u + r * 0x9e37u + 0x811c9dc5u;
}

#endif /* __ASSEMBLER__ */

#endif /* _PICORV32_TEST_H_ */
