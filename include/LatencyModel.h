//
// _LatencyModel_h_
//
// Copyright (C) 2026 Tactical Computing Laboratories, LLC
// All Rights Reserved
// contact@tactcomplabs.com
//
// See LICENSE.txt in the top level directory for licensing details
//

#ifndef _SST_FIREDRAKE_LATENCYMODEL_H_
#define _SST_FIREDRAKE_LATENCYMODEL_H_

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace SST::Firedrake {

/// CycleExpr: a kernel cycle count as a sum of products of integer
/// constants and scalar argument names, e.g. "64 + 2*size" or "rows*cols".
class CycleExpr {
public:
  /// Resolves an identifier (scalar argument name) to its current value
  using Lookup = std::function<uint64_t( const std::string& )>;

  /// Parses `text`; throws std::invalid_argument on a syntax error
  explicit CycleExpr( const std::string& text = "0" );

  /// Evaluates the expression
  uint64_t eval( const Lookup& lookup ) const;

  /// Returns the identifiers referenced by the expression
  std::vector<std::string> identifiers() const;

private:
  struct Factor {
    uint64_t    value = 1;
    std::string ident;  ///< empty for a constant
  };

  std::vector<std::vector<Factor>> terms;  ///< sum of products
};

/// LatencyModel: deterministic timing for a kernel and its host link.
///
/// All durations are returned in picoseconds.  Host transfers share one
/// host<->device link: each transfer occupies the link for bytes/hostBW and
/// completes hostLatency after it leaves the link.
class LatencyModel {
public:
  /// `kernelClock` (e.g. "300MHz"), `hostBW` (e.g. "12GB/s"),
  /// `hostLatency` and `startLatency` (e.g. "1us"); throws
  /// std::invalid_argument on bad units
  LatencyModel( const std::string& kernelClock,
                const std::string& cycles,
                const std::string& hostBW,
                const std::string& hostLatency,
                const std::string& startLatency );

  /// Returns the kernel cycle count for the current scalar values
  uint64_t kernelCycles( const CycleExpr::Lookup& lookup ) const { return cycleExpr.eval( lookup ); }

  /// Returns the duration of `cycles` kernel cycles plus the start latency
  uint64_t kernelPs( uint64_t cycles ) const;

  /// Schedules a `bytes` transfer at time `nowPs` on the host link and
  /// returns its completion delay relative to `nowPs`
  uint64_t transferPs( uint64_t nowPs, uint64_t bytes );

  const CycleExpr& expr() const { return cycleExpr; }

private:
  CycleExpr cycleExpr;
  double    periodPs      = 0.0;  ///< kernel clock period
  double    psPerByte     = 0.0;  ///< host link occupancy per byte
  uint64_t  latencyPs     = 0;    ///< host link latency
  uint64_t  startPs       = 0;    ///< kernel start overhead
  uint64_t  linkFreeAtPs  = 0;    ///< time the host link becomes idle
};

}  // namespace SST::Firedrake

#endif  // _SST_FIREDRAKE_LATENCYMODEL_H_
