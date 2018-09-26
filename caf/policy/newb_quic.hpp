/******************************************************************************
 *                       ____    _    _____                                   *
 *                      / ___|  / \  |  ___|    C++                           *
 *                     | |     / _ \ | |_       Actor                         *
 *                     | |___ / ___ \|  _|      Framework                     *
 *                      \____/_/   \_|_|                                      *
 *                                                                            *
 * Copyright 2011-2018 Dominik Charousset                                     *
 *                                                                            *
 * Distributed under the terms and conditions of the BSD 3-Clause License or  *
 * (at your option) under the terms and conditions of the Boost Software      *
 * License 1.0. See accompanying files LICENSE and LICENSE_ALTERNATIVE.       *
 *                                                                            *
 * If you did not receive a copy of the license files, see                    *
 * http://opensource.org/licenses/BSD-3-Clause and                            *
 * http://www.boost.org/LICENSE_1_0.txt.                                      *
 ******************************************************************************/

#pragma once

#include "caf/io/newb.hpp"
#include "caf/io/network/native_socket.hpp"
#include "MozQuic.h"

namespace caf {
namespace policy {

struct client_closure {
  client_closure(io::network::byte_buffer& wr_buf, io::network::byte_buffer& rec_buf) :
    write_buffer(wr_buf),
    receive_buffer(rec_buf) {

  };

  bool is_server = false;
  bool connected = false;
  int amount_read = 0;
  io::network::byte_buffer& write_buffer;
  io::network::byte_buffer& receive_buffer;
};

struct server_closure {
    mozquic_connection_t* new_connection = nullptr;
};

struct quic_transport : public io::network::transport_policy {
  quic_transport(mozquic_connection_t* conn = nullptr);

  ~quic_transport() override {
    if (connection) {
      mozquic_shutdown_connection(connection);
      mozquic_destroy_connection(connection);
    }
  }

  io::network::rw_state read_some(io::network::newb_base* parent) override;

  bool should_deliver() override;

  void prepare_next_read(io::network::newb_base*) override;

  void configure_read(io::receive_policy::config config) override;

  io::network::rw_state write_some(io::network::newb_base* parent) override;

  void prepare_next_write(io::network::newb_base* parent) override;

  void flush(io::network::newb_base* parent) override;

  expected<io::network::native_socket>
  connect(const std::string& host, uint16_t port,
          optional<io::network::protocol::network> preferred = none) override;

  // State for reading.
  size_t read_threshold;
  size_t collected;
  size_t maximum;
  io::receive_policy_flag rd_flag;

  // State for writing.
  bool writing;
  size_t written;

  // connection state
  mozquic_connection_t* connection;
  client_closure closure;
};

// only server uses acceptors
struct accept_quic : public io::network::accept_policy {
  accept_quic() :
    io::network::accept_policy(false) // true if reading is handled manually
    {};

  ~accept_quic() override {
    // destroy all pending connections
    if (connection) {
      mozquic_shutdown_connection(connection);
      mozquic_destroy_connection(connection);
    }
  }

  expected<io::network::native_socket>
  create_socket(uint16_t port, const char* host, bool reuse = false) override;

  std::pair<io::network::native_socket, io::network::transport_policy_ptr>
    accept(io::network::newb_base* parent) override;

  /// If `manual_read` is set to true, the acceptor will only call
  /// this function for new read event and let the policy handle everything
  /// else.
  void read_event(io::network::newb_base*) override;

  void init(io::network::newb_base& n) override;

  expected<actor> create_newb(native_socket sockfd,
                              io::network::transport_policy_ptr pol) override {
    CAF_LOG_TRACE(CAF_ARG(sockfd));
    auto n = io::network::make_newb<raw_newb>(this->backend().system(), sockfd);
    auto ptr = caf::actor_cast<caf::abstract_actor*>(n);
    if (ptr == nullptr)
      return sec::runtime_error;
    auto& ref = dynamic_cast<raw_newb&>(*ptr);
    // TODO: Transport has to be assigned before protocol ... which sucks.
    //  (basp protocol calls configure read which accesses the transport.)
    ref.transport = std::move(pol);
    ref.protocol.reset(new ProtocolPolicy(&ref));
    ref.responder = responder;
    ref.configure_read(io::receive_policy::exactly(1000));
    // TODO: Just a workaround.
    anon_send(responder, n);
    return n;
  }

  // connection state
  mozquic_connection_t* connection;
  server_closure closure;
};

template <class T>
struct quic_protocol
      : public io::network::protocol_policy<typename T::message_type> {
  T impl;
  quic_protocol(io::network::newb<typename T::message_type>* parent)
          : impl(parent) {
    // nop
  }

  error read(char* bytes, size_t count) override {
    return impl.read(bytes, count);
  }

  error timeout(atom_value atm, uint32_t id) override {
    return impl.timeout(atm, id);
  }

  void write_header(io::network::byte_buffer& buf,
                    io::network::header_writer* hw) override {
    impl.write_header(buf, hw);
  }

  void prepare_for_sending(io::network::byte_buffer& buf, size_t hstart,
                           size_t offset, size_t plen) override {
    impl.prepare_for_sending(buf, hstart, offset, plen);
  }
};

} // namespace policy
} // namespace caf
