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

#include <set>
#include <zconf.h>
#include "caf/io/newb.hpp"
#include "caf/config.hpp"
#include "caf/io/network/default_multiplexer.hpp"
#include "caf/io/network/native_socket.hpp"
#include "caf/policy/accept.hpp"
#include "caf/policy/transport.hpp"
#include "MozQuic.h"
#include "detail/mozquic_helper.hpp"
#include "detail/mozquic_CB.h"

namespace caf {
namespace policy {

const int trigger_threshold = 1000;

class quic_transport : public transport {
public:
  quic_transport(io::network::acceptor_base* acceptor, mozquic_connection_t* conn,
                 mozquic_stream_t* stream);
  quic_transport();

  ~quic_transport() override;

  io::network::rw_state read_some(io::network::newb_base* parent) override;

  bool should_deliver() override;

  void prepare_next_read(io::network::newb_base*) override;

  void configure_read(io::receive_policy::config config) override;

  io::network::rw_state write_some(io::network::newb_base* parent) override;

  void prepare_next_write(io::network::newb_base* parent) override;

  void flush(io::network::newb_base* parent) override;

  expected<io::network::native_socket>
  connect(const std::string& host, uint16_t port,
          optional<io::network::protocol::network>) override;

private:
  // connection_transport_pol state
  mozquic_connection_t* connection_transport_pol_;
  mozquic_stream_t* stream_;
  mozquic_closure closure_;
  io::network::newb_base* acceptor_;

  // State for reading.
  size_t read_threshold_;
  size_t collected_;
  size_t maximum_;
  io::receive_policy_flag rd_flag_;

  // State for writing.
  bool writing_;
  size_t written_;
};

// only server uses acceptors
template <class Message>
class accept_quic : public accept<Message> {
private:
  // connection_ip4 state
  mozquic_closure closure_;
  mozquic_connection_t* connection_accept_pol_;
  std::set<mozquic_stream_t*> streams_;
  std::vector<caf::actor> newbs_;

public:
  accept_quic() : accept<Message>(true), connection_accept_pol_{nullptr} {}

  ~accept_quic() override {
    // nullcheck mozquic_functions aren't nullsafe!
    if (connection_accept_pol_) {
      mozquic_shutdown_connection(connection_accept_pol_);
      mozquic_destroy_connection(connection_accept_pol_);
    }
  }

  expected<io::network::native_socket>
  create_socket(uint16_t port, const char*, bool) override {
    CAF_LOG_TRACE("");
    // check for nss_config
    if (mozquic_nss_config(const_cast<char*>(nss_config_path)) != MOZQUIC_OK) {
      CAF_LOG_ERROR("nss-config failure");
      return sec::runtime_error;
    }

    mozquic_config_t config{};
    memset(&config, 0, sizeof(mozquic_config_t));
    config.originName = "foo.example.com";
    config.originPort = port;
    config.handleIO = 0;
    config.appHandlesLogging = 0;
    config.ipv6 = 1;
    mozquic_unstable_api1(&config, "tolerateBadALPN", 1, nullptr);
    mozquic_unstable_api1(&config, "tolerateNoTransportParams", 1, nullptr);
    mozquic_unstable_api1(&config, "sabotageVN", 0, nullptr);
    mozquic_unstable_api1(&config, "forceAddressValidation", 0, nullptr);
    mozquic_unstable_api1(&config, "streamWindow", 4906, nullptr);
    mozquic_unstable_api1(&config, "connWindow", 8192, nullptr);
    mozquic_unstable_api1(&config, "enable0RTT", 1, nullptr);

    // setting up the connection_transport_pol
    if (MOZQUIC_OK != mozquic_new_connection(&connection_accept_pol_, &config)) {
      CAF_LOG_ERROR("create new connection failed");
      return sec::runtime_error;
    }
    mozquic_set_event_callback(connection_accept_pol_, connectionCB);
    mozquic_set_event_callback_closure(connection_accept_pol_, &closure_);
    if (MOZQUIC_OK != mozquic_start_server(connection_accept_pol_)) {
      CAF_LOG_ERROR("start_server failed");
      return sec::runtime_error;
    }

    // prev trigger thresh
    for (int i = 0; i < 200; ++i) {
      if (MOZQUIC_OK != mozquic_IO(connection_accept_pol_)) {
        CAF_LOG_ERROR("mozquic_IO failed");
        return sec::runtime_error;
      }
    }
    return mozquic_osfd(connection_accept_pol_);
  }

