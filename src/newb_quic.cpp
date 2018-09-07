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
  std::cout << "read some called" << std::endl;
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
    std::cout << "received data: \n" << closure.buffer.data() << std::endl;
  return io::network::rw_state::success;
}

bool quic_transport::should_deliver() {
  std::cout << "should deliver called" << std::endl;
  CAF_LOG_DEBUG(CAF_ARG(collected) << CAF_ARG(read_threshold));
  return collected >= read_threshold;
}

void quic_transport::prepare_next_read(io::network::newb_base*) {
  std::cout << "prepare next read called" << std::endl;
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
  std::cout << "configure read called" << std::endl;
  rd_flag = config.first;
  maximum = config.second;
}

io::network::rw_state quic_transport::write_some(io::network::newb_base*
parent) {
  std::cout << "write some called" << std::endl;
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
  std::cout << "prepare next write called" << std::endl;
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
  std::cout << "flush called" << std::endl;
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

  closure->connections.push_back(new_connection);
  std::cout << "new connections accepted. connected: "
            << closure->connections.size() << std::endl;
  return MOZQUIC_OK;
}

int close_connection(mozquic_connection_t* c, closure_t* closure) {
  auto it = find(closure->connections.begin(), closure->connections.end(), c);
  if (it != closure->connections.end())
    closure->connections.erase(it);
  std::cout << "server closed connection. connected: "
       << closure->connections.size() << std::endl;
  return mozquic_destroy_connection(c);
}

int connEventCB(void* closure, uint32_t event, void* param) {
  switch (event) {
    case MOZQUIC_EVENT_CONNECTED: {
      std::cout << "connected" << std::endl;
      auto clo = static_cast<closure_t *>(closure);
      clo->connected = true;
      break;
    }

    case MOZQUIC_EVENT_NEW_STREAM_DATA: {
      std::cout << "new stream data" << std::endl;
      mozquic_stream_t *stream = param;
      if (mozquic_get_streamid(stream) & 0x3)
        break;

      auto clo = static_cast<closure_t*>(closure);
      uint32_t received = 0;
      int fin = 0;
      clo->buffer.resize(1024); // allocate enough space for reading into
                                   // the buffer
      do {
        int code = mozquic_recv(stream, clo->buffer.data() + clo->amount_read,
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
       std::cout << (event == MOZQUIC_EVENT_ERROR ? "ERROR" : "CLOSE") << std::endl;
      close_connection(param, static_cast<closure_t*>(closure));
      return MOZQUIC_ERR_GENERAL;

    case MOZQUIC_EVENT_ACCEPT_NEW_CONNECTION:
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
  std::cout << "connect called" << std::endl;
  // check for nss_config
  char nss_config[] = "/home/jakob/CLionProjects/measuring-newbs/nss-config/";
  if (mozquic_nss_config(const_cast<char*>(nss_config)) != MOZQUIC_OK) {
    std::cerr << "nss-config failure" << std::endl;
    return io::network::invalid_native_socket;
  }

  mozquic_config_t config = {};
  memset(&config, 0, sizeof(mozquic_config_t));
  // handle IO manually. automatic handling not yet implemented.
  config.handleIO = 0;
  config.originName = host.c_str();
  config.originPort = port;
  // set quic-related things
  mozquic_unstable_api1(&config, "greaseVersionNegotiation", 0, nullptr);
  mozquic_unstable_api1(&config, "tolerateBadALPN", 1, nullptr);
  mozquic_unstable_api1(&config, "tolerateNoTransportParams", 1, nullptr);
  mozquic_unstable_api1(&config, "maxSizeAllowed", 1452, nullptr);
  mozquic_unstable_api1(&config, "enable0RTT", 1, nullptr);
  // open new connections
  mozquic_new_connection(&connection, &config);
  mozquic_set_event_callback(connection, connEventCB);
  mozquic_set_event_callback_closure(connection, &closure);
  mozquic_start_client(connection);

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
accept_quic::create_socket(uint16_t port, const char* host, bool) {
  std::cout << "create socket called" << std::endl;

  // check for nss_config
  char nss_config[] = "/home/jakob/CLionProjects/measuring-newbs/nss-config/";
  if (mozquic_nss_config(const_cast<char*>(nss_config)) != MOZQUIC_OK) {
    std::cerr << "nss-config failure" << std::endl;
    return io::network::invalid_native_socket;
  }

  mozquic_config_t config = {};
  memset(&config, 0, sizeof(mozquic_config_t));
  if (!host)
    config.originName = "foo.example.com";
  else
    config.originName = host;
  config.originPort = port;

  config.handleIO = 0;
  config.appHandlesLogging = 0;

  mozquic_unstable_api1(&config, "tolerateBadALPN", 1, nullptr);
  mozquic_unstable_api1(&config, "tolerateNoTransportParams", 1, nullptr);
  mozquic_unstable_api1(&config, "sabotageVN", 0, nullptr);
  mozquic_unstable_api1(&config, "forceAddressValidation", 0, nullptr);
  mozquic_unstable_api1(&config, "streamWindow", 4906, nullptr);
  mozquic_unstable_api1(&config, "connWindow", 8192, nullptr);
  mozquic_unstable_api1(&config, "enable0RTT", 1, nullptr);

  closure_t closure;
  // set up connections
  config.ipv6 = 0;
  mozquic_new_connection(&connection_ip4, &config);
  mozquic_set_event_callback(connection_ip4, connEventCB);
  mozquic_set_event_callback_closure(connection_ip4, &closure);
  mozquic_start_server(connection_ip4);

  config.ipv6 = 1;
  mozquic_new_connection(&connection_ip6, &config);
  mozquic_set_event_callback(connection_ip6, connEventCB);
  mozquic_set_event_callback_closure(connection_ip6, &closure);
  mozquic_start_server(connection_ip6);

  config.originPort = port + 1;
  config.ipv6 = 0;
  mozquic_unstable_api1(&config, "forceAddressValidation", 1, nullptr);
  mozquic_new_connection(&hrr, &config);
  mozquic_set_event_callback(hrr, connEventCB);
  mozquic_set_event_callback_closure(hrr, &closure);
  mozquic_start_server(hrr);

  config.ipv6 = 1;
  mozquic_new_connection(&hrr6, &config);
  mozquic_set_event_callback(hrr6, connEventCB);
  mozquic_set_event_callback_closure(hrr6, &closure);
  mozquic_start_server(hrr6);

  closure.connections.push_back(connection_ip4);
  closure.connections.push_back(connection_ip6);
  closure.connections.push_back(hrr);
  closure.connections.push_back(hrr6);

  std::cout << "server initialized" << std::endl;
  return mozquic_osfd(connection_ip6);
}

void accept_quic::read_event(caf::io::network::newb_base *) {
  std::cout << "read_event called" << std::endl;
  for (auto c : closure.connections) {
    if(mozquic_IO(c) != MOZQUIC_OK)
      break;
  }
}

void accept_quic::init(io::network::newb_base& n) {
  n.start();
}

} // namespace policy
} // namespace caf
