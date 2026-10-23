//
// _regfile_cpp_
//
// Copyright (C) 2026 Tactical Computing Laboratories, LLC
// All Rights Reserved
// contact@tactcomplabs.com
//
// See LICENSE.txt in the top level directory for licensing details
//
// Free-running (ap_ctrl_none) s_axilite-only kernel: the outputs track the
// inputs continuously, so it is reached through xrt::ip register access.
//

#include <stdint.h>

extern "C" void regfile( int a, int b, unsigned int mask, int* sum, int* prod, unsigned int* masked ) {
#pragma HLS INTERFACE mode = s_axilite port = a
#pragma HLS INTERFACE mode = s_axilite port = b
#pragma HLS INTERFACE mode = s_axilite port = mask
#pragma HLS INTERFACE mode = s_axilite port = sum
#pragma HLS INTERFACE mode = s_axilite port = prod
#pragma HLS INTERFACE mode = s_axilite port = masked
#pragma HLS INTERFACE mode = ap_ctrl_none port = return
  *sum    = a + b;
  *prod   = a * b;
  *masked = static_cast<unsigned int>( a ) & mask;
}

// EOF
