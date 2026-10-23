//
// _FiredrakeKernel_cc_
//
// Copyright (C) 2026 Tactical Computing Laboratories, LLC
// All Rights Reserved
// contact@tactcomplabs.com
//
// See LICENSE.txt in the top level directory for licensing details
//

#include "FiredrakeKernel.h"

#include <algorithm>
#include <stdexcept>

namespace SST::Firedrake {

/// KernelTimerEvent: self-link event marking the modeled end of a run, or
/// of a buffer write (for write callbacks)
class KernelTimerEvent final : public SST::Event {
public:
  enum Kind : uint8_t { RUN_DONE = 0, WRITE_DONE = 1 };

  KernelTimerEvent() : SST::Event() {}

  KernelTimerEvent( Kind kind, uint64_t a, uint64_t b, uint64_t c = 0 ) : SST::Event(), kind( kind ), a( a ), b( b ), c( c ) {}

  Kind     kind = RUN_DONE;
  uint64_t a    = 0;  ///< RUN_DONE: run id;  WRITE_DONE: argument index
  uint64_t b    = 0;  ///< RUN_DONE: cycles;  WRITE_DONE: offset
  uint64_t c    = 0;  ///<                    WRITE_DONE: length

  void serialize_order( SST::Core::Serialization::serializer& ser ) override {
    Event::serialize_order( ser );
    SST_SER( kind );
    SST_SER( a );
    SST_SER( b );
    SST_SER( c );
  }

