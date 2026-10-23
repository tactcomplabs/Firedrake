//
// _DeviceRegistry_h_
//
// Copyright (C) 2026 Tactical Computing Laboratories, LLC
// All Rights Reserved
// contact@tactcomplabs.com
//
// See LICENSE.txt in the top level directory for licensing details
//

#ifndef _SST_FIREDRAKE_DEVICEREGISTRY_H_
#define _SST_FIREDRAKE_DEVICEREGISTRY_H_

#include <xrt/xrt_device.h>
#include <xrt/xrt_uuid.h>

#if __has_include( <xrt/experimental/xrt_xclbin.h>)
#include <xrt/experimental/xrt_xclbin.h>
#else
#include <experimental/xrt_xclbin.h>
#endif

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace SST::Firedrake {

/// DeviceHandle: an opened XRT device with an xclbin loaded
struct DeviceHandle {
  xrt::device device;
  xrt::xclbin xclbin;  ///< parsed xclbin (connectivity for xrt::ip buffers)
  xrt::uuid   uuid;
};

/// DeviceRegistry: process-wide cache of opened devices, keyed by
/// (device index, xclbin path), so every Firedrake kernel that shares a
/// device and xclbin shares one xrt::device and one xclbin load.
class DeviceRegistry {
public:
  static DeviceRegistry& instance();

  /// Opens (or reuses) device `index` with `xclbin` loaded
  std::shared_ptr<DeviceHandle> acquire( unsigned index, const std::string& xclbin );

private:
  DeviceRegistry() = default;

  std::mutex                                                             lock;
  std::map<std::pair<unsigned, std::string>, std::shared_ptr<DeviceHandle>> devices;
};

}  // namespace SST::Firedrake

#endif  // _SST_FIREDRAKE_DEVICEREGISTRY_H_
