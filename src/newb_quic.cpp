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

#include "caf/config.hpp"
#include "MozQuic.h"
#include "../caf/policy/newb_quic.hpp"

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

quic_transport::quic_transport()
        : read_threshold{0},
          collected{0},
          maximum{0},
          writing{false},
          written{0} {
  // nop
}

error quic_transport::read_some(io::network::event_handler* parent) {
  CAF_LOG_TRACE("");
  size_t len = receive_buffer.size() - collected;
  void* buf = receive_buffer.data() + collected;
  auto sres = ::recv(parent->fd(),
                     reinterpret_cast<io::network::socket_recv_ptr>(buf),
                     len, io::network::no_sigpipe_io_flag);
  if (io::network::is_error(sres, true) || sres == 0) {
    std::cerr << "read some error" << std::endl;
    // recv returns 0 when the peer has performed an orderly shutdown
    return sec::runtime_error;
  }
  size_t result = (sres > 0) ? static_cast<size_t>(sres) : 0;
  collected += result;
  received_bytes = collected;
  return none;
}

bool quic_transport::should_deliver() {
  CAF_LOG_DEBUG(CAF_ARG(collected) << CAF_ARG(read_threshold));
  return collected >= read_threshold;
}

void quic_transport::prepare_next_read(io::network::event_handler*) {
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
      // read up to 10% more, but at least allow 100 bytes more
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

error quic_transport::write_some(io::network::event_handler* parent) {
  CAF_LOG_TRACE("");
  const void* buf = send_buffer.data() + written;
  auto len = send_buffer.size() - written;
  auto sres = ::send(parent->fd(),
                     reinterpret_cast<io::network::socket_send_ptr>(buf),
                     len, io::network::no_sigpipe_io_flag);
  if (io::network::is_error(sres, true))
    return sec::runtime_error;
  size_t result = (sres > 0) ? static_cast<size_t>(sres) : 0;
  written += result;
  count += 1;
  auto remaining = send_buffer.size() - written;
  if (remaining == 0)
    prepare_next_write(parent);
  return none;
}

void quic_transport::prepare_next_write(io::network::event_handler* parent) {
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

void quic_transport::flush(io::network::event_handler* parent) {
  CAF_ASSERT(parent != nullptr);
  CAF_LOG_TRACE(CAF_ARG(offline_buffer.size()));
  if (!offline_buffer.empty() && !writing) {
    parent->backend().add(io::network::operation::write,
                          parent->fd(), parent);
    writing = true;
    prepare_next_write(parent);
  }
}

int connEventCB(void* closure, uint32_t event, void* param) {
  switch (event) {
    case MOZQUIC_EVENT_CONNECTED: {
      auto clo = static_cast<closure_t *>(closure);
      clo->connected = true;
      break;
    }

    case MOZQUIC_EVENT_NEW_STREAM_DATA: {
      mozquic_stream_t *stream = param;
      if (mozquic_get_streamid(stream) & 0x3) {
        break;
      }

      uint32_t amt = 0;
      int fin = 0;
      do {
        int code = mozquic_recv(stream, static_cast<closure_t*>(closure)->buffer, 1023, &amt, &fin);
        if (code != MOZQUIC_OK) {
          break;
        }
      } while(amt > 0 && !fin);
      break;
    }

    case MOZQUIC_EVENT_CLOSE_CONNECTION:
    case MOZQUIC_EVENT_ERROR:
      // todo: delete closure. memoryleak!
      mozquic_destroy_connection(param);
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
  char nss_config[] = "/home/jakob/CLionProjects/mozquic_example/nss-config/";
  if (mozquic_nss_config(const_cast<char*>(nss_config)) != MOZQUIC_OK) {
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
  auto clo = new closure_t;
  clo->buffer = std::make_shared<io::network::byte_buffer>(receive_buffer);
  mozquic_new_connection(&connection, &config);
  mozquic_set_event_callback(connection, connEventCB);
  mozquic_set_event_callback_closure(connection, clo);
  mozquic_start_client(connection);

  uint32_t i=0;
  do {
    usleep (1000); // this is for handleio todo
    int code = mozquic_IO(connection);
    if (code != MOZQUIC_OK) {
      fprintf(stderr,"IO reported failure\n");
      break;
    }
  } while (++i < 2000 && !clo->connected);

  return mozquic_osfd(connection);
}

expected<io::network::native_socket>
accept_quic::create_socket(uint16_t, const char*, bool) {
  return io::network::invalid_native_socket;
}

std::pair<io::network::native_socket, io::network::transport_policy_ptr>
accept_quic::accept(caf::io::network::event_handler*) {
  return {io::network::invalid_native_socket, nullptr};
}

void accept_quic::init(io::network::newb_base& n) {
  n.start();
}

} // namespace policy
} // namespace caf
