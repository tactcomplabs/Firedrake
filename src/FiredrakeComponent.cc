//
// _FiredrakeComponent_cc_
//
// Copyright (C) 2026 Tactical Computing Laboratories, LLC
// All Rights Reserved
// contact@tactcomplabs.com
//
// See LICENSE.txt in the top level directory for licensing details
//

#include "FiredrakeComponent.h"

namespace SST::Firedrake {

FiredrakeComponent::FiredrakeComponent( ComponentId_t id, Params& params ) : SST::Component( id ) {
  const uint32_t verbose = params.find<uint32_t>( "verbose", 0 );
  output.init( "FiredrakeComponent[" + getName() + ":@p:@t]: ", verbose, 0, SST::Output::STDOUT );

  model = loadUserSubComponent<FiredrakeBase>( "model" );
  if( !model )
    output.fatal( CALL_INFO, -1, "the 'model' slot must be filled with a generated Firedrake kernel\n" );

  runsWanted = params.find<uint64_t>( "runs", 1 );
  std::vector<std::string> after;
  params.find_array<std::string>( "startAfter", after );
  for( const auto& a : after ) {
    const ArgInfo* arg = model->getKernelInfo().findArg( a );
    if( !arg || arg->kind != ArgKind::BUFFER )
      output.fatal( CALL_INFO, -1, "startAfter: '%s' is not a buffer argument of the model\n", a.c_str() );
    startAfter.insert( a );
  }

  model->onBufferWrite( [this]( const std::string& arg, uint64_t, uint64_t ) { bufferWritten( arg ); } );
  model->onDone( [this]( uint64_t runId, uint64_t cycles ) { kernelDone( runId, cycles ); } );

  if( runsWanted ) {
    registerAsPrimaryComponent();
    primaryComponentDoNotEndSim();
  }
}

void FiredrakeComponent::setup() {
  maybeStart();
}

void FiredrakeComponent::bufferWritten( const std::string& arg ) {
  if( startAfter.count( arg ) )
    written.insert( arg );
  maybeStart();
}

void FiredrakeComponent::maybeStart() {
  if( runsStarted >= runsWanted || model->isRunning() || written.size() != startAfter.size() )
    return;
  if( !model->start() )
    output.fatal( CALL_INFO, -1, "kernel '%s' could not be started\n", model->getKernelInfo().name.c_str() );
  ++runsStarted;
  written.clear();
}

void FiredrakeComponent::kernelDone( uint64_t runId, uint64_t cycles ) {
  output.verbose( CALL_INFO, 1, 0, "run %" PRIu64 " complete (%" PRIu64 " cycles)\n", runId, cycles );
  if( runId >= runsWanted ) {
    primaryComponentOKToEndSim();
    return;
  }
  maybeStart();
}

}  // namespace SST::Firedrake

// EOF
