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

#include "policy/newb_quic.hpp"
#include "caf/config.hpp"
#include "detail/mozquic_helper.hpp"

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

io::network::rw_state quic_transport::read_some
(io::newb_base*) {
  CAF_LOG_TRACE("");
  receive_buffer.resize(10);
  auto len = receive_buffer.size() - collected_;
  void* buf = receive_buffer.data() + collected_;
  uint32_t sres = 0;
  auto fin = 0;
  // trigger connection_accept_pol to get incoming data for recv
  auto res = mozquic_IO(connection_transport_pol_);
  if (res != MOZQUIC_OK) {
    CAF_LOG_ERROR("recv failed");
    return io::network::rw_state::failure;
  }
  auto code = mozquic_recv(stream_,
                           buf,
                           static_cast<uint32_t>(len),
                           &sres,
                           &fin);
  if (code != MOZQUIC_OK) {
    CAF_LOG_DEBUG("recv failed" << CAF_ARG(sres));
    return io::network::rw_state::failure;
  }
  auto result = static_cast<size_t>(sres);
  collected_ += result;
  received_bytes = collected_;
  return io::network::rw_state::success;
}

bool quic_transport::should_deliver() {
  CAF_LOG_DEBUG(CAF_ARG(collected_) << CAF_ARG(read_threshold_));
  return collected_ >= read_threshold_;
}

void quic_transport::prepare_next_read(io::newb_base*) {
  collected_ = 0;
  received_bytes = 0;
  switch (rd_flag_) {
    case io::receive_policy_flag::exactly:
      if (receive_buffer.size() != maximum_)
        receive_buffer.resize(maximum_);
      read_threshold_ = maximum_;
      break;
    case io::receive_policy_flag::at_most:
      if (receive_buffer.size() != maximum_)
        receive_buffer.resize(maximum_);
      read_threshold_ = 1;
      break;
    case io::receive_policy_flag::at_least: {
      // Read up to 10% more, but at least allow 100 bytes more.
      auto maximumsize = maximum_ + std::max<size_t>(100, maximum_ / 10);
      if (receive_buffer.size() != maximumsize)
        receive_buffer.resize(maximumsize);
      read_threshold_ = maximum_;
      break;
    }
  }
}

void quic_transport::configure_read(io::receive_policy::config config) {
  rd_flag_ = config.first;
  maximum_ = config.second;
}

io::network::rw_state quic_transport::write_some(io::newb_base*
parent) {
  CAF_LOG_TRACE("");
  // dont't write if nothing here to write.
  if(!writing_) return io::network::rw_state::success;
  void* buf = send_buffer.data() + written_;
  auto len = send_buffer.size() - written_;
  int res = mozquic_send(stream_, buf,
          static_cast<uint32_t>(len), 0);
  if (res != MOZQUIC_OK) {
    CAF_LOG_ERROR("send failed");
    return io::network::rw_state::failure;
  }
  // trigger IO so data will be passed through
  res = mozquic_IO(connection_transport_pol_);
  if (res != MOZQUIC_OK) {
    CAF_LOG_ERROR("send failed");
    return io::network::rw_state::failure;
  }
  written_ += len;
  auto remaining = send_buffer.size() - written_;
  if (remaining == 0)
    prepare_next_write(parent);
  return io::network::rw_state::success;
}

void quic_transport::prepare_next_write(io::newb_base* parent) {
  written_ = 0;
  send_buffer.clear();
  if (offline_buffer.empty()) {
    if(!acceptor_) {
      parent->stop_writing();
    }
    writing_ = false;
  } else {
    send_buffer.swap(offline_buffer);
  }
}

void quic_transport::flush(io::newb_base* parent) {
  CAF_ASSERT(parent != nullptr);
  CAF_LOG_TRACE(CAF_ARG(offline_buffer.size()));
  if (!offline_buffer.empty() && !writing_) {
    if(acceptor_) {
      acceptor_->start_writing();
    } else {
      parent->start_writing();
    }
    writing_ = true;
    prepare_next_write(parent);
  }
}


expected<io::network::native_socket>
quic_transport::connect(const std::string& host, uint16_t port,
                       optional<io::network::protocol::network>) {
  // check for nss_config
  if (mozquic_nss_config(const_cast<char*>(nss_config_path)) != MOZQUIC_OK) {
    CAF_LOG_ERROR("nss-config failure");
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
                    0, nullptr), "connect-versionNegotiation");
  CHECK_MOZQUIC_ERR(mozquic_unstable_api1(&config, "tolerateBadALPN", 1,
                    nullptr), "connect-tolerateALPN");
  CHECK_MOZQUIC_ERR(mozquic_unstable_api1(&config, "tolerateNoTransportParams",
                    1, nullptr), "connect-noTransportParams");
  CHECK_MOZQUIC_ERR(mozquic_unstable_api1(&config, "maxSizeAllowed", 1452,
                    nullptr), "connect-maxSize");
  CHECK_MOZQUIC_ERR(mozquic_unstable_api1(&config, "enable0RTT", 1, nullptr),
                    "connect-0rtt");

  CHECK_MOZQUIC_ERR(mozquic_new_connection(&connection_transport_pol_, &config),
                    "connect-new_conn");
  CHECK_MOZQUIC_ERR(mozquic_set_event_callback(connection_transport_pol_,
                    connectionCB_connect), "connect-callback");
  CHECK_MOZQUIC_ERR(mozquic_set_event_callback_closure(connection_transport_pol_,
                    &closure_), "connect-callback closure_ip4");
  CHECK_MOZQUIC_ERR(mozquic_start_client(connection_transport_pol_),
                    "connect-start");

  uint32_t i=0;
  do {
    usleep (1000);
    auto code = mozquic_IO(connection_transport_pol_);
    if (code != MOZQUIC_OK) {
      break;
    }
  } while (++i < 2000 && !closure_.connected);

  if (!closure_.connected) {
    CAF_LOG_ERROR("connect failed");
    return io::network::invalid_native_socket;
  }

  // start new stream_ for this transport.
  mozquic_start_new_stream(&stream_, connection_transport_pol_, 0, 0,
                           const_cast<char*>(""), 0, 0);
  return mozquic_osfd(connection_transport_pol_);
}

} // namespace policy
} // namespace caf
