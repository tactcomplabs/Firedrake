//
// _FiredrakeKernel_h_
//
// Copyright (C) 2026 Tactical Computing Laboratories, LLC
// All Rights Reserved
// contact@tactcomplabs.com
//
// See LICENSE.txt in the top level directory for licensing details
//

#ifndef _SST_FIREDRAKE_FIREDRAKEKERNEL_H_
#define _SST_FIREDRAKE_FIREDRAKEKERNEL_H_

#include "Backend.h"
#include "FiredrakeAPI.h"
#include "LatencyModel.h"

#include <map>
#include <memory>

namespace SST::Firedrake {

/// KernelDefaults: per-kernel defaults baked in by the generator
struct KernelDefaults {
  std::string                     kernelCycles = "1";  ///< default kernelCycles expression
  std::map<std::string, uint64_t> bufferSizes;         ///< default bufferSize.<arg> values
};

/// FiredrakeKernel: the kernel-independent implementation of FiredrakeBase.
///
/// A generated kernel class derives from this, supplies its KernelInfo and
/// csim trampoline, and adds its ELI registration and typed accessors.  This
/// class configures one port per buffer argument (named after the argument)
/// plus "done", applies the bufferSize.<arg> / arg.<name> parameters, and
/// applies the LatencyModel: backends execute immediately, and completion
/// and transfer responses are delivered at the modeled times.
class FiredrakeKernel : public FiredrakeBase {
public:
  FiredrakeKernel( ComponentId_t id, Params& params, const KernelInfo& info, CsimInvoke invoke, const KernelDefaults& defaults );

  ~FiredrakeKernel() override;

  const KernelInfo&    getKernelInfo() const final { return info; }
  void                 setScalar( const std::string& arg, const std::vector<uint8_t>& value ) final;
  std::vector<uint8_t> getScalar( const std::string& arg ) final;
  void                 writeReg( uint32_t offset, uint32_t value ) final;
  uint32_t             readReg( uint32_t offset ) final;
  bool                 start() final;
  bool                 isRunning() const final { return running; }
  bool                 isDone() const final { return !running && runs > 0; }
  void                 onDone( DoneCallback cb ) final { doneCallbacks.push_back( std::move( cb ) ); }
  void                 onBufferWrite( WriteCallback cb ) final { writeCallbacks.push_back( std::move( cb ) ); }
  void                 allocBuffer( const std::string& arg, uint64_t bytes ) final;
  uint64_t             getBufferSize( const std::string& arg ) const final;

protected:
  SST::Output output;

private:
  const ArgInfo& requireArg( const std::string& arg, ArgKind kind ) const;
  void           handleData( SST::Event* ev, unsigned argIndex );
  void           handleDone( SST::Event* ev );
  void           handleTimed( SST::Event* ev );
  void           complete( uint64_t runId, uint64_t cycles );
  uint64_t       nowPs() const;
  uint64_t       scalarValue( const std::string& name ) const;

  /// Runs a backend call, converting BackendError into a fatal
  template<typename F>
  auto guarded( F&& f ) -> decltype( f() );

  const KernelInfo&                      info;
  std::unique_ptr<KernelBackend>         backend;
  std::unique_ptr<LatencyModel>          latency;
  std::map<unsigned, SST::Link*>         ports;    ///< argument index -> buffer port
  SST::Link*                             donePort = nullptr;
  SST::Link*                             timer    = nullptr;
  std::map<unsigned, std::vector<uint8_t>> scalars;  ///< input scalar shadow values
  std::vector<DoneCallback>              doneCallbacks;
  std::vector<WriteCallback>             writeCallbacks;
  bool                                   running = false;
  uint64_t                               runs    = 0;

  Statistic<uint64_t>* statWrites;
  Statistic<uint64_t>* statReads;
  Statistic<uint64_t>* statBytesWritten;
  Statistic<uint64_t>* statBytesRead;
  Statistic<uint64_t>* statReadsWhileRunning;
  Statistic<uint64_t>* statRuns;
  Statistic<uint64_t>* statCycles;
};

}  // namespace SST::Firedrake

#endif  // _SST_FIREDRAKE_FIREDRAKEKERNEL_H_
