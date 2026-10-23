//
// _CsimBackend_cc_
//
// Copyright (C) 2026 Tactical Computing Laboratories, LLC
// All Rights Reserved
// contact@tactcomplabs.com
//
// See LICENSE.txt in the top level directory for licensing details
//

#include "CsimBackend.h"

#include <algorithm>
#include <cstring>

namespace SST::Firedrake {

namespace {

/// Bytes reserved for an output scalar; generous so any C scalar fits
constexpr size_t OUTPUT_STORAGE = 16;

void checkRange( const ArgInfo& arg, uint64_t size, uint64_t offset, uint64_t len ) {
  if( offset > size || len > size - offset )
    throw BackendError( "buffer '" + arg.name + "' access [" + std::to_string( offset ) + ", +" + std::to_string( len ) +
                        ") exceeds its " + std::to_string( size ) + " byte allocation" );
}

}  // namespace

CsimBackend::CsimBackend( const KernelInfo& kernel, CsimInvoke invoke ) : KernelBackend( kernel ), fn( invoke ) {
  unsigned n = 0;
  for( const auto& a : kernel.args )
    n = std::max( n, a.index + 1 );
  values.resize( n );
  for( const auto& a : kernel.args ) {
    if( a.kind == ArgKind::SCALAR )
      values[a.index].assign( a.size, 0 );
    else if( a.kind == ArgKind::OUTPUT )
      values[a.index].assign( std::max<size_t>( a.size, OUTPUT_STORAGE ), 0 );
  }
}

void CsimBackend::open( const BackendConfig& ) {
  if( !fn )
    throw BackendError( "kernel '" + kernel.name + "' was generated without HLS sources; the csim backend is unavailable" );
  for( const auto& a : kernel.args )
    if( a.kind == ArgKind::STREAM )
      throw BackendError( "kernel '" + kernel.name + "' argument '" + a.name + "' is an axis stream; csim requires memory-mapped "
                          "or scalar arguments" );
}

void CsimBackend::setScalar( const ArgInfo& arg, const std::vector<uint8_t>& value ) {
  auto& v = values.at( arg.index );
  std::fill( v.begin(), v.end(), 0 );
  std::memcpy( v.data(), value.data(), std::min( v.size(), value.size() ) );
  if( kernel.protocol == CtrlProtocol::AP_CTRL_NONE )
    invoke();
}

std::vector<uint8_t> CsimBackend::getOutput( const ArgInfo& arg ) {
  const auto& v = values.at( arg.index );
  return std::vector<uint8_t>( v.begin(), v.begin() + std::min<ptrdiff_t>( arg.size, ptrdiff_t( v.size() ) ) );
}

void CsimBackend::allocBuffer( const ArgInfo& arg, uint64_t bytes ) {
  values.at( arg.index ).assign( bytes, 0 );
}

uint64_t CsimBackend::bufferSize( const ArgInfo& arg ) const {
  return values.at( arg.index ).size();
}

void CsimBackend::bufWrite( const ArgInfo& arg, uint64_t offset, const uint8_t* data, uint64_t len ) {
  auto& v = values.at( arg.index );
  checkRange( arg, v.size(), offset, len );
  if( len )
    std::memcpy( v.data() + offset, data, len );
}

void CsimBackend::bufRead( const ArgInfo& arg, uint64_t offset, uint8_t* data, uint64_t len ) {
  const auto& v = values.at( arg.index );
  checkRange( arg, v.size(), offset, len );
  if( len )
    std::memcpy( data, v.data() + offset, len );
}

void CsimBackend::start() {
  invoke();
}

void CsimBackend::invoke() {
  CsimArgs args;
  args.ptrs.resize( values.size(), nullptr );
  for( const auto& a : kernel.args )
    args.ptrs[a.index] = values[a.index].empty() ? nullptr : values[a.index].data();
  fn( args );
}

const ArgInfo* CsimBackend::argAt( uint32_t offset ) const {
  for( const auto& a : kernel.args )
    if( a.kind != ArgKind::STREAM && offset >= a.offset && offset < a.offset + std::max<uint32_t>( a.size, 4 ) )
      return &a;
  return nullptr;
}

void CsimBackend::writeReg( uint32_t offset, uint32_t value ) {
  const ArgInfo* a = argAt( offset );
  if( !a || a->kind != ArgKind::SCALAR )
    return;  // control, address and output registers are not writable here
  auto&        v   = values[a->index];
  const size_t pos = offset - a->offset;
  if( pos < v.size() )
    std::memcpy( v.data() + pos, &value, std::min<size_t>( 4, v.size() - pos ) );
  if( kernel.protocol == CtrlProtocol::AP_CTRL_NONE )
    invoke();
}

uint32_t CsimBackend::readReg( uint32_t offset ) {
  const ArgInfo* a = argAt( offset );
  if( !a )
    return 0;
  const size_t pos = offset - a->offset;
  if( a->kind == ArgKind::BUFFER ) {
    const uint64_t addr = deviceAddr( a->index );
    return pos < 4 ? uint32_t( addr ) : uint32_t( addr >> 32 );
  }
  const auto& v     = values[a->index];
  uint32_t    value = 0;
  if( pos < v.size() )
    std::memcpy( &value, v.data() + pos, std::min<size_t>( 4, v.size() - pos ) );
  return value;
}

}  // namespace SST::Firedrake

// EOF
