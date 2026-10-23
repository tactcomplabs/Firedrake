//
// _FiredrakeHost_h_
//
// Copyright (C) 2026 Tactical Computing Laboratories, LLC
// All Rights Reserved
// contact@tactcomplabs.com
//
// See LICENSE.txt in the top level directory for licensing details
//

#ifndef _SST_FIREDRAKE_FIREDRAKEHOST_H_
#define _SST_FIREDRAKE_FIREDRAKEHOST_H_

#include "FiredrakeAPI.h"

#include <map>

namespace SST::Firedrake {

/// FiredrakeHost: test host for Firedrake kernels.
///
/// mode=direct: the kernel is loaded into the "model" slot and controlled
///              through the C++ API (typed accessors, start(), onDone());
///              its buffers are reached through port%d links.
/// mode=links:  the kernel lives in a FiredrakeComponent; this host only
///              moves data over port%d and waits for the "done" port.
///
/// Scenarios: "vadd" (port0=in1, port1=in2, port2=out) and "regfile"
/// (direct only).  Prints result=PASS or result=FAIL.
class FiredrakeHost final : public SST::Component {
public:
  SST_ELI_REGISTER_COMPONENT( FiredrakeHost,
                              "firedraketest",
                              "FiredrakeHost",
                              SST_ELI_ELEMENT_VERSION( 1, 0, 0 ),
                              "Test host for Firedrake kernels",
                              COMPONENT_CATEGORY_UNCATEGORIZED )

  SST_ELI_DOCUMENT_PARAMS(
    { "verbose", "Verbosity level", "0" },
    { "mode", "direct or links", "direct" },
    { "scenario", "vadd or regfile", "vadd" },
    { "elements", "vadd: number of 32-bit elements", "256" },
    { "runs", "vadd: number of kernel runs", "1" },
    { "expectKernelPs", "vadd: expected start-to-done time in ps (0: unchecked)", "0" },
    { "expectDonePs", "vadd links: expected absolute time of the first done event in ps (0: unchecked)", "0" }, )

  SST_ELI_DOCUMENT_PORTS(
    { "port%(ports)d", "Buffer ports, connected to the kernel's buffer ports", { "SST::Firedrake::DataEvent" } },
    { "done", "Kernel completion (links mode)", { "SST::Firedrake::DoneEvent" } }, )

  SST_ELI_DOCUMENT_SUBCOMPONENT_SLOTS( { "model", "Firedrake kernel (direct mode)", "SST::Firedrake::FiredrakeBase" }, )

  FiredrakeHost( ComponentId_t id, Params& params );

  void setup() override;
  void finish() override;

private:
  // vadd
  void beginRun();
  void handlePort( SST::Event* ev, unsigned port );
  void handleDone( SST::Event* ev );
  void kernelDone( uint64_t runId, uint64_t cycles );
  void readResult();
  void checkResult( const std::vector<uint8_t>& data );

  // regfile
  void runRegfile();

  void     check( bool ok, const char* what );
  void     pass();
  uint64_t nowPs() const { return getCurrentSimTime( "1ps" ); }

  SST::Output                    output;
  FiredrakeBase*                 model = nullptr;
  std::map<unsigned, SST::Link*> ports;
  SST::Link*                     donePort = nullptr;
  std::string                    mode;
  std::string                    scenario;
  uint64_t                       elements       = 0;
  uint64_t                       runsWanted     = 1;
  uint64_t                       run            = 0;
  uint64_t                       expectKernelPs = 0;
  uint64_t                       expectDonePs   = 0;
  uint64_t                       pendingAcks    = 0;
  uint64_t                       startPs        = 0;
  uint64_t                       nextId         = 1;
  bool                           sawNack        = false;
  bool                           failed         = false;
  bool                           finished       = false;
};

}  // namespace SST::Firedrake

#endif  // _SST_FIREDRAKE_FIREDRAKEHOST_H_
