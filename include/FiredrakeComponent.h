//
// _FiredrakeComponent_h_
//
// Copyright (C) 2026 Tactical Computing Laboratories, LLC
// All Rights Reserved
// contact@tactcomplabs.com
//
// See LICENSE.txt in the top level directory for licensing details
//

#ifndef _SST_FIREDRAKE_FIREDRAKECOMPONENT_H_
#define _SST_FIREDRAKE_FIREDRAKECOMPONENT_H_

#include "FiredrakeAPI.h"

#include <set>

namespace SST::Firedrake {

/// FiredrakeComponent: a standalone shell around one Firedrake kernel.
///
/// Use it when the kernel's data ports are driven by other components over
/// links (connected directly to the model's buffer ports) and no C++
/// controller is needed: scalars come from the model's arg.<name>
/// parameters, and the kernel is started `runs` times, each time once every
/// buffer listed in `startAfter` has completed a write since the previous
/// start (or immediately when `startAfter` is empty).  The simulation may
/// end after the last run completes.
class FiredrakeComponent final : public SST::Component {
public:
  SST_ELI_REGISTER_COMPONENT( FiredrakeComponent,
                              "firedrake",
                              "FiredrakeComponent",
                              SST_ELI_ELEMENT_VERSION( 1, 0, 0 ),
                              "Standalone shell for a Firedrake (Vitis/XRT) kernel",
                              COMPONENT_CATEGORY_PROCESSOR )

  SST_ELI_DOCUMENT_PARAMS(
    { "verbose", "Verbosity level", "0" },
    { "runs", "Number of kernel runs before the component allows the simulation to end (0: never start)", "1" },
    { "startAfter", "Buffer arguments that must be written before each start, e.g. [in1, in2]", "[]" }, )

  SST_ELI_DOCUMENT_SUBCOMPONENT_SLOTS( { "model", "Generated Firedrake kernel", "SST::Firedrake::FiredrakeBase" }, )

  FiredrakeComponent( ComponentId_t id, Params& params );

  void setup() override;

private:
  void bufferWritten( const std::string& arg );
  void kernelDone( uint64_t runId, uint64_t cycles );
  void maybeStart();

  SST::Output           output;
  FiredrakeBase*        model = nullptr;
  std::set<std::string> startAfter;
  std::set<std::string> written;
  uint64_t              runsWanted = 1;
  uint64_t              runsStarted = 0;
};

}  // namespace SST::Firedrake

#endif  // _SST_FIREDRAKE_FIREDRAKECOMPONENT_H_
