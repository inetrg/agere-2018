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

#include "newb_quicly.hpp"
#include "caf/io/newb.hpp"
#include "caf/binary_deserializer.hpp"

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
#include <policy/newb_quicly.hpp>

# elif defined(CAF_EPOLL_MULTIPLEXER)
#   include <sys/epoll.h>
# else
#   error "neither CAF_POLL_MULTIPLEXER nor CAF_EPOLL_MULTIPLEXER defined"
# endif
#endif

namespace caf {
namespace policy {

quicly_transport::quicly_transport(quicly_conn_t* conn, int fd,
                                   io::network::acceptor_base* parent, bool connected)
    : parent_(parent),
      cid_key_(nullptr),
      sa_(),
      salen_(0),
      next_cid_(),
      hs_properties_(),
      resumed_transport_params_(),
      closed_by_peer_{&on_closed_by_peer},
      stream_open_(),
      save_ticket_{&save_ticket_cb},
      key_exchanges_(),
      tlsctx_(),
      conn_(conn),
      stream_(nullptr),
      fd_(fd),
      read_threshold{1},
      collected{0},
      maximum{0},
      rd_flag{io::receive_policy_flag::exactly},
      writing{false},
      written{0},
      connected(connected){
  configure_read(io::receive_policy::at_most(1024));
  stream_open_.state = this;
  stream_open_.cb = [](quicly_stream_open_t* self, quicly_stream_t* stream) -> int {
    auto tmp = static_cast<quicly_stream_open_trans*>(self);
    return tmp->state->on_stream_open(self, stream);
  };
  streambuf_.state = this;
}

quicly_transport::quicly_transport() : quicly_transport(nullptr, -1, nullptr, false) {}

io::network::rw_state quicly_transport::read_some(io::network::newb_base* parent) {
  CAF_LOG_TRACE("");
  uint8_t buf[4096];
  msghdr mess = {};
  sockaddr sa = {};
  iovec vec = {};
  memset(&mess, 0, sizeof(mess));
  mess.msg_name = &sa;
  mess.msg_namelen = sizeof(sa);
  vec.iov_base = buf;
  vec.iov_len = sizeof(buf);
  mess.msg_iov = &vec;
  mess.msg_iovlen = 1;
  ssize_t rret;
  if ((rret = recvmsg(fd_, &mess, 0)) <= 0) {
    return io::network::rw_state::indeterminate;
  };

  size_t off = 0;
  while (off != rret) {
    quicly_decoded_packet_t packet;
    size_t plen = quicly_decode_packet(&ctx, &packet, buf + off, rret - off);
    if (plen == SIZE_MAX)
      break;
    quicly_receive(conn_, &packet);
    off += plen;
  }

  if (conn_ != nullptr) {
    auto ret = send_pending(fd_, conn_);
    if (ret != 0) {
      quicly_free(conn_);
      conn_ = nullptr;
      if (ret == QUICLY_ERROR_FREE_CONNECTION) {
        CAF_LOG_ERROR("QUICLY_ERROR_FREE_CONNECTION");
      } else {
        CAF_LOG_ERROR("quicly_send returned");
      }
    }
  }

  if (conn_ != nullptr) {
    auto state = quicly_get_state(conn_);
    if (!connected && state == QUICLY_STATE_CONNECTED) {
      connected = true;
      flush(parent);
    }
  }

  // need some state for the callbacks to determine a shutdown
  /*if (sres < 0) {
    auto err = io::network::last_socket_error();
    if (io::network::would_block_or_temporarily_unavailable(err))
      return io::network::rw_state::indeterminate;
    CAF_LOG_DEBUG("recv failed" << CAF_ARG(sres));
    return io::network::rw_state::failure;
  } else if (sres == 0) {
    // Recv returns 0 when the peer has performed an orderly shutdown.
    CAF_LOG_DEBUG("peer shutdown");
    return io::network::rw_state::failure;
  }*/
  /*size_t result = (sres > 0) ? static_cast<size_t>(sres) : 0;
  collected += result;
  received_bytes = collected;*/
  return io::network::rw_state::success;
}

void quicly_transport::take_data(ptls_iovec_t& input) {
  receive_buffer.resize(input.len);
  std::memcpy(receive_buffer.data(), input.base, input.len);
  collected += input.len;
  received_bytes = collected;
}

bool quicly_transport::should_deliver() {
  CAF_LOG_DEBUG(CAF_ARG(collected) << CAF_ARG(read_threshold));
  return collected >= read_threshold;
}

void quicly_transport::prepare_next_read(io::network::newb_base*) {
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

void quicly_transport::configure_read(io::receive_policy::config config) {
  rd_flag = config.first;
  maximum = config.second;
}

io::network::rw_state
quicly_transport::write_some(io::network::newb_base* parent) {
  CAF_LOG_TRACE("");
  const void* buf = send_buffer.data() + written;
  auto len = send_buffer.size() - written;

  // open stream if necessary
  if (connected && !stream_) {
    if (quicly_open_stream(conn_, &stream_, 0)) {
      CAF_LOG_ERROR("quicly_open_stream failed");
    }
  } 

  // send data and close stream afterwards.
  quicly_streambuf_egress_write(stream_, buf, len);

  if (send_pending(fd_, conn_)) {
    CAF_LOG_ERROR("send failed"
                  << CAF_ARG(io::network::last_socket_error_as_string()));
    return io::network::rw_state::failure;
  }
  written += len;
  // since the whole buffer is copied, we can call prepare next write every time
  prepare_next_write(parent);
  return io::network::rw_state::success;
}

void quicly_transport::prepare_next_write(io::network::newb_base* parent) {
  written = 0;
  send_buffer.clear();
  if (offline_buffer.empty()) {
    if (parent_) {
      parent_->stop_writing();
    } else {
      parent->stop_writing();
    }

    writing = false;
  } else {
    send_buffer.swap(offline_buffer);
  }
}

void quicly_transport::flush(io::network::newb_base* parent) {
  CAF_ASSERT(parent != nullptr);
  CAF_LOG_TRACE(CAF_ARG(offline_buffer.size()));
  if (!offline_buffer.empty() && !writing && connected) {
    if (parent_) {
      parent_->start_writing(); // this transport is multiplexed
    } else {
      parent->start_writing(); // this one isn't
    }

    writing = true;
    prepare_next_write(parent);
  }
}

expected<io::network::native_socket>
quicly_transport::connect(const std::string& host, uint16_t port,
                       optional<io::network::protocol::network>) {
  CAF_LOG_TRACE("");
  memset(&tlsctx_, 0, sizeof(ptls_context_t));
  tlsctx_.random_bytes = ptls_openssl_random_bytes;
  tlsctx_.get_time = &ptls_get_time;
  tlsctx_.key_exchanges = key_exchanges_;
  tlsctx_.cipher_suites = ptls_openssl_cipher_suites;
  tlsctx_.require_dhe_on_psk = 1;
  tlsctx_.save_ticket = &save_ticket_;

  ctx = quicly_spec_context;
  ctx.tls = &tlsctx_;
  ctx.stream_open = &stream_open_;
  ctx.closed_by_peer = &closed_by_peer_;
  
  // enable logging to std::cerr
  // ctx.event_log.cb = quicly_new_default_event_logger(stderr);
  // ctx.event_log.mask = UINT64_MAX;

  setup_session_cache(ctx.tls);
  quicly_amend_ptls_context(ctx.tls);

  key_exchanges_[0] = &ptls_openssl_secp256r1;
  load_ticket(&hs_properties_, &resumed_transport_params_);

  if (resolve_address(reinterpret_cast<sockaddr*>(&sa_), &salen_, host.c_str(),
                      std::to_string(port).c_str(), AF_INET, SOCK_DGRAM,
                      IPPROTO_UDP) != 0) {
    CAF_LOG_ERROR("could not resolve address");
    return io::network::invalid_native_socket;
  }

  sockaddr_in local = {};
  if ((fd_ = socket(reinterpret_cast<sockaddr*>(&sa_)->sa_family, SOCK_DGRAM,
                    IPPROTO_UDP)) == -1) {
    CAF_LOG_ERROR("socket(2) failed");
    return io::network::invalid_native_socket;
  }

  memset(&local, 0, sizeof(local));
  local.sin_family = AF_INET;
  if (bind(fd_, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0) {
    CAF_LOG_ERROR("bind(2) failed");
    return io::network::invalid_native_socket;
  }

  if (quicly_connect(&conn_, &ctx, host.c_str(), reinterpret_cast<sockaddr*>(&sa_),
                     salen_, &next_cid_, &hs_properties_,
                     &resumed_transport_params_)) {
    CAF_LOG_ERROR("quicly_connect failed");
    return io::network::invalid_native_socket;
  }
  ++next_cid_.master_id;
  send_pending(fd_, conn_);
  return fd_;
}

void quicly_transport::shutdown(io::network::newb_base*,
                             io::network::native_socket sockfd) {
  std::cout << "shutdown called" << std::endl;
  quicly_streambuf_egress_shutdown(stream_);
  send_pending(fd_, conn_);

  // this newb is not multiplexed -> clear whole connection
  if (!parent_) {
    // close connection.
    quicly_close(conn_, 0, "");
    send_pending(fd_, conn_);
    quicly_free(conn_);
    io::network::shutdown_both(sockfd);
  }
}

int quicly_transport::on_stream_open(quicly_stream_open_t*,
                                     quicly_stream_t* stream) {
  // set stream callbacks for the new stream
  int ret;
  if ((ret = quicly_streambuf_create(stream, sizeof(transport_streambuf))) != 0)
    return ret;
  stream->callbacks = &stream_callbacks;
  // set this ptr to streambuf.
  static_cast<transport_streambuf*>(stream->data)->state = this;
  return 0;
}

error quicly_transport::timeout(io::network::newb_base* base, atom_value, uint32_t) {
  send_pending(fd_, conn_);
  auto timeout = quicly_get_first_timeout(conn_);
  // set next timeout after quicly timeout ms
  base->set_timeout(std::chrono::milliseconds(100), 
                    caf::io::transport_atom::value, 0);
  return none;
}

io::network::native_socket get_newb_socket(io::network::newb_base* n) {
  return n->fd();
}

} // namespace policy
} // namespace caf
