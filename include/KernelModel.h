//
// _KernelModel_h_
//
// Copyright (C) 2026 Tactical Computing Laboratories, LLC
// All Rights Reserved
// contact@tactcomplabs.com
//
// See LICENSE.txt in the top level directory for licensing details
//

#ifndef _SST_FIREDRAKE_KERNELMODEL_H_
#define _SST_FIREDRAKE_KERNELMODEL_H_

#include <cstdint>
#include <string>
#include <vector>

namespace SST::Firedrake {

/// ArgKind: how a kernel argument is reached from the host
enum class ArgKind : uint8_t {
  SCALAR = 0,  ///< s_axilite input register (addressQualifier=0)
  OUTPUT = 1,  ///< s_axilite output register (addressQualifier=0, pointer type)
  BUFFER = 2,  ///< m_axi global memory pointer (addressQualifier=1)
  STREAM = 3,  ///< axis stream (addressQualifier=4); not host visible
};

/// CtrlProtocol: the kernel's block-level control protocol (hwControlProtocol)
enum class CtrlProtocol : uint8_t {
  AP_CTRL_HS    = 0,
  AP_CTRL_CHAIN = 1,
  AP_CTRL_NONE  = 2,
  USER_MANAGED  = 3,
};

/// ArgInfo: one kernel argument, as described by the kernel's kernel.xml
struct ArgInfo {
  std::string name;      ///< argument name
  unsigned    index;     ///< argument index (kernel.xml id)
  ArgKind     kind;      ///< argument kind
  std::string hostType;  ///< C type as seen by the host (e.g. "unsigned int*")
  uint32_t    size;      ///< size of the s_axilite register(s) in bytes
  uint32_t    offset;    ///< s_axilite register offset in bytes
  std::string port;      ///< kernel interface port (e.g. M_AXI_GMEM0)
};

/// KernelInfo: the generator's normalized description of one kernel
struct KernelInfo {
  std::string          name;      ///< kernel (HLS top function) name
  CtrlProtocol         protocol;  ///< block-level control protocol
  std::vector<ArgInfo> args;      ///< arguments, ordered by index

  /// Returns the argument named `n`, or nullptr
  const ArgInfo* findArg( const std::string& n ) const {
    for( const auto& a : args )
      if( a.name == n )
        return &a;
    return nullptr;
  }

  /// Returns true if the kernel has a start/done handshake
  bool hasStart() const { return protocol == CtrlProtocol::AP_CTRL_HS || protocol == CtrlProtocol::AP_CTRL_CHAIN; }
};

/// Block-level control register (offset 0x00) bits shared by HLS kernels
namespace CtrlReg {
constexpr uint32_t OFFSET   = 0x00;
constexpr uint32_t AP_START = 0x1;
constexpr uint32_t AP_DONE  = 0x2;
constexpr uint32_t AP_IDLE  = 0x4;
constexpr uint32_t AP_READY = 0x8;
}  // namespace CtrlReg

}  // namespace SST::Firedrake

#endif  // _SST_FIREDRAKE_KERNELMODEL_H_
