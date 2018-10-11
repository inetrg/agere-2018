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

#include "../caf/policy/newb_quic.hpp"
#include "caf/config.hpp"
#include "mozquic_helper.hpp"

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

quic_transport::quic_transport(mozquic_connection_t* conn)
        : connection{conn},
          closure{send_buffer, receive_buffer},
          read_threshold{0},
          collected{0},
          maximum{0},
          rd_flag{io::receive_policy_flag::exactly},
          writing{false},
          written{0} {
  std::cout << "quic_transport()" << std::endl;
  // nop
}

io::network::rw_state quic_transport::read_some
(io::newb_base*) {
  std::cout << "read_some called" << std::endl;
  CAF_LOG_TRACE("");
  int i = 0;
  while (++i < 20) {
    if (mozquic_IO(connection) != MOZQUIC_OK) {
      CAF_LOG_DEBUG("recv failed");
      return io::network::rw_state::failure;
    }
  }
  size_t result = (closure.amount_read > 0) ?
          static_cast<size_t>(closure.amount_read) : 0;
  collected += result;
  received_bytes = collected;
  if (received_bytes)
    std::cout << "received data: " << closure.receive_buffer.data() << std::endl;

  std::cout << "read_some done" << std::endl;
  return io::network::rw_state::success;
}

bool quic_transport::should_deliver() {
  std::cout << "should_deliver called" << std::endl;
  CAF_LOG_DEBUG(CAF_ARG(collected) << CAF_ARG(read_threshold));
  return collected >= read_threshold;
}

void quic_transport::prepare_next_read(io::newb_base*) {
  std::cout << "prepare_next_read called" << std::endl;
  collected = 0;
  received_bytes = 0;
  switch (rd_flag) {
    case io::receive_policy_flag::exactly:
      if (receive_buffer.size() != maximum)
        receive_buffer.resize(maximum);
      read_threshold = maximum;
      break;
    case io::receive_policy_flag::at_most:
      if (receive_buffer.size() != maximum)
        receive_buffer.resize(maximum);
      read_threshold = 1;
      break;
    case io::receive_policy_flag::at_least: {
      // Read up to 10% more, but at least allow 100 bytes more.
      auto maximumsize = maximum + std::max<size_t>(100, maximum / 10);
      if (receive_buffer.size() != maximumsize)
        receive_buffer.resize(maximumsize);
      read_threshold = maximum;
      break;
    }
  }
  std::cout << "prepare_next_read done" << std::endl;
}

void quic_transport::configure_read(io::receive_policy::config config) {
  rd_flag = config.first;
  maximum = config.second;
}

io::network::rw_state quic_transport::write_some(io::newb_base*
parent) {
  std::cout << "write_some called" << std::endl;
  CAF_LOG_TRACE("");
  mozquic_stream_t* stream;
  char msg[] = "";
  mozquic_start_new_stream(&stream, connection, 0, 0, msg, 0, 0);
  const void* buf = send_buffer.data();
  int res = mozquic_send(stream, const_cast<void*>(buf),
          static_cast<uint32_t>(send_buffer.size()), 0);
  if (res != MOZQUIC_OK) {
    CAF_LOG_ERROR("send failed");
    return io::network::rw_state::failure;
  }
  int i = 0;
  while (++i < 20) {
    if(mozquic_IO(connection) != MOZQUIC_OK)
      return io::network::rw_state::failure;
  }
  prepare_next_write(parent);
  std::cout << "write_some done" << std::endl;
  return io::network::rw_state::success;
}

void quic_transport::prepare_next_write(io::newb_base* parent) {
  std::cout << "prepare_next_write called" << std::endl;
  written = 0;
  send_buffer.clear();
  if (offline_buffer.empty()) {
    parent->stop_writing();
    writing = false;
  } else {
    send_buffer.swap(offline_buffer);
  }
  std::cout << "prepare_next_write done" << std::endl;
}

void quic_transport::flush(io::newb_base* parent) {
  std::cout << "flush called" << std::endl;
  CAF_ASSERT(parent != nullptr);
  CAF_LOG_TRACE(CAF_ARG(offline_buffer.size()));
  if (!offline_buffer.empty() && !writing) {
    parent->start_writing();
    writing = true;
    prepare_next_write(parent);
  }
  std::cout << "flush done" << std::endl;
}


expected<io::network::native_socket>
quic_transport::connect(const std::string& host, uint16_t port,
                       optional<io::network::protocol::network>) {
  std::cout << "connect called" << std::endl;
  std::cout << "host=" << host << " port=" << port << std::endl;
  // check for nss_config
  if (mozquic_nss_config(const_cast<char*>(NSS_CONFIG_PATH)) != MOZQUIC_OK) {
    std::cerr << "nss-config failure" << std::endl;
    return io::network::invalid_native_socket; // cant I return some error?
  }

  mozquic_config_t config {};
  memset(&config, 0, sizeof(mozquic_config_t));
  // handle IO manually. automatic handling not yet implemented.
  config.handleIO = 0;
  config.originName = host.c_str();
  config.originPort = port;
  // set quic-related things
  CHECK_MOZQUIC_ERR(mozquic_unstable_api1(&config, "greaseVersionNegotiation",
                                          0, nullptr),
                    "connect-versionNegotiation");
  CHECK_MOZQUIC_ERR(mozquic_unstable_api1(&config, "tolerateBadALPN", 1,
                                          nullptr), "connect-tolerateALPN");
  CHECK_MOZQUIC_ERR(mozquic_unstable_api1(&config, "tolerateNoTransportParams",
                                          1, nullptr),
                    "connect-noTransportParams");
  CHECK_MOZQUIC_ERR(mozquic_unstable_api1(&config, "maxSizeAllowed", 1452,
                                          nullptr), "connect-maxSize");
  CHECK_MOZQUIC_ERR(mozquic_unstable_api1(&config, "enable0RTT", 1, nullptr),
                    "connect-0rtt");

  // open new connection
  CHECK_MOZQUIC_ERR(mozquic_new_connection(&connection, &config),
                    "connect-new_conn");
  CHECK_MOZQUIC_ERR(mozquic_set_event_callback(connection, connEventCB),
                    "connect-callback");
  CHECK_MOZQUIC_ERR(mozquic_set_event_callback_closure(connection, &closure),
                    "connect-callback closure");
  CHECK_MOZQUIC_ERR(mozquic_start_client(connection), "connect-start");

  uint32_t i=0;
  do {
    usleep (10000); // this is for handleio todo
    auto code = mozquic_IO(connection);
    if (code != MOZQUIC_OK) {
      std::cerr << "connect: retcode != MOZQUIC_OK!!" << std::endl;
      break;
    }
  } while (++i < 4000 && !closure.connected);

  std::cout << "connect done" << std::endl;
  std::cout << "closure.connected=" << ((closure.connected) ? "true" : "false") << std::endl;
  if (!closure.connected)
    return io::network::invalid_native_socket;
  else {
    auto fd = mozquic_osfd(connection);
    std::cout << "" << std::endl;
    return fd;
  }
}

} // namespace policy
} // namespace caf
