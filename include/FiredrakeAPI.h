//
// _FiredrakeAPI_h_
//
// Copyright (C) 2026 Tactical Computing Laboratories, LLC
// All Rights Reserved
// contact@tactcomplabs.com
//
// See LICENSE.txt in the top level directory for licensing details
//

#ifndef _SST_FIREDRAKE_FIREDRAKEAPI_H_
#define _SST_FIREDRAKE_FIREDRAKEAPI_H_

#include "FiredrakeEvent.h"
#include "KernelModel.h"
#include "SST.h"

#include <cstring>
#include <functional>
#include <string>
#include <type_traits>
#include <vector>

namespace SST::Firedrake {

/// FiredrakeBase: the SubComponent API implemented by every generated
/// Firedrake kernel.
///
/// Data interfaces (m_axi buffer arguments) are SST ports carrying
/// DataEvent; completion is also reported on the "done" port as a
/// DoneEvent.  The control interface (s_axilite scalars, raw registers and
/// the start/done handshake) is this C++ API, used by the component that
/// loads the kernel into one of its slots.  Generated kernels additionally
/// provide typed accessors (set_<arg>() / get_<arg>()).
class FiredrakeBase : public SST::SubComponent {
public:
  SST_ELI_REGISTER_SUBCOMPONENT_API( SST::Firedrake::FiredrakeBase )

  SST_ELI_DOCUMENT_PARAMS(
    { "verbose", "Verbosity level", "0" },
    { "backend", "Kernel backend: csim, sw_emu, hw_emu or hw", "csim" },
    { "xclbin", "xclbin path (sw_emu, hw_emu, hw)", "" },
    { "emconfigDir", "Directory holding emconfig.json (sw_emu, hw_emu)", "" },
    { "deviceIndex", "XRT device index", "0" },
    { "instance", "Compute unit name, e.g. vadd:{vadd_1} (default: the kernel name)", "" },
    { "kernelClock", "Kernel clock frequency", "300MHz" },
    { "kernelCycles", "Kernel cycles per run: sum of products of constants and scalar args, e.g. '64 + 1*size'", "" },
    { "startLatency", "Delay between start() and the first kernel cycle", "0ns" },
    { "hostBW", "Host<->device link bandwidth used for buffer transfers", "12GB/s" },
    { "hostLatency", "Host<->device link latency per buffer transfer", "1us" }, )

  SST_ELI_DOCUMENT_STATISTICS(
    { "buffer_writes", "Buffer write requests received", "requests", 1 },
    { "buffer_reads", "Buffer read requests received", "requests", 1 },
    { "bytes_written", "Bytes written into kernel buffers", "bytes", 1 },
    { "bytes_read", "Bytes read from kernel buffers", "bytes", 1 },
    { "reads_while_running", "Buffer reads received while the kernel was running", "requests", 1 },
    { "kernel_runs", "Completed kernel runs", "runs", 1 },
    { "kernel_cycles", "Modeled kernel cycles per run", "cycles", 1 }, )

  FiredrakeBase( ComponentId_t id, Params& params ) : SubComponent( id ) {}

  ~FiredrakeBase() override = default;

  /// Called with the run id and modeled cycle count when a run completes
  using DoneCallback = std::function<void( uint64_t runId, uint64_t cycles )>;

  /// Called with the buffer name, offset and length when a buffer write
  /// arriving on a port completes (at the time its ACK is delivered)
  using WriteCallback = std::function<void( const std::string& arg, uint64_t offset, uint64_t len )>;

  /// Returns the kernel's argument description
  virtual const KernelInfo& getKernelInfo() const = 0;

  // -- s_axilite control interface ------------------------------------------

  /// Sets input scalar `arg` from little-endian bytes
  virtual void setScalar( const std::string& arg, const std::vector<uint8_t>& value ) = 0;

  /// Returns the bytes of scalar `arg` (inputs: last value set; outputs:
  /// the value read from the kernel)
  virtual std::vector<uint8_t> getScalar( const std::string& arg ) = 0;

  /// Raw s_axilite register access.  Writing AP_START to the control
  /// register is equivalent to start(); reading it reflects the modeled
  /// run state.
  virtual void     writeReg( uint32_t offset, uint32_t value ) = 0;
  virtual uint32_t readReg( uint32_t offset )                  = 0;

  /// Starts a run; returns false if the kernel is running or has no
  /// start/done handshake (ap_ctrl_none)
  virtual bool start() = 0;

  /// Returns true from start() until the modeled completion time
  virtual bool isRunning() const = 0;

  /// Returns true once at least one run has completed and none is running
  virtual bool isDone() const = 0;

  /// Registers a completion callback (called in addition to the done port)
  virtual void onDone( DoneCallback cb ) = 0;

  /// Registers a buffer-write completion callback
  virtual void onBufferWrite( WriteCallback cb ) = 0;

  // -- buffer management -------------------------------------------------------

  /// (Re)allocates the device buffer for m_axi argument `arg`; also set by
  /// the bufferSize.<arg> parameter
  virtual void allocBuffer( const std::string& arg, uint64_t bytes ) = 0;

  /// Returns the allocated size of buffer `arg`
  virtual uint64_t getBufferSize( const std::string& arg ) const = 0;

  // -- typed helpers ---------------------------------------------------------

  template<typename T>
  void setScalarAs( const std::string& arg, T value ) {
    static_assert( std::is_trivially_copyable_v<T> );
    std::vector<uint8_t> bytes( sizeof( T ) );
    std::memcpy( bytes.data(), &value, sizeof( T ) );
    setScalar( arg, bytes );
  }

  template<typename T>
  T getScalarAs( const std::string& arg ) {
    static_assert( std::is_trivially_copyable_v<T> );
    T    value{};
    auto bytes = getScalar( arg );
    std::memcpy( &value, bytes.data(), std::min( sizeof( T ), bytes.size() ) );
    return value;
  }
};

}  // namespace SST::Firedrake

#endif  // _SST_FIREDRAKE_FIREDRAKEAPI_H_