  ImplementSerializable( SST::Firedrake::KernelTimerEvent );
};

namespace {

/// Port and timer time base; all modeled delays are in picoseconds
constexpr const char* TIME_BASE = "1ps";

/// Encodes a parameter value as `size` little-endian bytes of `hostType`
std::vector<uint8_t> encodeScalar( const ArgInfo& arg, const std::string& text ) {
  std::vector<uint8_t> bytes( arg.size, 0 );
  if( arg.hostType == "float" ) {
    const float v = std::stof( text );
    std::memcpy( bytes.data(), &v, std::min<size_t>( sizeof( v ), bytes.size() ) );
  } else if( arg.hostType == "double" ) {
    const double v = std::stod( text );
    std::memcpy( bytes.data(), &v, std::min<size_t>( sizeof( v ), bytes.size() ) );
  } else {
    size_t         used = 0;
    const uint64_t v    = text.find( '-' ) == 0 ? uint64_t( std::stoll( text, &used, 0 ) ) : std::stoull( text, &used, 0 );
    if( used != text.size() )
      throw std::invalid_argument( text );
    std::memcpy( bytes.data(), &v, std::min<size_t>( sizeof( v ), bytes.size() ) );
  }
  return bytes;
}

}  // namespace

template<typename F>
auto FiredrakeKernel::guarded( F&& f ) -> decltype( f() ) {
  try {
    return f();
  } catch( const BackendError& e ) {
    output.fatal( CALL_INFO, -1, "%s\n", e.what() );
  }
  throw std::logic_error( "unreachable" );  // fatal() does not return
}

FiredrakeKernel::FiredrakeKernel(
  ComponentId_t id, Params& params, const KernelInfo& info, CsimInvoke invoke, const KernelDefaults& defaults ) :
  FiredrakeBase( id, params ), info( info ) {
  const uint32_t verbose = params.find<uint32_t>( "verbose", 0 );
  output.init( "Firedrake[" + getName() + ":@p:@t]: ", verbose, 0, SST::Output::STDOUT );

  // backend
  BackendConfig cfg;
  cfg.target      = params.find<std::string>( "backend", "csim" );
  cfg.xclbin      = params.find<std::string>( "xclbin", "" );
  cfg.emconfigDir = params.find<std::string>( "emconfigDir", "" );
  cfg.instance    = params.find<std::string>( "instance", "" );
  cfg.deviceIndex = params.find<unsigned>( "deviceIndex", 0 );
  guarded( [&] {
    backend = KernelBackend::create( info, cfg.target, invoke );
    backend->open( cfg );
  } );
  output.verbose( CALL_INFO, 1, 0, "kernel '%s' opened on backend '%s'\n", info.name.c_str(), cfg.target.c_str() );

  // timing
  std::string cycles = params.find<std::string>( "kernelCycles", "" );
  if( cycles.empty() )
    cycles = defaults.kernelCycles;
  try {
    latency = std::make_unique<LatencyModel>( params.find<std::string>( "kernelClock", "300MHz" ),
                                              cycles,
                                              params.find<std::string>( "hostBW", "12GB/s" ),
                                              params.find<std::string>( "hostLatency", "1us" ),
                                              params.find<std::string>( "startLatency", "0ns" ) );
  } catch( const std::exception& e ) {
    output.fatal( CALL_INFO, -1, "invalid timing parameters: %s\n", e.what() );
  }
  for( const auto& ident : latency->expr().identifiers() ) {
    const ArgInfo* a = info.findArg( ident );
    if( !a || a->kind != ArgKind::SCALAR )
      output.fatal( CALL_INFO, -1, "kernelCycles '%s' references '%s', which is not an input scalar\n", cycles.c_str(), ident.c_str() );
  }

  // scalars: zero, then arg.<name>
  for( const auto& a : info.args ) {
    if( a.kind != ArgKind::SCALAR )
      continue;
    std::vector<uint8_t> value( a.size, 0 );
    const std::string    text = params.find<std::string>( "arg." + a.name, "" );
    if( !text.empty() ) {
      try {
        value = encodeScalar( a, text );
      } catch( const std::exception& ) {
        output.fatal( CALL_INFO, -1, "arg.%s: cannot convert '%s' to %s\n", a.name.c_str(), text.c_str(), a.hostType.c_str() );
      }
    }
    scalars[a.index] = value;
    guarded( [&] { backend->setScalar( a, value ); } );
  }

  // buffers: bufferSize.<name> and one port per buffer
  for( const auto& a : info.args ) {
    if( a.kind == ArgKind::STREAM ) {
      output.verbose( CALL_INFO, 1, 0, "argument '%s' is an axis stream and has no host port\n", a.name.c_str() );
      continue;
    }
    if( a.kind != ArgKind::BUFFER )
      continue;
    const auto     def   = defaults.bufferSizes.find( a.name );
    const uint64_t bytes = params.find<uint64_t>( "bufferSize." + a.name, def == defaults.bufferSizes.end() ? 0 : def->second );
    if( bytes )
      allocBuffer( a.name, bytes );
    SST::Link* link =
      configureLink( a.name, TIME_BASE, new Event::Handler<FiredrakeKernel, &FiredrakeKernel::handleData, unsigned>( this, a.index ) );
    if( link )
      ports[a.index] = link;
    else
      output.verbose( CALL_INFO, 1, 0, "buffer port '%s' is not connected\n", a.name.c_str() );
  }
  donePort = configureLink( "done", TIME_BASE, new Event::Handler<FiredrakeKernel, &FiredrakeKernel::handleDone>( this ) );
  timer    = configureSelfLink( "timer", TIME_BASE, new Event::Handler<FiredrakeKernel, &FiredrakeKernel::handleTimed>( this ) );

  statWrites            = registerStatistic<uint64_t>( "buffer_writes" );
  statReads             = registerStatistic<uint64_t>( "buffer_reads" );
  statBytesWritten      = registerStatistic<uint64_t>( "bytes_written" );
  statBytesRead         = registerStatistic<uint64_t>( "bytes_read" );
  statReadsWhileRunning = registerStatistic<uint64_t>( "reads_while_running" );
  statRuns              = registerStatistic<uint64_t>( "kernel_runs" );
  statCycles            = registerStatistic<uint64_t>( "kernel_cycles" );
}

FiredrakeKernel::~FiredrakeKernel() = default;

const ArgInfo& FiredrakeKernel::requireArg( const std::string& arg, ArgKind kind ) const {
  const ArgInfo* a = info.findArg( arg );
  if( !a )
    output.fatal( CALL_INFO, -1, "kernel '%s' has no argument '%s'\n", info.name.c_str(), arg.c_str() );
  const bool ok = a->kind == kind || ( kind == ArgKind::SCALAR && a->kind == ArgKind::OUTPUT );
  if( !ok )
    output.fatal( CALL_INFO, -1, "argument '%s' of kernel '%s' has the wrong kind for this access\n", arg.c_str(), info.name.c_str() );
  return *a;
}

uint64_t FiredrakeKernel::nowPs() const {
  return getCurrentSimTime( TIME_BASE );
}

uint64_t FiredrakeKernel::scalarValue( const std::string& name ) const {
  const auto&    bytes = scalars.at( info.findArg( name )->index );
  uint64_t       v     = 0;
  std::memcpy( &v, bytes.data(), std::min<size_t>( sizeof( v ), bytes.size() ) );
  return v;
}

// ---------------------------------------------------------------------------
// control interface
// ---------------------------------------------------------------------------
void FiredrakeKernel::setScalar( const std::string& arg, const std::vector<uint8_t>& value ) {
  const ArgInfo& a = requireArg( arg, ArgKind::SCALAR );
  if( a.kind == ArgKind::OUTPUT )
    output.fatal( CALL_INFO, -1, "argument '%s' is an output register and cannot be set\n", arg.c_str() );
  if( running )
    output.verbose( CALL_INFO, 1, 0, "scalar '%s' set while the kernel is running\n", arg.c_str() );
  auto& shadow = scalars[a.index];
  std::fill( shadow.begin(), shadow.end(), 0 );
  std::copy_n( value.begin(), std::min( value.size(), shadow.size() ), shadow.begin() );
  guarded( [&] { backend->setScalar( a, shadow ); } );
}

std::vector<uint8_t> FiredrakeKernel::getScalar( const std::string& arg ) {
  const ArgInfo& a = requireArg( arg, ArgKind::SCALAR );
  if( a.kind == ArgKind::OUTPUT )
    return guarded( [&] { return backend->getOutput( a ); } );
  return scalars.at( a.index );
}

void FiredrakeKernel::writeReg( uint32_t offset, uint32_t value ) {
  if( info.hasStart() && offset == CtrlReg::OFFSET ) {
    if( value & CtrlReg::AP_START )
      start();
    return;
  }
  // keep the scalar shadow (used by the latency model) coherent
  for( const auto& a : info.args ) {
    if( a.kind != ArgKind::SCALAR || offset < a.offset || offset >= a.offset + a.size )
      continue;
    auto&        shadow = scalars[a.index];
    const size_t pos    = offset - a.offset;
    std::memcpy( shadow.data() + pos, &value, std::min<size_t>( sizeof( value ), shadow.size() - pos ) );
  }
  guarded( [&] { backend->writeReg( offset, value ); } );
}

uint32_t FiredrakeKernel::readReg( uint32_t offset ) {
  if( info.hasStart() && offset == CtrlReg::OFFSET )
    return running ? CtrlReg::AP_START : ( CtrlReg::AP_IDLE | ( runs ? CtrlReg::AP_DONE : 0 ) );
  return guarded( [&] { return backend->readReg( offset ); } );
}

bool FiredrakeKernel::start() {
  if( !info.hasStart() ) {
    output.verbose( CALL_INFO, 1, 0, "kernel '%s' is free running (ap_ctrl_none); start() ignored\n", info.name.c_str() );
    return false;
  }
  if( running )
    return false;
  const uint64_t cycles = latency->kernelCycles( [this]( const std::string& n ) { return scalarValue( n ); } );
  guarded( [&] { backend->start(); } );
  running            = true;
  const uint64_t run = runs + 1;
  output.verbose( CALL_INFO, 2, 0, "run %" PRIu64 " started: %" PRIu64 " cycles\n", run, cycles );
  timer->send( latency->kernelPs( cycles ), new KernelTimerEvent( KernelTimerEvent::RUN_DONE, run, cycles ) );
  return true;
}

void FiredrakeKernel::allocBuffer( const std::string& arg, uint64_t bytes ) {
  const ArgInfo& a = requireArg( arg, ArgKind::BUFFER );
  guarded( [&] { backend->allocBuffer( a, bytes ); } );
  output.verbose( CALL_INFO, 2, 0, "buffer '%s': %" PRIu64 " bytes\n", arg.c_str(), bytes );
}

uint64_t FiredrakeKernel::getBufferSize( const std::string& arg ) const {
  return backend->bufferSize( requireArg( arg, ArgKind::BUFFER ) );
}

// ---------------------------------------------------------------------------
// event handlers
// ---------------------------------------------------------------------------
void FiredrakeKernel::handleData( SST::Event* ev, unsigned argIndex ) {
  auto* req = dynamic_cast<DataEvent*>( ev );
  if( !req )
    output.fatal( CALL_INFO, -1, "buffer port received a non-DataEvent\n" );
  const ArgInfo* a = nullptr;
  for( const auto& arg : info.args )
    if( arg.index == argIndex )
      a = &arg;

  const uint64_t len     = req->getLength();
  bool           written = false;
  try {
    if( req->getAction() == DataAction::WRITE ) {
      statWrites->addData( 1 );
      statBytesWritten->addData( len );
      if( running )
        output.verbose( CALL_INFO, 1, 0, "buffer '%s' written while the kernel is running\n", a->name.c_str() );
      backend->bufWrite( *a, req->getOffset(), req->getData().data(), len );
      req->makeResponse( DataAction::ACK );
      written = true;
    } else if( req->getAction() == DataAction::READ ) {
      statReads->addData( 1 );
      statBytesRead->addData( len );
      if( running ) {
        statReadsWhileRunning->addData( 1 );
        output.verbose( CALL_INFO, 1, 0, "buffer '%s' read while the kernel is running\n", a->name.c_str() );
      }
      std::vector<uint8_t> data( len );
      backend->bufRead( *a, req->getOffset(), data.data(), len );
      req->makeResponse( DataAction::RESP, std::move( data ) );
    } else {
      output.fatal( CALL_INFO, -1, "buffer port '%s' received a response event\n", a->name.c_str() );
    }
  } catch( const BackendError& e ) {
    output.verbose( CALL_INFO, 1, 0, "NACK: %s\n", e.what() );
    req->makeResponse( DataAction::NACK );
  }
  output.verbose( CALL_INFO, 4, 0, "buffer '%s' id=%" PRIu64 " offset=%" PRIu64 " len=%" PRIu64 "\n", a->name.c_str(), req->getId(),
                  req->getOffset(), len );
  const uint64_t delay = latency->transferPs( nowPs(), len );
  if( written && !writeCallbacks.empty() )
    timer->send( delay, new KernelTimerEvent( KernelTimerEvent::WRITE_DONE, argIndex, req->getOffset(), len ) );
  ports.at( argIndex )->send( delay, req );
}

void FiredrakeKernel::handleDone( SST::Event* ev ) {
  output.verbose( CALL_INFO, 1, 0, "ignoring event received on the done port\n" );
  delete ev;
}

void FiredrakeKernel::handleTimed( SST::Event* ev ) {
  auto* t = static_cast<KernelTimerEvent*>( ev );
  if( t->kind == KernelTimerEvent::RUN_DONE ) {
    complete( t->a, t->b );
  } else {
    for( const auto& arg : info.args )
      if( arg.index == t->a )
        for( const auto& cb : writeCallbacks )
          cb( arg.name, t->b, t->c );
  }
  delete t;
}

void FiredrakeKernel::complete( uint64_t runId, uint64_t cycles ) {
  // the modeled completion time has arrived: make sure the backend is done
  guarded( [&] { backend->wait(); } );
  running = false;
  runs    = runId;
  statRuns->addData( 1 );
  statCycles->addData( cycles );
  output.verbose( CALL_INFO, 2, 0, "run %" PRIu64 " done\n", runId );
  for( const auto& cb : doneCallbacks )
    cb( runId, cycles );
  if( donePort )
    donePort->send( new DoneEvent( runId, cycles ) );
}

}  // namespace SST::Firedrake

// EOF
