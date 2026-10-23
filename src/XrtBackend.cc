//
// _XrtBackend_cc_
//
// Copyright (C) 2026 Tactical Computing Laboratories, LLC
// All Rights Reserved
// contact@tactcomplabs.com
//
// See LICENSE.txt in the top level directory for licensing details
//
// Compiled only when FIREDRAKE_ENABLE_XRT is set (XRT found at configure time)
//

#include "XrtBackend.h"
#include "DeviceRegistry.h"

#include <xrt/xrt_bo.h>
#include <xrt/xrt_kernel.h>

#if __has_include( <xrt/experimental/xrt_ip.h>)
#include <xrt/experimental/xrt_ip.h>
#else
#include <experimental/xrt_ip.h>
#endif

#include <cstdlib>
#include <cstring>
#include <map>
#include <optional>

namespace SST::Firedrake {

// ---------------------------------------------------------------------------
// DeviceRegistry
// ---------------------------------------------------------------------------
DeviceRegistry& DeviceRegistry::instance() {
  static DeviceRegistry reg;
  return reg;
}

std::shared_ptr<DeviceHandle> DeviceRegistry::acquire( unsigned index, const std::string& xclbin ) {
  std::lock_guard<std::mutex> g( lock );
  auto&                       slot = devices[{ index, xclbin }];
  if( !slot ) {
    auto h    = std::make_shared<DeviceHandle>();
    h->device = xrt::device( index );
    h->xclbin = xrt::xclbin( xclbin );
    h->uuid   = h->device.load_xclbin( h->xclbin );
    slot      = h;
  }
  return slot;
}

// ---------------------------------------------------------------------------
// XrtBackend
// ---------------------------------------------------------------------------
struct XrtBackend::Impl {
  std::shared_ptr<DeviceHandle> dev;
  std::string                   instance;
  std::optional<xrt::kernel>    kernel;  ///< ap_ctrl_hs / ap_ctrl_chain
  std::optional<xrt::run>       run;
  std::optional<xrt::ip>        ip;  ///< ap_ctrl_none / user managed
  std::map<unsigned, xrt::bo>   bos;
  std::map<unsigned, uint64_t>  sizes;
};

namespace {

/// Sets an emulation environment variable once per process; XRT reads these
/// when the first device is opened, so conflicting values are an error
void setProcessEnv( const char* name, const std::string& value ) {
  const char* cur = std::getenv( name );
  if( cur && value != cur )
    throw BackendError( std::string( name ) + " is already '" + cur + "'; all Firedrake kernels in a process must use the same "
                                              "XRT target" );
  if( !cur && setenv( name, value.c_str(), 1 ) != 0 )
    throw BackendError( std::string( "unable to set " ) + name );
}

template<typename T>
T asScalar( const std::vector<uint8_t>& value ) {
  T v{};
  std::memcpy( &v, value.data(), std::min( sizeof( T ), value.size() ) );
  return v;
}

}  // namespace

std::recursive_mutex& XrtBackend::xrtMutex() {
  static std::recursive_mutex m;
  return m;
}

XrtBackend::XrtBackend( const KernelInfo& kernel ) : KernelBackend( kernel ), impl( std::make_unique<Impl>() ) {}

XrtBackend::~XrtBackend() {
  std::lock_guard<std::recursive_mutex> g( xrtMutex() );
  impl.reset();
}

void XrtBackend::open( const BackendConfig& cfg ) {
  std::lock_guard<std::recursive_mutex> g( xrtMutex() );
  if( cfg.xclbin.empty() )
    throw BackendError( "the '" + cfg.target + "' backend requires the 'xclbin' parameter" );
  if( cfg.target == "sw_emu" || cfg.target == "hw_emu" ) {
    setProcessEnv( "XCL_EMULATION_MODE", cfg.target );
    if( !cfg.emconfigDir.empty() )
      setProcessEnv( "EMCONFIG_PATH", cfg.emconfigDir );
  } else if( std::getenv( "XCL_EMULATION_MODE" ) ) {
    throw BackendError( "XCL_EMULATION_MODE is set but the backend target is 'hw'" );
  }

  try {
    impl->dev      = DeviceRegistry::instance().acquire( cfg.deviceIndex, cfg.xclbin );
    impl->instance = cfg.instance.empty() ? kernel.name : cfg.instance;
    if( kernel.hasStart() ) {
      impl->kernel.emplace( impl->dev->device, impl->dev->uuid, impl->instance, xrt::kernel::cu_access_mode::exclusive );
      impl->run.emplace( *impl->kernel );
    } else {
      std::string name = impl->instance;
      if( name.find( ':' ) == std::string::npos )
        name += ":{" + name + "_1}";
      impl->instance = name;
      impl->ip.emplace( impl->dev->device, impl->dev->uuid, name );
    }
  } catch( const std::exception& e ) {
    throw BackendError( "XRT: unable to open kernel '" + kernel.name + "' from '" + cfg.xclbin + "': " + e.what() );
  }
}

void XrtBackend::setScalar( const ArgInfo& arg, const std::vector<uint8_t>& value ) {
  std::lock_guard<std::recursive_mutex> g( xrtMutex() );
  if( impl->run ) {
    switch( arg.size ) {
    case 1: impl->run->set_arg( int( arg.index ), asScalar<uint8_t>( value ) ); break;
    case 2: impl->run->set_arg( int( arg.index ), asScalar<uint16_t>( value ) ); break;
    case 4: impl->run->set_arg( int( arg.index ), asScalar<uint32_t>( value ) ); break;
    case 8: impl->run->set_arg( int( arg.index ), asScalar<uint64_t>( value ) ); break;
    default: throw BackendError( "XRT: scalar '" + arg.name + "' has unsupported size " + std::to_string( arg.size ) );
    }
    return;
  }
  // xrt::ip: write the register image 32 bits at a time
  for( uint32_t pos = 0; pos < arg.size; pos += 4 ) {
    uint32_t word = 0;
    if( pos < value.size() )
      std::memcpy( &word, value.data() + pos, std::min<size_t>( 4, value.size() - pos ) );
    impl->ip->write_register( arg.offset + pos, word );
  }
}

std::vector<uint8_t> XrtBackend::getOutput( const ArgInfo& arg ) {
  std::vector<uint8_t> out( arg.size, 0 );
  for( uint32_t pos = 0; pos < arg.size; pos += 4 ) {
    const uint32_t word = readReg( arg.offset + pos );
    std::memcpy( out.data() + pos, &word, std::min<size_t>( 4, arg.size - pos ) );
  }
  return out;
}

void XrtBackend::allocBuffer( const ArgInfo& arg, uint64_t bytes ) {
  std::lock_guard<std::recursive_mutex> g( xrtMutex() );
  try {
    xrt::memory_group grp;
    if( impl->kernel ) {
      grp = xrt::memory_group( impl->kernel->group_id( int( arg.index ) ) );
    } else {
      // xrt::ip has no group_id(); derive the bank from the xclbin connectivity
      auto ipInfo = impl->dev->xclbin.get_ip( impl->instance );
      auto mems   = ipInfo.get_arg( int32_t( arg.index ) ).get_mems();
      if( mems.empty() )
        throw BackendError( "no memory bank connected" );
      grp = xrt::memory_group( mems.front().get_index() );
    }
    xrt::bo bo( impl->dev->device, bytes, grp );
    std::vector<uint8_t> zero( bytes, 0 );
    bo.write( zero.data(), bytes, 0 );
    bo.sync( XCL_BO_SYNC_BO_TO_DEVICE );
    if( impl->run ) {
      impl->run->set_arg( int( arg.index ), bo );
    } else {
      const uint64_t addr = bo.address();
      impl->ip->write_register( arg.offset, uint32_t( addr ) );
      impl->ip->write_register( arg.offset + 4, uint32_t( addr >> 32 ) );
    }
    impl->bos.insert_or_assign( arg.index, bo );
    impl->sizes[arg.index] = bytes;
  } catch( const std::exception& e ) {
    throw BackendError( "XRT: unable to allocate buffer '" + arg.name + "': " + e.what() );
  }
}

uint64_t XrtBackend::bufferSize( const ArgInfo& arg ) const {
  auto it = impl->sizes.find( arg.index );
  return it == impl->sizes.end() ? 0 : it->second;
}

void XrtBackend::bufWrite( const ArgInfo& arg, uint64_t offset, const uint8_t* data, uint64_t len ) {
  std::lock_guard<std::recursive_mutex> g( xrtMutex() );
  const uint64_t                        size = bufferSize( arg );
  if( offset > size || len > size - offset )
    throw BackendError( "buffer '" + arg.name + "' write exceeds its " + std::to_string( size ) + " byte allocation" );
  if( !len )
    return;
  auto& bo = impl->bos.at( arg.index );
  bo.write( data, len, offset );
  bo.sync( XCL_BO_SYNC_BO_TO_DEVICE, len, offset );
}

void XrtBackend::bufRead( const ArgInfo& arg, uint64_t offset, uint8_t* data, uint64_t len ) {
  std::lock_guard<std::recursive_mutex> g( xrtMutex() );
  const uint64_t                        size = bufferSize( arg );
  if( offset > size || len > size - offset )
    throw BackendError( "buffer '" + arg.name + "' read exceeds its " + std::to_string( size ) + " byte allocation" );
  if( !len )
    return;
  auto& bo = impl->bos.at( arg.index );
  bo.sync( XCL_BO_SYNC_BO_FROM_DEVICE, len, offset );
  bo.read( data, len, offset );
}

void XrtBackend::start() {
  std::lock_guard<std::recursive_mutex> g( xrtMutex() );
  if( !impl->run )
    throw BackendError( "kernel '" + kernel.name + "' has no start/done handshake" );
  impl->run->start();
}

void XrtBackend::wait() {
  // not under xrtMutex: other kernels may issue XRT calls while this one runs
  if( !impl->run )
    return;
  const auto state = impl->run->wait();
  if( state != ERT_CMD_STATE_COMPLETED )
    throw BackendError( "XRT: kernel '" + kernel.name + "' run ended in state " + std::to_string( int( state ) ) );
}

void XrtBackend::writeReg( uint32_t offset, uint32_t value ) {
  std::lock_guard<std::recursive_mutex> g( xrtMutex() );
  if( impl->ip )
    impl->ip->write_register( offset, value );
  else
    impl->kernel->write_register( offset, value );
}

uint32_t XrtBackend::readReg( uint32_t offset ) {
  std::lock_guard<std::recursive_mutex> g( xrtMutex() );
  return impl->ip ? impl->ip->read_register( offset ) : impl->kernel->read_register( offset );
}

}  // namespace SST::Firedrake

// EOF