  void accept_connection(io::network::acceptor_base* base) {
    CAF_LOG_TRACE("");
    CAF_LOG_DEBUG((closure_.new_streams.empty() ? "new_streams empty" : "new streams NOT empty"));
    // create newb with new connection_transport_pol
    for (auto stream : closure_.new_streams) {
      // only accept *new* streams
      if(streams_.find(stream) != streams_.end()) continue;
      int fd = mozquic_osfd(connection_accept_pol_);
      transport_ptr trans{new quic_transport(base, connection_accept_pol_, stream)};
      trans->prepare_next_read(nullptr);
      trans->prepare_next_write(nullptr);
      auto en = base->create_newb(fd, std::move(trans), false);
      newbs_.emplace_back(*en);
      streams_.insert(stream);
    }
    closure_.new_streams.clear();
  }

  void read_event(io::network::acceptor_base* base) override {
    CAF_LOG_TRACE("");
    using namespace io::network;
    for (int i = 0; i < trigger_threshold; ++i) {
      if (MOZQUIC_OK != mozquic_IO(connection_accept_pol_)) {
        CAF_LOG_ERROR("mozquic_IO failed");
      }
    }
    // accept all pending connections
    accept_connection(base);

    // check existing connections for incoming data
    for (auto& act : newbs_) {
      auto ptr = caf::actor_cast<caf::abstract_actor *>(act);
      CAF_ASSERT(ptr != nullptr);
      auto &ref = dynamic_cast<io::newb<Message> &>(*ptr);
      ref.read_event();
    }
    // trigger IO some more after read/write
    for (int i = 0; i < trigger_threshold; ++i) {
      if (MOZQUIC_OK != mozquic_IO(connection_accept_pol_)) {
        CAF_LOG_ERROR("mozquic_IO failed");
      }
    }
  }

  error write_event(io::network::acceptor_base* base) override {
    CAF_LOG_TRACE("");
    for (auto& act : newbs_) {
      auto ptr = caf::actor_cast<caf::abstract_actor *>(act);
      CAF_ASSERT(ptr != nullptr);
      auto &ref = dynamic_cast<io::newb<Message> &>(*ptr);
      ref.write_event();
    }
    for (int i = 0; i < trigger_threshold; ++i) {
      if (MOZQUIC_OK != mozquic_IO(connection_accept_pol_)) {
        CAF_LOG_ERROR("mozquic_IO failed");
        return sec::runtime_error;
      }
    }
    base->stop_writing();
    return none;
  }

  void shutdown(io::network::acceptor_base*, io::network::native_socket) override {
    CAF_LOG_TRACE("");
    // clear all saved newbs
    for (auto& act : newbs_) {
      auto ptr = caf::actor_cast<caf::abstract_actor *>(act);
      CAF_ASSERT(ptr != nullptr);
      auto &ref = dynamic_cast<io::newb<Message> &>(*ptr);
      ref.graceful_shutdown();
    }
    newbs_.clear();
    // close open streams
    for (auto stream : streams_) {
      mozquic_end_stream(stream);
    }
    streams_.clear();
    // shutdown connection
    mozquic_shutdown_connection(connection_accept_pol_);
    mozquic_destroy_connection(connection_accept_pol_);
    connection_accept_pol_ = nullptr;
  }
};

// was a generic protocol before - but written out!
template <class T>
using quic_protocol = generic_protocol<T>;

} // namespace policy
} // namespace caf
