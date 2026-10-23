//
// _FiredrakeEvent_h_
//
// Copyright (C) 2026 Tactical Computing Laboratories, LLC
// All Rights Reserved
// contact@tactcomplabs.com
//
// See LICENSE.txt in the top level directory for licensing details
//

#ifndef _SST_FIREDRAKE_FIREDRAKEEVENT_H_
#define _SST_FIREDRAKE_FIREDRAKEEVENT_H_

#include "SST.h"

#include <cstdint>
#include <string>
#include <vector>

namespace SST::Firedrake {

/// DataAction: the operation carried by a DataEvent
enum class DataAction : uint8_t {
  WRITE = 0,  ///< host -> device: write `data` at `offset` in the buffer
  READ  = 1,  ///< device -> host: read `length` bytes at `offset`
  ACK   = 2,  ///< response to a WRITE
  RESP  = 3,  ///< response to a READ, carrying `data`
  NACK  = 4,  ///< error response (out of range, unallocated buffer)
};

/// DataEvent: host data movement on a kernel buffer (m_axi argument) port.
/// Requests (WRITE/READ) flow to the kernel; responses (ACK/RESP/NACK) flow
/// back on the same link and echo the request id, offset and length.
class DataEvent final : public SST::Event {
public:
  DataEvent() : SST::Event() {}

  DataEvent( DataAction action, uint64_t id, uint64_t offset, uint64_t length, std::vector<uint8_t> data = {} ) :
    SST::Event(), action( action ), id( id ), offset( offset ), length( length ), data( std::move( data ) ) {}

  /// Builds a WRITE request
  static DataEvent* makeWrite( uint64_t id, uint64_t offset, std::vector<uint8_t> data ) {
    const uint64_t len = data.size();
    return new DataEvent( DataAction::WRITE, id, offset, len, std::move( data ) );
  }

  /// Builds a READ request
  static DataEvent* makeRead( uint64_t id, uint64_t offset, uint64_t length ) {
    return new DataEvent( DataAction::READ, id, offset, length );
  }

  DataAction                  getAction() const { return action; }
  uint64_t                    getId() const { return id; }
  uint64_t                    getOffset() const { return offset; }
  uint64_t                    getLength() const { return length; }
  const std::vector<uint8_t>& getData() const { return data; }
  std::vector<uint8_t>&       getData() { return data; }

  /// Converts this request into its response in place
  void makeResponse( DataAction resp, std::vector<uint8_t> payload = {} ) {
    action = resp;
    data   = std::move( payload );
  }

  void serialize_order( SST::Core::Serialization::serializer& ser ) override {
    Event::serialize_order( ser );
    SST_SER( action );
    SST_SER( id );
    SST_SER( offset );
    SST_SER( length );
    SST_SER( data );
  }

private:
  DataAction           action = DataAction::READ;
  uint64_t             id     = 0;
  uint64_t             offset = 0;
  uint64_t             length = 0;
  std::vector<uint8_t> data;

  ImplementSerializable( SST::Firedrake::DataEvent );
};

/// DoneEvent: sent on a kernel's "done" port when a run completes
class DoneEvent final : public SST::Event {
public:
  DoneEvent() : SST::Event() {}

  DoneEvent( uint64_t runId, uint64_t cycles ) : SST::Event(), runId( runId ), cycles( cycles ) {}

  uint64_t getRunId() const { return runId; }
  uint64_t getCycles() const { return cycles; }

  void serialize_order( SST::Core::Serialization::serializer& ser ) override {
    Event::serialize_order( ser );
    SST_SER( runId );
    SST_SER( cycles );
  }

private:
  uint64_t runId  = 0;
  uint64_t cycles = 0;

  ImplementSerializable( SST::Firedrake::DoneEvent );
};

}  // namespace SST::Firedrake

#endif  // _SST_FIREDRAKE_FIREDRAKEEVENT_H_
