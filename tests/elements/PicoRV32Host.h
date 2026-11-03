//
// _PicoRV32Host_h_
//
// Copyright (C) 2026 Tactical Computing Laboratories, LLC
// All Rights Reserved
// contact@tactcomplabs.com
//
// See LICENSE.txt in the top level directory for licensing details
//

#ifndef _SST_FIREDRAKE_PICORV32HOST_H_
#define _SST_FIREDRAKE_PICORV32HOST_H_

#include "FiredrakeAPI.h"

namespace SST::Firedrake {

/// PicoRV32Host: test host for the picorv32_kernel RTL kernel
/// (tests/kernels/picorv32).
///
/// Each run writes one image into the kernel's `mem` buffer: the firmware
/// at 0, n at PRV_PARAMS, a cleared struct prv_result and the inputs at
/// PRV_IN.  After the run it reads the result block and out[] back and
/// checks them against the reference in picorv32_test.h.
///
/// mode=direct: the kernel is loaded into the "model" slot and controlled
///              through the C++ API (set_max_cycles(), start(), onDone(),
///              get_cycles(), get_status()).  With timeoutCycles > 0 a
///              first run is stopped by the kernel's watchdog.
/// mode=links:  the kernel lives in a FiredrakeComponent that starts it
///              when the image write completes; this host waits for the
///              "done" port.
///
/// Prints result=PASS or result=FAIL.
class PicoRV32Host final : public SST::Component {
public:
  SST_ELI_REGISTER_COMPONENT( PicoRV32Host,
                              "firedraketest",
                              "PicoRV32Host",
                              SST_ELI_ELEMENT_VERSION( 1, 0, 0 ),
                              "Test host for the picorv32_kernel RTL kernel",
                              COMPONENT_CATEGORY_UNCATEGORIZED )

  SST_ELI_DOCUMENT_PARAMS(
    { "verbose", "Verbosity level", "0" },
    { "mode", "direct or links", "direct" },
    { "program", "Firmware image (raw binary, loaded at address 0)", "" },
    { "elements", "Number of 32-bit elements the firmware processes (at most 1024)", "64" },
    { "runs", "Number of complete runs", "1" },
    { "maxCycles", "max_cycles for complete runs (links: must match the model's arg.max_cycles)", "100000000" },
    { "timeoutCycles", "direct: max_cycles of an extra first run the watchdog must stop (0: no such run)", "0" }, )

  SST_ELI_DOCUMENT_PORTS(
    { "mem", "Connected to the kernel's mem buffer port", { "SST::Firedrake::DataEvent" } },
    { "done", "Kernel completion (links mode)", { "SST::Firedrake::DoneEvent" } }, )

  SST_ELI_DOCUMENT_SUBCOMPONENT_SLOTS( { "model", "picorv32_kernel (direct mode)", "SST::Firedrake::FiredrakeBase" }, )

  PicoRV32Host( ComponentId_t id, Params& params );

  void setup() override;
  void finish() override;

private:
  void beginRun();
  void handleMem( SST::Event* ev );
  void handleDone( SST::Event* ev );
  void kernelDone( uint64_t runId, uint64_t cycles );
  void checkResult( const std::vector<uint8_t>& data );

  void check( bool ok, const char* what );
  void pass();

  /// True while the watchdog run (the first run in direct mode) is active
  bool timeoutRun() const { return timeoutCycles && !timeoutDone; }

  SST::Output          output;
  FiredrakeBase*       model    = nullptr;
  SST::Link*           memPort  = nullptr;
  SST::Link*           donePort = nullptr;
  std::string          mode;
  std::vector<uint8_t> program;
  uint32_t             elements      = 0;
  uint64_t             runsWanted    = 1;
  uint64_t             run           = 0;  ///< complete runs verified
  uint64_t             kernelRuns    = 0;  ///< kernel runs finished, including the watchdog run
  uint32_t             maxCycles     = 0;
  uint32_t             timeoutCycles = 0;
  bool                 timeoutDone   = false;
  uint32_t             runMaxCycles  = 0;  ///< max_cycles of the current run
  uint32_t             hwCycles      = 0;  ///< direct: `cycles` output of the current run
  uint32_t             hwStatus      = 0;  ///< direct: `status` output of the current run
  uint64_t             nextId        = 1;
  bool                 failed        = false;
  bool                 finished      = false;
};

}  // namespace SST::Firedrake

#endif  // _SST_FIREDRAKE_PICORV32HOST_H_
