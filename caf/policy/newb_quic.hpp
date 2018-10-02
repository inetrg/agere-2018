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
#include "caf/io/newb.hpp"
#include "caf/config.hpp"
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
private:
  // connection state
  mozquic_connection_t* connection;
  client_closure closure;

public:
  explicit quic_transport(mozquic_connection_t* conn = nullptr);

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
          optional<io::network::protocol::network>) override;

  // State for reading.
  size_t read_threshold;
  size_t collected;
  size_t maximum;
  io::receive_policy_flag rd_flag;

  // State for writing.
  bool writing;
  size_t written;
};

// only server uses acceptors
template <class Message>
struct accept_quic : public accept<Message> {
private:
  // connection state
  mozquic_connection_t* connection;
  server_closure closure;

public:
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
  create_socket(uint16_t port, const char*, bool) {
    // check for nss_config
    char nss_config[] = "/home/jakob/CLionProjects/measuring-newbs/nss-config/";
    if (mozquic_nss_config(const_cast<char*>(nss_config)) != MOZQUIC_OK) {
      std::cerr << "nss-config failure" << std::endl;
      return io::network::invalid_native_socket;
    }

    mozquic_config_t config;
    memset(&config, 0, sizeof(mozquic_config_t));
    config.originName = "foo.example.com";
    config.originPort = port;
    config.handleIO = 0;
    config.appHandlesLogging = 0;
    config.ipv6 = 1;
    CHECK_MOZQUIC_ERR(mozquic_unstable_api1(&config, "tolerateBadALPN", 1,
                                            nullptr), "setup-bad_ALPN");
    CHECK_MOZQUIC_ERR(mozquic_unstable_api1(&config, "tolerateNoTransportParams",
                                            1, nullptr), "setup-noTransport");
    CHECK_MOZQUIC_ERR(mozquic_unstable_api1(&config, "sabotageVN", 0, nullptr),
                      "setup-sabotage");
    CHECK_MOZQUIC_ERR(mozquic_unstable_api1(&config, "forceAddressValidation", 0,
                                            nullptr), "setup-addrValidation->0");
    CHECK_MOZQUIC_ERR(mozquic_unstable_api1(&config, "streamWindow", 4906,
                                            nullptr), "setup-streamWindow");
    CHECK_MOZQUIC_ERR(mozquic_unstable_api1(&config, "connWindow", 8192, nullptr),
                      "setup-connWindow");
    CHECK_MOZQUIC_ERR(mozquic_unstable_api1(&config, "enable0RTT", 1, nullptr),
                      "setup-0rtt");

    // setting up the connection
    CHECK_MOZQUIC_ERR(mozquic_new_connection(&connection, &config),
                      "setup-new_conn_ip6");
    CHECK_MOZQUIC_ERR(mozquic_set_event_callback(connection, connEventCB),
                      "setup-event_cb_ip6");
    CHECK_MOZQUIC_ERR(mozquic_set_event_callback_closure(connection, &closure),
                      "setup-event_cb_closure");
    CHECK_MOZQUIC_ERR(mozquic_start_server(connection),
                      "setup-start_server_ip6");
    std::cout << "server initialized - Listening on port " << config.originPort << " with fd = " << mozquic_osfd(connection) << std::endl;

    return mozquic_osfd(connection);
  }

  expected<actor> create_newb(io::network::native_socket sockfd,
                                        policy::transport_ptr pol);
  /*virtual expected<actor> create_newb(io::network::native_socket sockfd,
                                      policy::transport_ptr pol) {
    CAF_LOG_TRACE(CAF_ARG(sockfd));
    auto n = detail::apply_args_prefixed(
            io::spawn_newb<io::network::protocol::quic, no_spawn_options, Fun,
            Ts...>,
            detail::get_indices(std::tuple(0,0)),
            nullptr, this->backend().system(),
            fun_, std::move(pol), sockfd
    );
    return n;
    return
  }*/

  void read_event(caf::io::newb_base*) {
    using namespace io::network;
    int i = 0;
    do {
      mozquic_IO(connection);
      usleep (1000);
    } while(++i < 20);

    if(closure.new_connection) {
      int fd = mozquic_osfd(closure.new_connection);
      transport_ptr transport{new quic_transport{closure.new_connection}};
      auto en = create_newb(fd, std::move(transport));
      if (!en) {
        return;
      }
      auto ptr = caf::actor_cast<caf::abstract_actor*>(*en);
      CAF_ASSERT(ptr != nullptr);
      auto& ref = dynamic_cast<io::newb<message>&>(*ptr);
      init(ref);
      std::cout << "new connection accepted." << std::endl;
      closure.new_connection = nullptr;
    }
  }

  /*std::pair<io::network::native_socket, transport_ptr>
  accept_event(io::newb_base *) {
    using namespace io::network;
    int i = 0;
    do {
      mozquic_IO(connection);
      usleep(1000);
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
  }*/

  void init(io::newb_base*, io::newb<Message>& spawned) override {
    spawned.start();
  }
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
