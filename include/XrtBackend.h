//
// _XrtBackend_h_
//
// Copyright (C) 2026 Tactical Computing Laboratories, LLC
// All Rights Reserved
// contact@tactcomplabs.com
//
// See LICENSE.txt in the top level directory for licensing details
//

#ifndef _SST_FIREDRAKE_XRTBACKEND_H_
#define _SST_FIREDRAKE_XRTBACKEND_H_

#include "Backend.h"

#include <memory>
#include <mutex>

namespace SST::Firedrake {

/// XrtBackend: drives a kernel through the native XRT C++ API (sw_emu,
/// hw_emu or hw).  ap_ctrl_hs / ap_ctrl_chain kernels use xrt::kernel and
/// xrt::run; ap_ctrl_none and user-managed kernels use xrt::ip with raw
/// register access.  XRT types are hidden behind `Impl` so this header does
/// not require the XRT headers.
class XrtBackend final : public KernelBackend {
public:
  explicit XrtBackend( const KernelInfo& kernel );
  ~XrtBackend() override;

  void                 open( const BackendConfig& cfg ) override;
  void                 setScalar( const ArgInfo& arg, const std::vector<uint8_t>& value ) override;
  std::vector<uint8_t> getOutput( const ArgInfo& arg ) override;
  void                 allocBuffer( const ArgInfo& arg, uint64_t bytes ) override;
  uint64_t             bufferSize( const ArgInfo& arg ) const override;
  void                 bufWrite( const ArgInfo& arg, uint64_t offset, const uint8_t* data, uint64_t len ) override;
  void                 bufRead( const ArgInfo& arg, uint64_t offset, uint8_t* data, uint64_t len ) override;
  void                 start() override;
  void                 wait() override;
  void                 writeReg( uint32_t offset, uint32_t value ) override;
  uint32_t             readReg( uint32_t offset ) override;

  /// Process-wide lock serializing XRT calls from all Firedrake kernels
  static std::recursive_mutex& xrtMutex();

private:
  struct Impl;
  std::unique_ptr<Impl> impl;
};

}  // namespace SST::Firedrake

#endif  // _SST_FIREDRAKE_XRTBACKEND_H_
