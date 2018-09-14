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
#include "mozquic_helper.h"

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

int connEventCB(void* closure, uint32_t event, void* param);

quic_transport::quic_transport()
        : read_threshold{0},
          collected{0},
          maximum{0},
          writing{false},
          written{0},
          connection{nullptr},
          closure{} {
  // nop
}

io::network::rw_state quic_transport::read_some
(io::network::newb_base*) {
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
    std::cout << "received data: " << closure.buffer.data() << std::endl;
  return io::network::rw_state::success;
}

bool quic_transport::should_deliver() {
  CAF_LOG_DEBUG(CAF_ARG(collected) << CAF_ARG(read_threshold));
  return collected >= read_threshold;
}

void quic_transport::prepare_next_read(io::network::newb_base*) {
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
}

void quic_transport::configure_read(io::receive_policy::config config) {
  rd_flag = config.first;
  maximum = config.second;
}

io::network::rw_state quic_transport::write_some(io::network::newb_base*
parent) {
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
  return io::network::rw_state::success;
}

void quic_transport::prepare_next_write(io::network::newb_base* parent) {
  written = 0;
  send_buffer.clear();
  if (offline_buffer.empty()) {
    parent->backend().del(io::network::operation::write,
                          parent->fd(), parent);
    writing = false;
  } else {
    send_buffer.swap(offline_buffer);
  }
}

void quic_transport::flush(io::network::newb_base* parent) {
  CAF_ASSERT(parent != nullptr);
  CAF_LOG_TRACE(CAF_ARG(offline_buffer.size()));
  if (!offline_buffer.empty() && !writing) {
    parent->backend().add(io::network::operation::write,
                          parent->fd(), parent);
    writing = true;
    prepare_next_write(parent);
  }
}

int accept_new_connection(mozquic_connection_t* new_connection, closure_t* closure) {
  mozquic_set_event_callback(new_connection, connEventCB);
  mozquic_set_event_callback_closure(new_connection, closure);
  closure->connections.push_back(new_connection); // add this conn to all connections
  std::cout << "new_connection: fd=" << mozquic_osfd(new_connection) << std::endl;
  return MOZQUIC_OK;
}

int close_connection(mozquic_connection_t* c, closure_t* closure) {
  auto it = find(closure->connections.begin(), closure->connections.end(), c);
  if (it != closure->connections.end())
    closure->connections.erase(it);
  return mozquic_destroy_connection(c);
}

int connEventCB(void* closure, uint32_t event, void* param) {
  auto clo = static_cast<closure_t *>(closure);

  switch (event) {
    case MOZQUIC_EVENT_CONNECTED: {
      if(clo->is_server) break;
      std::cout << "connected" << std::endl;
      clo->connected = true;
      break;
    }

    case MOZQUIC_EVENT_NEW_STREAM_DATA: {
      mozquic_stream_t *stream = param;
      if (mozquic_get_streamid(stream) & 0x3)
        break;
      uint32_t received = 0;
      int fin = 0;
      clo->buffer.resize(1024); // allocate enough space for reading into
                                   // the buffer
      do {
        int code = mozquic_recv(stream, clo->buffer.data(),
                1024,
                &received,
                &fin);
        if (code != MOZQUIC_OK)
          return code;
        clo->amount_read += received; // gather amount that was read

      } while(received > 0 && !fin);
      break;
    }

    case MOZQUIC_EVENT_CLOSE_CONNECTION:
    case MOZQUIC_EVENT_ERROR:
      close_connection(param, static_cast<closure_t*>(closure));
      return MOZQUIC_ERR_GENERAL;

    case MOZQUIC_EVENT_ACCEPT_NEW_CONNECTION:
      if(clo->is_server)
        accept_new_connection(param, static_cast<closure_t*>(closure));
      break;

    default:
      break;
  }

  return MOZQUIC_OK;
}

expected<io::network::native_socket>
quic_transport::connect(const std::string& host, uint16_t port,
                       optional<io::network::protocol::network>) {
  // check for nss_config
  char nss_config[] = "/home/jakob/CLionProjects/measuring-newbs/nss-config/";
  if (mozquic_nss_config(const_cast<char*>(nss_config)) != MOZQUIC_OK) {
    std::cerr << "nss-config failure" << std::endl;
    return io::network::invalid_native_socket; // cant I return some error?
  }

  mozquic_config_t config;
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
    usleep (1000); // this is for handleio todo
    int code = mozquic_IO(connection);
    if (code != MOZQUIC_OK)
      break;
  } while (++i < 2000 && !closure.connected);

  if (!closure.connected)
    return io::network::invalid_native_socket;
  else
    return mozquic_osfd(connection);
}

expected<io::network::native_socket>
accept_quic::create_socket(uint16_t port, const char*, bool) {
  // check for nss_config
  char nss_config[] = "/home/jakob/CLionProjects/measuring-newbs/nss-config/";
  if (mozquic_nss_config(const_cast<char*>(nss_config)) != MOZQUIC_OK) {
    std::cerr << "nss-config failure" << std::endl;
    return io::network::invalid_native_socket;
  }

  closure.is_server = true;
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

  // one ipv6 connection should be enough -- example uses 1 ipv4 1 ipv6
  // 1 hrr?! and 1 hrr6 connection for some reason.
  mozquic_connection_t* connection = nullptr;
  CHECK_MOZQUIC_ERR(mozquic_new_connection(&connection, &config),
                    "setup-new_conn_ip6");
  CHECK_MOZQUIC_ERR(mozquic_set_event_callback(connection, connEventCB),
                    "setup-event_cb_ip6");
  CHECK_MOZQUIC_ERR(mozquic_set_event_callback_closure(connection, &closure),
                    "set callback closure error");
  CHECK_MOZQUIC_ERR(mozquic_start_server(connection),
                    "setup-start_server_ip6");
  closure.connections.push_back(connection);

  return mozquic_osfd(connection);
}

void accept_quic::read_event(caf::io::network::newb_base*) {
  for (auto c : closure.connections) {
    mozquic_IO(c);
  }
}

void accept_quic::init(io::network::newb_base& n) {
  n.start();
}

} // namespace policy
} // namespace caf
