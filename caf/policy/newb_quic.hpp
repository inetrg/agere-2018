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

#include <zconf.h>
#include "caf/io/network/default_multiplexer.hpp"
#include "caf/io/network/native_socket.hpp"
#include "caf/policy/accept.hpp"
#include "caf/policy/transport.hpp"
#include "MozQuic.h"
#include "../../src/mozquic_helper.hpp"
#include "../../src/mozquic_CB.h"

namespace caf {
namespace policy {

struct quic_transport : public transport {
  quic_transport(mozquic_connection_t* conn = nullptr);

  ~quic_transport() override {
    if (connection) {
      mozquic_shutdown_connection(connection);
      mozquic_destroy_connection(connection);
    }
  }

  io::network::rw_state read_some(io::newb_base* parent) override;

  bool should_deliver() override;

  void prepare_next_read(io::newb_base*) override;

  void configure_read(io::receive_policy::config config) override;

  io::network::rw_state write_some(io::newb_base* parent) override;

  void prepare_next_write(io::newb_base* parent) override;

  void flush(io::newb_base* parent) override;

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
template <class Message>
struct accept_quic : public accept<Message> {
  accept_quic() : accept<Message>(true) {
    connection = nullptr;
  };

  ~accept_quic() override {
    // destroy all pending connections
    if (connection) {
      mozquic_shutdown_connection(connection);
      mozquic_destroy_connection(connection);
    }
  }

  expected<io::network::native_socket>
  create_socket(uint16_t port, const char*, bool) override;

  void read_event(caf::io::newb_base*) {
    using namespace io::network;
    int i = 0;
    do {
      mozquic_IO(connection);
      usleep (1000); // this is for handleio todo
    } while(++i < 20);
  /*
    if(closure.new_connection) {
      int fd = mozquic_osfd(closure.new_connection);
      transport_ptr transport{new quic_transport{closure.new_connection}};
      auto en = create_newb(fd, std::move(transport));
      if (!en) {
        return;
      }
      auto ptr = caf::actor_cast<caf::abstract_actor*>(*en);
      CAF_ASSERT(ptr != nullptr);
      auto& ref = dynamic_cast<newb<message>&>(*ptr);
      init(ref);
      std::cout << "new connection accepted." << std::endl;
      closure.new_connection = nullptr;
    }*/
  }

  std::pair<io::network::native_socket, transport_ptr>
  accept_event(io::newb_base *) {
    using namespace io::network;
    int i = 0;
    do {
      mozquic_IO(connection);
      usleep(1000); // this is for handleio todo
    } while(++i < 20);

    if(closure.new_connection) {
      std::pair<native_socket, transport_ptr> ret(
              mozquic_osfd(closure.new_connection),
              new quic_transport{closure.new_connection}
      );
      closure.new_connection = nullptr;
      std::cout << "new connection accepted." << std::endl;
      return ret;
    }
    return {0, nullptr};
  }

  void init(io::newb_base& n) {
    n.start();
  }

/*
  expected<actor> create_newb(io::network::native_socket sockfd,
                              transport_ptr pol) override {
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
  }*/

  // connection state
  mozquic_connection_t* connection;
  server_closure closure;
};

template <class T>
struct quic_protocol
      : public protocol<typename T::message_type> {
  T impl;
  quic_protocol(io::newb<typename T::message_type>* parent)
          : impl(parent) {
    // nop
  }

  error read(char* bytes, size_t count) override {
    return impl.read(bytes, count);
  }

  error timeout(atom_value atm, uint32_t id) override {
    return impl.timeout(atm, id);
  }

  void write_header(byte_buffer& buf,
                    header_writer* hw) override {
    impl.write_header(buf, hw);
  }

  void prepare_for_sending(byte_buffer& buf, size_t hstart,
                           size_t offset, size_t plen) override {
    impl.prepare_for_sending(buf, hstart, offset, plen);
  }
};

} // namespace policy
} // namespace caf
