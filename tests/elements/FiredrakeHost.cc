//
// _FiredrakeHost_cc_
//
// Copyright (C) 2026 Tactical Computing Laboratories, LLC
// All Rights Reserved
// contact@tactcomplabs.com
//
// See LICENSE.txt in the top level directory for licensing details
//

#include "FiredrakeHost.h"

// generated kernel classes, for their typed accessors (header-only use)
#include "FiredrakeRegfile.h"
#include "FiredrakeVadd.h"

namespace SST::Firedrake {

namespace {

constexpr unsigned PORT_IN1 = 0;
constexpr unsigned PORT_IN2 = 1;
constexpr unsigned PORT_OUT = 2;
constexpr unsigned NUM_PORTS = 3;

/// vadd cycle model used by tests/kernels/vadd/vadd.firedrake.json
constexpr uint64_t VADD_FIXED_CYCLES = 70;

uint32_t in1Value( uint64_t i, uint64_t run ) {
  return uint32_t( i * 3 + run );
}

uint32_t in2Value( uint64_t i, uint64_t run ) {
  return uint32_t( 0xFFFF0000u + i * 7 + run * 11 );
}

std::vector<uint8_t> pack( const std::vector<uint32_t>& words ) {
  std::vector<uint8_t> bytes( words.size() * sizeof( uint32_t ) );
  std::memcpy( bytes.data(), words.data(), bytes.size() );
  return bytes;
}

}  // namespace

FiredrakeHost::FiredrakeHost( ComponentId_t id, Params& params ) : SST::Component( id ) {
  const uint32_t verbose = params.find<uint32_t>( "verbose", 0 );
  output.init( "FiredrakeHost[" + getName() + ":@p:@t]: ", verbose, 0, SST::Output::STDOUT );

  mode           = params.find<std::string>( "mode", "direct" );
  scenario       = params.find<std::string>( "scenario", "vadd" );
  elements       = params.find<uint64_t>( "elements", 256 );
  runsWanted     = params.find<uint64_t>( "runs", 1 );
  expectKernelPs = params.find<uint64_t>( "expectKernelPs", 0 );
  expectDonePs   = params.find<uint64_t>( "expectDonePs", 0 );
  if( mode != "direct" && mode != "links" )
    output.fatal( CALL_INFO, -1, "mode must be direct or links\n" );
  if( scenario != "vadd" && scenario != "regfile" )
    output.fatal( CALL_INFO, -1, "scenario must be vadd or regfile\n" );

  if( mode == "direct" ) {
    model = loadUserSubComponent<FiredrakeBase>( "model" );
    if( !model )
      output.fatal( CALL_INFO, -1, "direct mode requires the 'model' slot\n" );
    model->onDone( [this]( uint64_t runId, uint64_t cycles ) { kernelDone( runId, cycles ); } );
  } else {
    donePort = configureLink( "done", "1ps", new Event::Handler<FiredrakeHost, &FiredrakeHost::handleDone>( this ) );
    if( !donePort )
      output.fatal( CALL_INFO, -1, "links mode requires the 'done' port\n" );
  }

  if( scenario == "vadd" ) {
    for( unsigned p = 0; p < NUM_PORTS; p++ ) {
      SST::Link* l = configureLink(
        "port" + std::to_string( p ), "1ps", new Event::Handler<FiredrakeHost, &FiredrakeHost::handlePort, unsigned>( this, p ) );
      if( !l )
        output.fatal( CALL_INFO, -1, "vadd requires port0..port2\n" );
      ports[p] = l;
    }
  }

  registerAsPrimaryComponent();
  primaryComponentDoNotEndSim();
}

void FiredrakeHost::setup() {
  if( scenario == "regfile" )
    runRegfile();
  else
    beginRun();
}

void FiredrakeHost::finish() {
  if( !finished )
    output.output( "result=FAIL: %s did not complete\n", scenario.c_str() );
}

void FiredrakeHost::check( bool ok, const char* what ) {
  if( ok ) {
    output.verbose( CALL_INFO, 1, 0, "ok: %s\n", what );
    return;
  }
  output.output( "result=FAIL: %s\n", what );
  failed   = true;
  finished = true;
  primaryComponentOKToEndSim();
}

void FiredrakeHost::pass() {
  if( failed )
    return;
  finished = true;
  output.output( "result=PASS: %s (%s)\n", scenario.c_str(), mode.c_str() );
  primaryComponentOKToEndSim();
}

// ---------------------------------------------------------------------------
// vadd
// ---------------------------------------------------------------------------
void FiredrakeHost::beginRun() {
  if( failed )
    return;
  const uint64_t bytes = elements * sizeof( uint32_t );

  if( model ) {
    auto* vadd = dynamic_cast<FiredrakeVadd*>( model );
    check( vadd != nullptr, "model is a FiredrakeVadd" );
    if( failed )
      return;
    const ArgInfo* size = model->getKernelInfo().findArg( "size" );
    vadd->set_size( int( elements ) );
    check( vadd->get_size() == int( elements ), "typed accessor round trip" );
    check( model->readReg( size->offset ) == elements, "size register at its kernel.xml offset" );
    check( model->readReg( CtrlReg::OFFSET ) & CtrlReg::AP_IDLE, "control register reports idle" );
    check( model->getBufferSize( "in1" ) >= bytes, "in1 buffer is large enough" );
  }

  std::vector<uint32_t> a( elements ), b( elements );
  for( uint64_t i = 0; i < elements; i++ ) {
    a[i] = in1Value( i, run );
    b[i] = in2Value( i, run );
  }
  pendingAcks = 2;
  ports[PORT_IN1]->send( DataEvent::makeWrite( nextId++, 0, pack( a ) ) );
  ports[PORT_IN2]->send( DataEvent::makeWrite( nextId++, 0, pack( b ) ) );
  if( run == 0 && model ) {
    // an out-of-range write must be rejected (direct mode only, so the
    // links-mode host link schedule holds just the two input writes)
    pendingAcks++;
    ports[PORT_OUT]->send( DataEvent::makeWrite( nextId++, uint64_t( 1 ) << 40, { 1, 2, 3, 4 } ) );
  }
}

void FiredrakeHost::handlePort( SST::Event* ev, unsigned port ) {
  auto* d = dynamic_cast<DataEvent*>( ev );
  check( d != nullptr, "port event is a DataEvent" );
  if( failed ) {
    delete ev;
    return;
  }
  bool acked = false;
  switch( d->getAction() ) {
  case DataAction::ACK:
    check( port != PORT_OUT, "only input buffers are written" );
    acked = true;
    break;
  case DataAction::NACK:
    check( port == PORT_OUT && run == 0 && !sawNack, "out-of-range write is NACKed" );
    sawNack = true;
    acked   = true;
    break;
  case DataAction::RESP:
    check( port == PORT_OUT, "read response on the out port" );
    checkResult( d->getData() );
    break;
  default: check( false, "unexpected request event at the host" ); break;
  }
  delete d;

  // direct mode: start once every write of this run has been acknowledged;
  // in links mode FiredrakeComponent starts the kernel by itself
  if( acked && --pendingAcks == 0 && model && !failed ) {
    startPs = nowPs();
    check( model->start(), "start() accepted" );
    check( model->isRunning(), "kernel running after start()" );
    check( !model->start(), "second start() rejected while running" );
    check( model->readReg( CtrlReg::OFFSET ) & CtrlReg::AP_START, "control register reports running" );
  }
}

void FiredrakeHost::handleDone( SST::Event* ev ) {
  auto* d = dynamic_cast<DoneEvent*>( ev );
  check( d != nullptr, "done port event is a DoneEvent" );
  if( d ) {
    if( run == 0 && expectDonePs )
      check( nowPs() == expectDonePs, "first done event at the modeled time" );
    kernelDone( d->getRunId(), d->getCycles() );
  }
  delete ev;
}

void FiredrakeHost::kernelDone( uint64_t runId, uint64_t cycles ) {
  if( failed )
    return;
  output.verbose( CALL_INFO, 1, 0, "run %" PRIu64 " done after %" PRIu64 " cycles\n", runId, cycles );
  check( runId == run + 1, "run ids are sequential" );
  check( cycles == VADD_FIXED_CYCLES + elements, "modeled cycles follow kernelCycles" );
  if( model ) {
    check( model->isDone() && !model->isRunning(), "kernel reports done" );
    if( expectKernelPs ) {
      if( nowPs() - startPs != expectKernelPs )
        output.output( "kernel time %" PRIu64 " ps, expected %" PRIu64 " ps\n", nowPs() - startPs, expectKernelPs );
      check( nowPs() - startPs == expectKernelPs, "kernel time follows kernelClock" );
    }
  }
  readResult();
}

void FiredrakeHost::readResult() {
  if( failed )
    return;
  ports[PORT_OUT]->send( DataEvent::makeRead( nextId++, 0, elements * sizeof( uint32_t ) ) );
}

void FiredrakeHost::checkResult( const std::vector<uint8_t>& data ) {
  check( data.size() == elements * sizeof( uint32_t ), "read response length" );
  if( failed )
    return;
  for( uint64_t i = 0; i < elements; i++ ) {
    uint32_t v;
    std::memcpy( &v, data.data() + i * sizeof( uint32_t ), sizeof( v ) );
    const uint32_t want = in1Value( i, run ) + in2Value( i, run );
    if( v != want ) {
      output.output( "out[%" PRIu64 "] = 0x%08x, expected 0x%08x\n", i, v, want );
      check( false, "vadd result" );
      return;
    }
  }
  output.verbose( CALL_INFO, 1, 0, "run %" PRIu64 " verified\n", run + 1 );
  run++;
  if( run == runsWanted ) {
    check( sawNack || !model, "out-of-range write was NACKed" );
    pass();
  } else {
    beginRun();
  }
}

// ---------------------------------------------------------------------------
// regfile
// ---------------------------------------------------------------------------
void FiredrakeHost::runRegfile() {
  check( model != nullptr, "regfile runs in direct mode" );
  if( failed )
    return;
  auto* rf = dynamic_cast<FiredrakeRegfile*>( model );
  check( rf != nullptr, "model is a FiredrakeRegfile" );
  if( failed )
    return;

  const KernelInfo& info = model->getKernelInfo();
  check( info.protocol == CtrlProtocol::AP_CTRL_NONE, "regfile is ap_ctrl_none" );
  check( !model->start(), "start() is rejected for a free-running kernel" );

  // typed accessors
  rf->set_a( 7 );
  rf->set_b( -3 );
  rf->set_mask( 0xF0u );
  check( rf->get_sum() == 4, "sum = a + b" );
  check( rf->get_prod() == -21, "prod = a * b" );
  check( rf->get_masked() == ( 7u & 0xF0u ), "masked = a & mask" );

  // raw registers at their kernel.xml offsets
  const uint32_t offA   = info.findArg( "a" )->offset;
  const uint32_t offSum = info.findArg( "sum" )->offset;
  model->writeReg( offA, 0x55 );
  check( model->readReg( offA ) == 0x55, "register write/read round trip" );
  check( model->readReg( offSum ) == 0x52, "output register tracks a raw register write" );
  check( rf->get_a() == 0x55, "scalar shadow follows raw register writes" );
  check( rf->get_masked() == ( 0x55u & 0xF0u ), "masked follows a" );

  // generic byte interface
  model->setScalar( "b", { 0x10, 0, 0, 0 } );
  check( model->getScalarAs<int>( "sum" ) == 0x65, "generic setScalar()/getScalarAs()" );
  pass();
}

}  // namespace SST::Firedrake

// EOF
