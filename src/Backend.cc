//
// _Backend_cc_
//
// Copyright (C) 2026 Tactical Computing Laboratories, LLC
// All Rights Reserved
// contact@tactcomplabs.com
//
// See LICENSE.txt in the top level directory for licensing details
//

#include "Backend.h"
#include "CsimBackend.h"

#ifdef FIREDRAKE_HAVE_XRT
#include "XrtBackend.h"
#endif

namespace SST::Firedrake {

bool KernelBackend::haveXrt() {
#ifdef FIREDRAKE_HAVE_XRT
  return true;
#else
  return false;
#endif
}

std::unique_ptr<KernelBackend> KernelBackend::create( const KernelInfo& kernel, const std::string& target, CsimInvoke invoke ) {
  if( target == "csim" )
    return std::make_unique<CsimBackend>( kernel, invoke );
  if( target == "sw_emu" || target == "hw_emu" || target == "hw" ) {
#ifdef FIREDRAKE_HAVE_XRT
    return std::make_unique<XrtBackend>( kernel );
#else
    throw BackendError( "backend '" + target + "' requires XRT, but Firedrake was built without it (set XILINX_XRT and "
                                               "reconfigure)" );
#endif
  }
  throw BackendError( "unknown backend '" + target + "' (expected csim, sw_emu, hw_emu or hw)" );
}

}  // namespace SST::Firedrake

// EOF
