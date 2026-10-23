//
// _LatencyModel_cc_
//
// Copyright (C) 2026 Tactical Computing Laboratories, LLC
// All Rights Reserved
// contact@tactcomplabs.com
//
// See LICENSE.txt in the top level directory for licensing details
//

#include "LatencyModel.h"
#include "SST.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>

namespace SST::Firedrake {

namespace {

std::string trim( const std::string& s ) {
  const auto b = s.find_first_not_of( " \t" );
  if( b == std::string::npos )
    return "";
  const auto e = s.find_last_not_of( " \t" );
  return s.substr( b, e - b + 1 );
}

std::vector<std::string> split( const std::string& s, char sep ) {
  std::vector<std::string> out;
  size_t                   start = 0;
  for( size_t pos; ( pos = s.find( sep, start ) ) != std::string::npos; start = pos + 1 )
    out.push_back( s.substr( start, pos - start ) );
  out.push_back( s.substr( start ) );
  return out;
}

/// Parses `text` as a UnitAlgebra carrying `units`; returns its SI value
double parseUnits( const std::string& what, const std::string& text, const std::string& units ) {
  try {
    UnitAlgebra ua( text );
    if( !ua.hasUnits( units ) )
      throw std::invalid_argument( "" );
    return ua.getValue().toDouble();
  } catch( const std::exception& ) {
    throw std::invalid_argument( what + " '" + text + "' must be expressed in " + units );
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// CycleExpr
// ---------------------------------------------------------------------------
CycleExpr::CycleExpr( const std::string& text ) {
  for( const auto& term : split( text, '+' ) ) {
    std::vector<Factor> product;
    for( const auto& raw : split( term, '*' ) ) {
      const std::string f = trim( raw );
      if( f.empty() )
        throw std::invalid_argument( "kernelCycles '" + text + "': empty term" );
      Factor factor;
      if( std::isdigit( static_cast<unsigned char>( f[0] ) ) ) {
        size_t used  = 0;
        factor.value = std::stoull( f, &used, 0 );
        if( used != f.size() )
          throw std::invalid_argument( "kernelCycles '" + text + "': bad constant '" + f + "'" );
      } else {
        const bool ok = std::all_of( f.begin(), f.end(), []( char c ) {
          return std::isalnum( static_cast<unsigned char>( c ) ) || c == '_';
        } );
        if( !ok )
          throw std::invalid_argument( "kernelCycles '" + text + "': bad identifier '" + f + "'" );
        factor.ident = f;
      }
      product.push_back( factor );
    }
    terms.push_back( std::move( product ) );
  }
}

uint64_t CycleExpr::eval( const Lookup& lookup ) const {
  uint64_t sum = 0;
  for( const auto& product : terms ) {
    uint64_t p = 1;
    for( const auto& f : product )
      p *= f.ident.empty() ? f.value : lookup( f.ident );
    sum += p;
  }
  return sum;
}

std::vector<std::string> CycleExpr::identifiers() const {
  std::vector<std::string> ids;
  for( const auto& product : terms )
    for( const auto& f : product )
      if( !f.ident.empty() && std::find( ids.begin(), ids.end(), f.ident ) == ids.end() )
        ids.push_back( f.ident );
  return ids;
}

// ---------------------------------------------------------------------------
// LatencyModel
// ---------------------------------------------------------------------------
LatencyModel::LatencyModel( const std::string& kernelClock,
                            const std::string& cycles,
                            const std::string& hostBW,
                            const std::string& hostLatency,
                            const std::string& startLatency ) :
  cycleExpr( cycles ) {
  const double freq = parseUnits( "kernelClock", kernelClock, "Hz" );
  const double bw   = parseUnits( "hostBW", hostBW, "B/s" );
  if( freq <= 0.0 || bw <= 0.0 )
    throw std::invalid_argument( "kernelClock and hostBW must be positive" );
  periodPs  = 1e12 / freq;
  psPerByte = 1e12 / bw;
  latencyPs = uint64_t( std::llround( parseUnits( "hostLatency", hostLatency, "s" ) * 1e12 ) );
  startPs   = uint64_t( std::llround( parseUnits( "startLatency", startLatency, "s" ) * 1e12 ) );
}

uint64_t LatencyModel::kernelPs( uint64_t cycles ) const {
  return startPs + uint64_t( std::llround( double( cycles ) * periodPs ) );
}

uint64_t LatencyModel::transferPs( uint64_t nowPs, uint64_t bytes ) {
  const uint64_t begin = std::max( nowPs, linkFreeAtPs );
  linkFreeAtPs         = begin + uint64_t( std::llround( double( bytes ) * psPerByte ) );
  return linkFreeAtPs + latencyPs - nowPs;
}

}  // namespace SST::Firedrake

// EOF
