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
        "/home/jakob/CLionProjects/agere-2018/nss-config/";

struct quic_transport : public transport {
private:
  // connection_ip4 state
  mozquic_connection_t* connection;
  transport_closure* closure;

public:
  explicit quic_transport(mozquic_connection_t* conn = nullptr)
    : connection{conn},
      closure{new transport_closure},
      read_threshold{0},
      collected{0},
      maximum{0},
      rd_flag{io::receive_policy_flag::exactly},
      writing{false},
      written{0} {
    std::cout << "quic_transport()" << std::endl;
    if (conn) {
      mozquic_set_event_callback(connection, connectionCB_transport);
      mozquic_set_event_callback_closure(conn, closure);
      for(int i = 0; i < 5; ++i) {
        mozquic_IO(conn);
        usleep(1000);
      }
    }
  }

  ~quic_transport() override {
    std::cout << "~quic_transport()" << std::endl;
    if (closure) delete closure;
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
  mozquic_connection_t* connection;
  accept_closure closure;
  std::vector<quic_transport*> transports;

public:
  accept_quic() : accept<Message>(true) {
    std::cout << "accept_quic()" << std::endl;
    connection = nullptr;
  };

  ~accept_quic() override {
    // destroy all pending connections
    std::cout << "~accept_quic()" << std::endl;
    if (connection) {
      mozquic_shutdown_connection(connection);
      mozquic_destroy_connection(connection);
    }
  }


  expected<io::network::native_socket>
  create_socket(uint16_t port, const char*, bool) override {
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
    CHECK_MOZQUIC_ERR(mozquic_unstable_api1(&config, "enable0RTT", 1, nullptr),
                      "setup-0rtt");

    // setting up the connection
    config.ipv6 = 1;

    CHECK_MOZQUIC_ERR(mozquic_new_connection(&connection, &config),
                      "setup-new_conn_ip6");
    CHECK_MOZQUIC_ERR(mozquic_set_event_callback(connection,
                                                 connectionCB_accept),
                      "setup-event_cb_ip6");
    CHECK_MOZQUIC_ERR(mozquic_set_event_callback_closure(connection,
                                                         &closure),
                      "setup-event_cb_closure");
    CHECK_MOZQUIC_ERR(mozquic_start_server(connection),
                      "setup-start_server_ip6");

    auto i = 0;
    do {
      mozquic_IO(connection);
      usleep(1000);
    } while(++i < 100);

    std::cout << "server initialized - Listening on port " <<
              config.originPort << " with fd = " <<
              mozquic_osfd(connection) << std::endl;

    return mozquic_osfd(connection);
  }

  bool accept_connection(accept_closure& closure, io::acceptor_base* base) {
    // create newb with new connection_ip4
    if(closure.new_connection) {
      std::cout << "new connection fd: " << mozquic_osfd(closure.new_connection)
                << std::endl;
      //int fd = mozquic_osfd(closure.new_connection);
      //auto trans = new quic_transport{closure.new_connection};
      int fd = mozquic_osfd(connection);
      auto trans = new quic_transport{connection};
      transport_ptr transport{trans};
      transports.emplace_back(trans);
      auto en = base->create_newb(fd, std::move(transport));
      if (!en) {
        return false;
      }
      auto ptr = caf::actor_cast<caf::abstract_actor*>(*en);
      CAF_ASSERT(ptr != nullptr);
      auto& ref = dynamic_cast<io::newb<Message>&>(*ptr);
      init(base, ref);
      std::cout << "new connection accepted." << std::endl;
      closure.new_connection = nullptr;
      return true;
    }
    return false;
  }

  void read_event(io::acceptor_base* base) override {
    using namespace io::network;
    std::cout << "read_event called" << std::endl;
    int i = 0;
    do {
      mozquic_IO(connection);
      usleep (1000);
    } while(++i < 2000);

    accept_connection(closure, base);

    std::cout << "checking transports" << std::endl;

    // check existing connections for incoming data
    for (auto trans : transports) {
      std::cout << "trans" << std::endl;
      trans->prepare_next_read(base);
      trans->read_some(base);
    }
    std::cout << "read_event done" << std::endl;
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
