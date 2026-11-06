//
// _PicoRV32Host_cc_
//
// Copyright (C) 2026 Tactical Computing Laboratories, LLC
// All Rights Reserved
// contact@tactcomplabs.com
//
// See LICENSE.txt in the top level directory for licensing details
//

#include "PicoRV32Host.h"

// generated kernel class, for its typed accessors (header-only use)
#include "FiredrakePicorv32Kernel.h"

// memory map and reference shared with the firmware
#include "picorv32_test.h"

#include <fstream>
#include <iterator>

namespace SST::Firedrake {

namespace {

/// Data seed of the watchdog run, distinct from the complete runs
constexpr uint32_t TIMEOUT_SEED = 0xffffu;

void putWord( std::vector<uint8_t>& image, uint32_t addr, uint32_t value ) {
  std::memcpy( image.data() + addr, &value, sizeof( value ) );
}

uint32_t getWord( const std::vector<uint8_t>& data, uint32_t offset ) {
  uint32_t v;
  std::memcpy( &v, data.data() + offset, sizeof( v ) );
  return v;
}

}  // namespace

PicoRV32Host::PicoRV32Host( ComponentId_t id, Params& params ) : SST::Component( id ) {
  const uint32_t verbose = params.find<uint32_t>( "verbose", 0 );
  output.init( "PicoRV32Host[" + getName() + ":@p:@t]: ", verbose, 0, SST::Output::STDOUT );

  mode          = params.find<std::string>( "mode", "direct" );
  elements      = params.find<uint32_t>( "elements", 64 );
  runsWanted    = params.find<uint64_t>( "runs", 1 );
  maxCycles     = params.find<uint32_t>( "maxCycles", 100000000 );
  timeoutCycles = params.find<uint32_t>( "timeoutCycles", 0 );
  if( mode != "direct" && mode != "links" )
    output.fatal( CALL_INFO, -1, "mode must be direct or links\n" );
  if( elements == 0 || elements > PRV_MAX_N )
    output.fatal( CALL_INFO, -1, "elements must be 1..%u\n", PRV_MAX_N );
  if( runsWanted == 0 )
    output.fatal( CALL_INFO, -1, "runs must be at least 1\n" );
  if( mode == "links" && timeoutCycles )
    output.fatal( CALL_INFO, -1, "timeoutCycles needs direct mode (the model's max_cycles is fixed in links mode)\n" );

  const std::string path = params.find<std::string>( "program", "" );
  std::ifstream     in( path, std::ios::binary );
  if( path.empty() || !in )
    output.fatal( CALL_INFO, -1, "unable to read the firmware image '%s'\n", path.c_str() );
  program.assign( std::istreambuf_iterator<char>( in ), std::istreambuf_iterator<char>() );
  if( program.empty() || program.size() > PRV_CODE_LIMIT )
    output.fatal( CALL_INFO, -1, "firmware image '%s' is %zu bytes; it must be 1..%u\n", path.c_str(), program.size(), PRV_CODE_LIMIT );

  if( mode == "direct" ) {
    model = loadUserSubComponent<FiredrakeBase>( "model" );
    if( !model )
      output.fatal( CALL_INFO, -1, "direct mode requires the 'model' slot\n" );
    model->onDone( [this]( uint64_t runId, uint64_t cycles ) { kernelDone( runId, cycles ); } );
  } else {
    donePort = configureLink( "done", "1ps", new Event::Handler<PicoRV32Host, &PicoRV32Host::handleDone>( this ) );
    if( !donePort )
      output.fatal( CALL_INFO, -1, "links mode requires the 'done' port\n" );
  }
  memPort = configureLink( "mem", "1ps", new Event::Handler<PicoRV32Host, &PicoRV32Host::handleMem>( this ) );
  if( !memPort )
    output.fatal( CALL_INFO, -1, "the 'mem' port must be connected\n" );

  registerAsPrimaryComponent();
  primaryComponentDoNotEndSim();
}

void PicoRV32Host::setup() {
  if( model ) {
    auto* k = dynamic_cast<FiredrakePicorv32Kernel*>( model );
    check( k != nullptr, "model is a FiredrakePicorv32Kernel" );
    if( failed )
      return;
    check( model->getKernelInfo().protocol == CtrlProtocol::AP_CTRL_HS, "picorv32_kernel is ap_ctrl_hs" );
    check( model->getBufferSize( "mem" ) >= PRV_MEM_BYTES, "mem buffer covers the core address window" );
    check( model->readReg( CtrlReg::OFFSET ) & CtrlReg::AP_IDLE, "control register reports idle" );
  }
  beginRun();
}

void PicoRV32Host::finish() {
  if( !finished )
    output.output( "result=FAIL: picorv32 did not complete\n" );
}

void PicoRV32Host::check( bool ok, const char* what ) {
  if( ok ) {
    output.verbose( CALL_INFO, 1, 0, "ok: %s\n", what );
    return;
  }
  output.output( "result=FAIL: %s\n", what );
  failed   = true;
  finished = true;
  primaryComponentOKToEndSim();
}

void PicoRV32Host::pass() {
  if( failed )
    return;
  finished = true;
  output.output( "result=PASS: picorv32 (%s)\n", mode.c_str() );
  primaryComponentOKToEndSim();
}

// ---------------------------------------------------------------------------
// runs
// ---------------------------------------------------------------------------
void PicoRV32Host::beginRun() {
  if( failed )
    return;
  // firmware, n, a cleared result block and the inputs, as one write: in
  // links mode the FiredrakeComponent starts the kernel when it completes
  const uint32_t       seed = timeoutRun() ? TIMEOUT_SEED : uint32_t( run );
  std::vector<uint8_t> image( PRV_IN + elements * sizeof( uint32_t ), 0 );
  std::copy( program.begin(), program.end(), image.begin() );
  putWord( image, PRV_PARAMS, elements );
  for( uint32_t i = 0; i < elements; i++ )
    putWord( image, PRV_IN + i * uint32_t( sizeof( uint32_t ) ), prv_input( i, seed ) );
  runMaxCycles = timeoutRun() ? timeoutCycles : maxCycles;
  memPort->send( DataEvent::makeWrite( nextId++, 0, std::move( image ) ) );
}

void PicoRV32Host::handleMem( SST::Event* ev ) {
  auto* d = dynamic_cast<DataEvent*>( ev );
  check( d != nullptr, "mem port event is a DataEvent" );
  if( failed ) {
    delete ev;
    return;
  }
  switch( d->getAction() ) {
  case DataAction::ACK:
    // direct mode: the image is in place; set the watchdog and start
    if( model ) {
      auto* k = static_cast<FiredrakePicorv32Kernel*>( model );
      k->set_max_cycles( runMaxCycles );
      check( k->get_max_cycles() == runMaxCycles, "max_cycles typed accessor round trip" );
      check( model->start(), "start() accepted" );
      check( model->isRunning(), "kernel running after start()" );
      check( !model->start(), "second start() rejected while running" );
    }
    break;
  case DataAction::RESP: checkResult( d->getData() ); break;
  case DataAction::NACK: check( false, "mem buffer access NACKed" ); break;
  default: check( false, "unexpected request event at the host" ); break;
  }
  delete d;
}

void PicoRV32Host::handleDone( SST::Event* ev ) {
  auto* d = dynamic_cast<DoneEvent*>( ev );
  check( d != nullptr, "done port event is a DoneEvent" );
  if( d )
    kernelDone( d->getRunId(), d->getCycles() );
  delete ev;
}

void PicoRV32Host::kernelDone( uint64_t runId, uint64_t cycles ) {
  if( failed )
    return;
  check( runId == ++kernelRuns, "run ids are sequential" );
  check( cycles == runMaxCycles, "modeled cycles follow kernelCycles (1*max_cycles)" );
  if( model ) {
    auto* k = static_cast<FiredrakePicorv32Kernel*>( model );
    check( model->isDone() && !model->isRunning(), "kernel reports done" );
    hwCycles = k->get_cycles();
    hwStatus = k->get_status();
  }
  const uint32_t from = PRV_RESULT;
  const uint32_t to   = PRV_OUT + elements * uint32_t( sizeof( uint32_t ) );
  memPort->send( DataEvent::makeRead( nextId++, from, to - from ) );
}

void PicoRV32Host::checkResult( const std::vector<uint8_t>& data ) {
  check( data.size() == PRV_OUT + elements * sizeof( uint32_t ) - PRV_RESULT, "read response length" );
  if( failed )
    return;
  auto field = [&]( size_t member ) { return getWord( data, uint32_t( member ) ); };
  auto outAt = [&]( uint32_t i ) { return getWord( data, PRV_OUT - PRV_RESULT + i * uint32_t( sizeof( uint32_t ) ) ); };
  const uint32_t magic = field( offsetof( prv_result, magic ) );

  if( timeoutRun() ) {
    output.output( "watchdog run: status=%u cycles=%u\n", hwStatus, hwCycles );
    check( hwStatus == PRV_STATUS_TIMEOUT, "watchdog run reports status timeout" );
    check( hwCycles == timeoutCycles, "watchdog run stops after max_cycles cycles" );
    check( magic != PRV_MAGIC, "watchdog stopped the firmware before it finished" );
    timeoutDone = true;
    beginRun();
    return;
  }

  check( magic == PRV_MAGIC, "firmware wrote the result block" );
  check( field( offsetof( prv_result, n ) ) == elements, "firmware read n" );
  check( field( offsetof( prv_result, fib ) ) == prv_fib( PRV_FIB_N ), "fib result" );
  if( failed )
    return;
  uint32_t sum = 0;
  for( uint32_t i = 0; i < elements; i++ ) {
    const uint32_t want = prv_element( prv_input( i, uint32_t( run ) ), i );
    if( outAt( i ) != want ) {
      output.output( "out[%u] = 0x%08x, expected 0x%08x\n", i, outAt( i ), want );
      check( false, "out[] values" );
      return;
    }
    sum = prv_checksum( sum, want );
  }
  check( field( offsetof( prv_result, checksum ) ) == sum, "checksum" );

  const uint32_t fwCycles = field( offsetof( prv_result, cycles ) );
  const uint32_t instret  = field( offsetof( prv_result, instret ) );
  if( model ) {
    check( hwStatus == PRV_STATUS_TRAP, "run ends with the core trapped (ebreak)" );
    check( fwCycles > 0 && fwCycles < hwCycles, "firmware rdcycle within the kernel cycle count" );
    output.output( "run %" PRIu64 ": kernel cycles=%u firmware cycles=%u instret=%u CPI=%.2f\n",
                   run + 1, hwCycles, fwCycles, instret, instret ? double( fwCycles ) / instret : 0.0 );
  } else {
    output.output( "run %" PRIu64 ": firmware cycles=%u instret=%u\n", run + 1, fwCycles, instret );
  }
  output.verbose( CALL_INFO, 1, 0, "run %" PRIu64 " verified\n", run + 1 );
  run++;
  if( run == runsWanted )
    pass();
  else
    beginRun();
}

}  // namespace SST::Firedrake

// EOF
