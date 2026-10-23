//
// _Backend_h_
//
// Copyright (C) 2026 Tactical Computing Laboratories, LLC
// All Rights Reserved
// contact@tactcomplabs.com
//
// See LICENSE.txt in the top level directory for licensing details
//

#ifndef _SST_FIREDRAKE_BACKEND_H_
#define _SST_FIREDRAKE_BACKEND_H_

#include "KernelModel.h"

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace SST::Firedrake {

/// BackendError: thrown by backends; the SST wrapper turns it into a fatal
class BackendError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

/// BackendConfig: how to reach the kernel
struct BackendConfig {
  std::string target;           ///< csim | sw_emu | hw_emu | hw
  std::string xclbin;           ///< xclbin path (XRT targets)
  std::string emconfigDir;      ///< directory holding emconfig.json (emulation targets)
  std::string instance;         ///< compute unit name, e.g. "vadd:{vadd_1}" (default: kernel name)
  unsigned    deviceIndex = 0;  ///< XRT device index
};

/// CsimArgs: argument storage handed to a generated csim trampoline
struct CsimArgs {
  std::vector<void*> ptrs;  ///< per argument index: buffer, scalar or output storage

  /// Returns argument `idx` as a buffer pointer of type T
  template<typename T>
  T buffer( unsigned idx ) const {
    return static_cast<T>( ptrs.at( idx ) );
  }

  /// Returns argument `idx` as a scalar of type T
  template<typename T>
  T scalar( unsigned idx ) const {
    return *static_cast<const T*>( ptrs.at( idx ) );
  }
};

/// CsimInvoke: generated function that calls the native HLS top function
using CsimInvoke = void ( * )( CsimArgs& );

/// KernelBackend: functional access to one kernel instance.
///
/// Backends are purely functional; all timing is applied by the SST wrapper
/// (FiredrakeKernel) through the LatencyModel.  Buffer transfers include the
/// host<->device synchronization of the transferred range.
class KernelBackend {
public:
  explicit KernelBackend( const KernelInfo& kernel ) : kernel( kernel ) {}

  virtual ~KernelBackend() = default;

  KernelBackend( const KernelBackend& )            = delete;
  KernelBackend& operator=( const KernelBackend& ) = delete;

  /// Opens the device / kernel
  virtual void open( const BackendConfig& cfg ) = 0;

  /// Sets an input scalar; `value` holds arg.size little-endian bytes
  virtual void setScalar( const ArgInfo& arg, const std::vector<uint8_t>& value ) = 0;

  /// Reads an output scalar (ArgKind::OUTPUT)
  virtual std::vector<uint8_t> getOutput( const ArgInfo& arg ) = 0;

  /// (Re)allocates a device buffer for a BUFFER argument; contents are zeroed
  virtual void allocBuffer( const ArgInfo& arg, uint64_t bytes ) = 0;

  /// Returns the allocated size of a buffer, 0 if unallocated
  virtual uint64_t bufferSize( const ArgInfo& arg ) const = 0;

  /// Copies host data into a buffer and syncs the range to the device
  virtual void bufWrite( const ArgInfo& arg, uint64_t offset, const uint8_t* data, uint64_t len ) = 0;

  /// Syncs a buffer range from the device and copies it to host memory
  virtual void bufRead( const ArgInfo& arg, uint64_t offset, uint8_t* data, uint64_t len ) = 0;

  /// Starts one kernel run (ap_ctrl_hs / ap_ctrl_chain)
  virtual void start() = 0;

  /// Blocks until the run started by start() completes
  virtual void wait() = 0;

  /// Raw s_axilite register access
  virtual void     writeReg( uint32_t offset, uint32_t value ) = 0;
  virtual uint32_t readReg( uint32_t offset )                  = 0;

  /// Creates the backend for `cfg.target`; throws if it is unavailable
  static std::unique_ptr<KernelBackend> create( const KernelInfo& kernel, const std::string& target, CsimInvoke invoke );

  /// Returns true if this build includes the XRT backend
  static bool haveXrt();

protected:
  const KernelInfo& kernel;
};

}  // namespace SST::Firedrake

#endif  // _SST_FIREDRAKE_BACKEND_H_
