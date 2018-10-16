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

static const char* NSS_CONFIG_PATH =
        "/home/boss/CLionProjects/agere-2018/nss-config/";

struct quic_transport : public transport {
private:
  // connection_ip4 state
  mozquic_connection_t* connection;
  transport_closure closure;

public:
  explicit quic_transport(mozquic_connection_t* conn = nullptr);

  ~quic_transport() override {
    std::cout << "~quic_transport()" << std::endl;
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
  // connection_ip4 state
  mozquic_connection_t* connection_ip4;
  mozquic_connection_t* connection_ip6;
  mozquic_connection_t* hrr;
  mozquic_connection_t* hrr6;
  accept_closure closure_ip4;
  accept_closure closure_ip6;
  accept_closure closure_hrr;
  accept_closure closure_hrr6;
  std::vector<transport_ptr*> transports;

public:
  accept_quic() : accept<Message>(true) {
    std::cout << "accept_quic()" << std::endl;
    connection_ip4 = nullptr;
    connection_ip6 = nullptr;
    hrr = nullptr;
    hrr6 = nullptr;
  };

  ~accept_quic() override {
    // destroy all pending connections
    std::cout << "~accept_quic()" << std::endl;
    if (connection_ip4) {
      mozquic_shutdown_connection(connection_ip4);
      mozquic_destroy_connection(connection_ip4);
    }
  }

  expected<io::network::native_socket>
  create_socket(uint16_t port, const char*, bool) override {
    setenv("MOZQUIC_LOG", "all:9", 0);
    std::cout << "create_socket called" << std::endl;
    // check for nss_config
    if (mozquic_nss_config(const_cast<char*>(NSS_CONFIG_PATH)) != MOZQUIC_OK) {
      std::cerr << "nss-config failure" << std::endl;
      return io::network::invalid_native_socket;
    }

    mozquic_config_t config{};
    memset(&config, 0, sizeof(mozquic_config_t));
    config.originName = "foo.example.com";
    config.originPort = port;
    config.handleIO = 0;
    config.appHandlesLogging = 0;
    config.ipv6 = 0;
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
    CHECK_MOZQUIC_ERR(mozquic_unstable_api1(&config, "enable0RTT", 0, nullptr),
                      "setup-0rtt");

    // setting up the connection_ip4
    CHECK_MOZQUIC_ERR(mozquic_new_connection(&connection_ip4, &config),
                      "setup-new_conn_ip6");
    CHECK_MOZQUIC_ERR(mozquic_set_event_callback(connection_ip4, connectionCB_accept),
                      "setup-event_cb_ip6");
    CHECK_MOZQUIC_ERR(mozquic_set_event_callback_closure(connection_ip4, &closure_ip4),
                      "setup-event_cb_closure");
    CHECK_MOZQUIC_ERR(mozquic_start_server(connection_ip4),
                      "setup-start_server_ip6");

    // setting up the connection_ip4
    config.ipv6 = 1;
    CHECK_MOZQUIC_ERR(mozquic_new_connection(&connection_ip6, &config),
                      "setup-new_conn_ip6");
    CHECK_MOZQUIC_ERR(mozquic_set_event_callback(connection_ip6,
            connectionCB_accept),
                      "setup-event_cb_ip6");
    CHECK_MOZQUIC_ERR(mozquic_set_event_callback_closure(connection_ip6,
            &closure_ip6),
                      "setup-event_cb_closure");
    CHECK_MOZQUIC_ERR(mozquic_start_server(connection_ip6),
                      "setup-start_server_ip6");

    // setting up the connection_ip4
    config.originPort = port + 1;
    config.ipv6 = 0;
    mozquic_unstable_api1(&config, "forceAddressValidation", 1, nullptr);
    CHECK_MOZQUIC_ERR(mozquic_new_connection(&hrr, &config),
                      "setup-new_conn_ip6");
    CHECK_MOZQUIC_ERR(mozquic_set_event_callback(hrr, connectionCB_accept),
                      "setup-event_cb_ip6");
    CHECK_MOZQUIC_ERR(mozquic_set_event_callback_closure(hrr, &closure_hrr),
                      "setup-event_cb_closure");
    CHECK_MOZQUIC_ERR(mozquic_start_server(hrr),
                      "setup-start_server_ip6");

    // setting up the connection_ip4
    config.ipv6 = 1;
    CHECK_MOZQUIC_ERR(mozquic_new_connection(&hrr6, &config),
                      "setup-new_conn_ip6");
    CHECK_MOZQUIC_ERR(mozquic_set_event_callback(hrr6, connectionCB_accept),
                      "setup-event_cb_ip6");
    CHECK_MOZQUIC_ERR(mozquic_set_event_callback_closure(hrr6, &closure_hrr6),
                      "setup-event_cb_closure");
    CHECK_MOZQUIC_ERR(mozquic_start_server(hrr6),
                      "setup-start_server_ip6");

    auto i = 0;
    do {
      mozquic_IO(connection_ip4);
      mozquic_IO(connection_ip6);
      mozquic_IO(hrr);
      mozquic_IO(hrr6);
      usleep(1000);
    } while(++i < 100);

    std::cout << "file descriptors: \n" <<
                 "ip4: " << mozquic_osfd(connection_ip4) << "\n" <<
                 "ip6: " << mozquic_osfd(connection_ip6) << "\n" <<
                 "hrr: " << mozquic_osfd(hrr) << "\n" <<
                 "hrr6: " << mozquic_osfd(hrr6) << "\n" <<
                 std::endl;

    std::cout << "server initialized - Listening on port " <<
                 config.originPort << " with fd = " <<
                 mozquic_osfd(connection_ip6) << std::endl;

    return mozquic_osfd(connection_ip6);
  }

  void accept_connection(accept_closure& closure, io::acceptor_base* base){
    // create newb with new connection_ip4
    if(closure.new_connection) {
      std::cout << "new connection fd: " << mozquic_osfd(closure.new_connection)
      << std::endl;
      int fd = mozquic_osfd(closure.new_connection);
      transport_ptr transport{new quic_transport{closure.new_connection}};
      transports.emplace_back(&transport);
      auto en = base->create_newb(fd, std::move(transport));
      if (!en) {
        return;
      }
      auto ptr = caf::actor_cast<caf::abstract_actor*>(*en);
      CAF_ASSERT(ptr != nullptr);
      auto& ref = dynamic_cast<io::newb<Message>&>(*ptr);
      init(base, ref);
      std::cout << "new connection accepted." << std::endl;
      closure.new_connection = nullptr;
    }
  }

  void read_event(io::acceptor_base* base) override {
    using namespace io::network;
    std::cout << "read_event called" << std::endl;
    int i = 0;
    do {
      mozquic_IO(connection_ip4);
      mozquic_IO(connection_ip6);
      mozquic_IO(hrr);
      mozquic_IO(hrr6);
      usleep (1000);
    } while(++i < 100 &&
            !closure_ip4.new_connection &&
            !closure_ip6.new_connection &&
            !closure_hrr.new_connection &&
            !closure_hrr.new_connection);

    accept_connection(closure_ip4, base);
    accept_connection(closure_ip6, base);
    accept_connection(closure_hrr, base);
    accept_connection(closure_hrr6, base);
    std::cout << "read_event done" << std::endl;
    /*
    // check existing connections for incoming data
    for (auto& trans : transports) {
      (*trans)->read_some(base);
    }*/
  }

  std::pair<io::network::native_socket, transport_ptr>
  accept_event(io::acceptor_base *) override {
    std::cout << "accept_event called" << std::endl;
    return {0, nullptr};
  }

  void init(io::acceptor_base*, io::newb<Message>& spawned) override {
    std::cout << "init called" << std::endl;
    spawned.start();
    std::cout << "init done." << std::endl;
  }
};

// was a generic protocol before - but written out!
template <class T>
using quic_protocol = generic_protocol<T>;

} // namespace policy
} // namespace caf
