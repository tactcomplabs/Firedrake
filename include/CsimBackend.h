//
// _CsimBackend_h_
//
// Copyright (C) 2026 Tactical Computing Laboratories, LLC
// All Rights Reserved
// contact@tactcomplabs.com
//
// See LICENSE.txt in the top level directory for licensing details
//

#ifndef _SST_FIREDRAKE_CSIMBACKEND_H_
#define _SST_FIREDRAKE_CSIMBACKEND_H_

#include "Backend.h"

namespace SST::Firedrake {

/// CsimBackend: runs the HLS top function natively (C simulation).
///
/// Buffers live in host memory and scalars in a byte image laid out like the
/// kernel's s_axilite register map, so raw register accesses behave as on a
/// device.  Buffer argument registers read back as synthetic device
/// addresses.  An ap_ctrl_none kernel is free running: the function is
/// re-invoked after every input register write.
class CsimBackend final : public KernelBackend {
public:
  CsimBackend( const KernelInfo& kernel, CsimInvoke invoke );

  void                 open( const BackendConfig& cfg ) override;
  void                 setScalar( const ArgInfo& arg, const std::vector<uint8_t>& value ) override;
  std::vector<uint8_t> getOutput( const ArgInfo& arg ) override;
  void                 allocBuffer( const ArgInfo& arg, uint64_t bytes ) override;
  uint64_t             bufferSize( const ArgInfo& arg ) const override;
  void                 bufWrite( const ArgInfo& arg, uint64_t offset, const uint8_t* data, uint64_t len ) override;
  void                 bufRead( const ArgInfo& arg, uint64_t offset, uint8_t* data, uint64_t len ) override;
  void                 start() override;
  void                 wait() override {}
  void                 writeReg( uint32_t offset, uint32_t value ) override;
  uint32_t             readReg( uint32_t offset ) override;

private:
  /// Calls the native kernel function with the current argument values
  void invoke();

  /// Returns the argument whose register range covers `offset`, or nullptr
  const ArgInfo* argAt( uint32_t offset ) const;

  /// Returns the synthetic device address of buffer argument `idx`
  static uint64_t deviceAddr( unsigned idx ) { return ( uint64_t( idx ) + 1 ) << 32; }

  CsimInvoke                        fn;
  std::vector<std::vector<uint8_t>> values;  ///< per argument index: scalar/output bytes or buffer contents
};

}  // namespace SST::Firedrake

#endif  // _SST_FIREDRAKE_CSIMBACKEND_H_
