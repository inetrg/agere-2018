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

#include "policy/quic_acceptor.hpp"
#include "policy/quic_transport.hpp"
#include "caf/config.hpp"
#include "detail/mozquic_CB.h"

#ifdef CAF_WINDOWS
# ifndef WIN32_LEAN_AND_MEAN
#   define WIN32_LEAN_AND_MEAN
# endif // WIN32_LEAN_AND_MEAN
# ifndef NOMINMAX
#   define NOMINMAX
# endif
# ifdef CAF_MINGW
#   undef _WIN32_WINNT
#   undef WINVER
#   define _WIN32_WINNT WindowsVista
#   define WINVER WindowsVista
#   include <w32api.h>
# endif
# include <io.h>
# include <windows.h>
# include <winsock2.h>
# include <ws2ipdef.h>
# include <ws2tcpip.h>
#else
# include <unistd.h>
# include <arpa/inet.h>
# include <cerrno>
# include <fcntl.h>
# include <netdb.h>
# include <netinet/in.h>
# include <netinet/ip.h>
# include <netinet/tcp.h>
# include <sys/socket.h>
# include <sys/types.h>
# ifdef CAF_POLL_MULTIPLEXER
#   include <poll.h>
# elif defined(CAF_EPOLL_MULTIPLEXER)
#   include <sys/epoll.h>
# else
#   error "neither CAF_POLL_MULTIPLEXER nor CAF_EPOLL_MULTIPLEXER defined"
# endif
#endif


namespace caf {
namespace policy {

template <class Message>
accept_quic<Message>::accept_quic() :
  accept<Message>(true),
  connection_accept_pol_{nullptr},
  streams_(0) {
    // nop
}

template <class Message>
accept_quic<Message>::~accept_quic() {
  // nullcheck mozquic_functions aren't nullsafe!
  if (connection_accept_pol_) {
    mozquic_shutdown_connection(connection_accept_pol_);
    mozquic_destroy_connection(connection_accept_pol_);
  }
}

template <class Message>
expected<io::network::native_socket>
accept_quic<Message>::create_socket(uint16_t port, const char*, bool) {
  CAF_LOG_TRACE("");
  // check for nss_config
  if (mozquic_nss_config(const_cast<char*>(nss_config_path)) != MOZQUIC_OK) {
    std::cerr << "nss-config failure" << std::endl;
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
    std::cerr << "create new connection failed" << std::endl;
    return sec::runtime_error;
  }
  mozquic_set_event_callback(connection_accept_pol_,
                             mozquic_connection_CB_server);
  mozquic_set_event_callback_closure(connection_accept_pol_, &closure_);
  if (MOZQUIC_OK != mozquic_start_server(connection_accept_pol_)) {
    CAF_LOG_ERROR("start_server failed");
    std::cerr << "start_server failed" << std::endl;
    return sec::runtime_error;
  }

  // prev trigger thresh
  for (int i = 0; i < trigger_threshold; ++i) {
    if (MOZQUIC_OK != mozquic_IO(connection_accept_pol_)) {
      CAF_LOG_ERROR("mozquic_IO failed");
      std::cerr << "mozquic_IO failed" << std::endl;
      return sec::runtime_error;
    }
  }
  std::cout << "socket_fd = " << mozquic_osfd(connection_accept_pol_) << std::endl;
  return mozquic_osfd(connection_accept_pol_);
}

template <class Message>
void accept_quic<Message>::accept_connection(mozquic_stream_t* stream,
        io::network::acceptor_base* base) {
  CAF_LOG_TRACE("");
  // create newb with new connection_transport_pol
  auto fd = mozquic_osfd(connection_accept_pol_);
  transport_ptr trans{new quic_transport(base, connection_accept_pol_, stream)};
  trans->prepare_next_read(nullptr);
  trans->prepare_next_write(nullptr);
  auto en = base->create_newb(fd, std::move(trans), false);
  if (!en) {
    CAF_LOG_ERROR("could not create newb");
    std::cerr << "could not create newb" << std::endl;
    return;
  }
  newbs_.insert(std::make_pair(stream, *en));
}

template <class Message>
void accept_quic<Message>::read_event(io::network::acceptor_base* base) {
  CAF_LOG_TRACE("");
  using namespace io::network;
  std::cout << "read_event called" << std::endl;
  // trigger IO to get incoming events
  for (int i = 0; i < trigger_threshold; ++i) {
    if (MOZQUIC_OK != mozquic_IO(connection_accept_pol_)) {
      CAF_LOG_ERROR("mozquic_IO failed");
      std::cerr << "mozquic_IO failed" << std::endl;
    }
  }

  // go through collected streams and look for the actor that has incoming data
  for (const auto& stream : closure_.new_data_streams) {
    // is key contained in map?
    if (newbs_.count(stream)) { // TODO: isn't there a better way?
      std::cout << "stream exists. Calling newb" << std::endl;
      // incoming data on existing newb
      auto& act = newbs_.at(stream);
      auto ptr = caf::actor_cast<caf::abstract_actor*>(act);
      CAF_ASSERT(ptr != nullptr);
      auto &ref = dynamic_cast<io::newb<Message>&>(*ptr);
      ref.read_event();
    } else {
      std::cout << "stream doesn't exist. creating newbfor it." << std::endl;
      accept_connection(stream, base);
      std::cout << "streams_ = " << ++streams_ << std::endl;
    }
  }
  closure_.new_data_streams.clear();

  // TODO: is this necessary?!
  // trigger IO some more after read/write
  for (int i = 0; i < trigger_threshold; ++i) {
    if (MOZQUIC_OK != mozquic_IO(connection_accept_pol_)) {
      CAF_LOG_ERROR("mozquic_IO failed");
      std::cerr << "mozquic_IO failed" << std::endl;
    }
  }
}

template <class Message>
error accept_quic<Message>::write_event(io::network::acceptor_base* base) {
  CAF_LOG_TRACE("");
  for (const auto& pair : newbs_) {
    auto& act = pair.second;
    auto ptr = caf::actor_cast<caf::abstract_actor *>(act);
    CAF_ASSERT(ptr != nullptr);
    auto &ref = dynamic_cast<io::newb<Message> &>(*ptr);
    ref.write_event();
  }
  for (int i = 0; i < trigger_threshold; ++i) {
    if (MOZQUIC_OK != mozquic_IO(connection_accept_pol_)) {
      CAF_LOG_ERROR("mozquic_IO failed");
      std::cerr << "mozquic_IO failed" << std::endl;
      return sec::runtime_error;
    }
  }
  base->stop_writing();
  return none;
}

template <class Message>
void accept_quic<Message>::shutdown(io::network::acceptor_base*, io::network::native_socket) {
  CAF_LOG_TRACE("");
  for (const auto& pair : newbs_) {
    // close open streams
    mozquic_end_stream(pair.first); // stream
    auto ptr = caf::actor_cast<caf::abstract_actor *>(pair.second); // newb
    CAF_ASSERT(ptr != nullptr);
    auto &ref = dynamic_cast<io::newb<Message> &>(*ptr);
    ref.graceful_shutdown();
  }
  newbs_.clear();
  // shutdown connection
  mozquic_shutdown_connection(connection_accept_pol_);
  mozquic_destroy_connection(connection_accept_pol_);
  connection_accept_pol_ = nullptr;
}

template <class Message>
void accept_quic<Message>::init(io::network::acceptor_base*, io::newb<Message>& spawned) {
  spawned.start();
}

} // namespace policy
} // namespace caf
