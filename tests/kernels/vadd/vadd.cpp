//
// _vadd_cpp_
//
// Copyright (C) 2026 Tactical Computing Laboratories, LLC
// All Rights Reserved
// contact@tactcomplabs.com
//
// See LICENSE.txt in the top level directory for licensing details
//
// Vitis HLS vector add (load/compute/store with hls::stream and dataflow),
// after the Vitis vadd example: out[i] = in1[i] + in2[i] for i < size.
//

#include <hls_stream.h>
#include <stdint.h>

static const int c_size = 4096;

static void read_input( const unsigned int* in, hls::stream<unsigned int>& s, int size ) {
mem_rd:
  for( int i = 0; i < size; i++ ) {
#pragma HLS LOOP_TRIPCOUNT min = c_size max = c_size
    s << in[i];
  }
}

static void compute_add( hls::stream<unsigned int>& a, hls::stream<unsigned int>& b, hls::stream<unsigned int>& o, int size ) {
execute:
  for( int i = 0; i < size; i++ ) {
#pragma HLS LOOP_TRIPCOUNT min = c_size max = c_size
    o << ( a.read() + b.read() );
  }
}

static void write_result( unsigned int* out, hls::stream<unsigned int>& s, int size ) {
mem_wr:
  for( int i = 0; i < size; i++ ) {
#pragma HLS LOOP_TRIPCOUNT min = c_size max = c_size
    out[i] = s.read();
  }
}

extern "C" void vadd( const unsigned int* in1, const unsigned int* in2, unsigned int* out, int size ) {
#pragma HLS INTERFACE mode = m_axi port = in1 bundle = gmem0
#pragma HLS INTERFACE mode = m_axi port = in2 bundle = gmem1
#pragma HLS INTERFACE mode = m_axi port = out bundle = gmem0
#pragma HLS INTERFACE mode = s_axilite port = size
#pragma HLS INTERFACE mode = ap_ctrl_chain port = return
  static hls::stream<unsigned int> s1( "in1_stream" );
  static hls::stream<unsigned int> s2( "in2_stream" );
  static hls::stream<unsigned int> so( "out_stream" );
#pragma HLS dataflow
  read_input( in1, s1, size );
  read_input( in2, s2, size );
  compute_add( s1, s2, so, size );
  write_result( out, so, size );
}

// EOF
