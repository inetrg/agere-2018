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

#include "caf/io/network/acceptor_base.hpp"
#include "caf/io/network/default_multiplexer.hpp"
#include "caf/io/network/native_socket.hpp"
#include "caf/io/network/newb_base.hpp"
#include "caf/policy/accept.hpp"
#include "caf/policy/transport.hpp"
#include "quicly_stuff.hpp"
#include <map>

namespace caf {
namespace policy {

struct quicly_transport;

struct quicly_stream_open_state : public quicly_stream_open_t {
  quicly_transport* state;
};

struct quicly_transport : public transport {
  friend quicly_stream_open_state;

  quicly_transport();

  io::network::rw_state read_some(io::network::newb_base* parent) override;

  bool should_deliver() override;

  void prepare_next_read(io::network::newb_base*) override;

  void configure_read(io::receive_policy::config config) override;

  io::network::rw_state write_some(io::network::newb_base* parent) override;

  void prepare_next_write(io::network::newb_base* parent) override;

  void flush(io::network::newb_base* parent) override;

  expected<io::network::native_socket>
  connect(const std::string& host, uint16_t port,
          optional<io::network::protocol::network> preferred = none) override;

  void shutdown(io::network::newb_base*, io::network::native_socket) override;

private:
  // quicly callbacks
  int on_stream_open(st_quicly_stream_open_t* self, st_quicly_stream_t* stream);
  int on_receive(quicly_stream_t *stream, size_t off, const void *src, size_t len);

  // quicly state
  quicly_stream_callbacks_t stream_callbacks = {
          quicly_streambuf_destroy,
          quicly_streambuf_egress_shift,
          quicly_streambuf_egress_emit,
          on_stop_sending,
          (&quicly_transport::on_receive),
          on_receive_reset
  };

  // connection info
  char* cid_key_;
  sockaddr_storage sa_;
  socklen_t salen_;

  quicly_cid_plaintext_t next_cid_;
  ptls_handshake_properties_t hs_properties_;
  quicly_transport_parameters_t resumed_transport_params_;
  quicly_closed_by_peer_t closed_by_peer_;
  quicly_stream_open_state stream_open_;
  ptls_save_ticket_t save_ticket_;
  ptls_key_exchange_algorithm_t *key_exchanges_[128];
  ptls_context_t tlsctx_;
  quicly_conn_t* conn_;
  int fd_;

  // State for reading.
  size_t read_threshold;
  size_t collected;
  size_t maximum;
  io::receive_policy_flag rd_flag;

  // State for writing.
  bool writing;
  size_t written;
};

io::network::native_socket get_newb_socket(io::network::newb_base*);

template <class Message>
struct accept_quicly : public accept<Message> {
private:
  char cid_key_[17];
  static int fd_;
  quicly_cid_plaintext_t next_cid_;
  ptls_handshake_properties_t hs_properties_;
  quicly_closed_by_peer_t closed_by_peer_;
  quicly_stream_open_t stream_open_;
  ptls_save_ticket_t save_ticket_;
  ptls_key_exchange_algorithm_t *key_exchanges_[128];
  ptls_context_t tlsctx_;
  static std::vector<quicly_conn_t*> conns_;
  bool enforce_retry_;
  sockaddr sa_;
  socklen_t salen_;
  std::map<quicly_conn_t*, actor> newbs_;

public:

  expected<io::network::native_socket>
  create_socket(uint16_t port, const char* host, bool reuse = false) override {
    memset(&tlsctx_, 0, sizeof(ptls_context_t));
    tlsctx_.random_bytes = ptls_openssl_random_bytes;
    tlsctx_.get_time = &ptls_get_time;
    tlsctx_.key_exchanges = key_exchanges_;
    tlsctx_.cipher_suites = ptls_openssl_cipher_suites;
    tlsctx_.require_dhe_on_psk = 1;
    tlsctx_.save_ticket = &save_ticket_;

    ctx = quicly_default_context;
    ctx.tls = &tlsctx_;
    ctx.stream_open = &stream_open_;
    ctx.closed_by_peer = &closed_by_peer_;

    setup_session_cache(ctx.tls);
    quicly_amend_ptls_context(ctx.tls);

    std::string path_to_certs;
    char* path = getenv("QUICLY_CERTS");
    if (path) {
      path_to_certs = path;
    } else {
      // try to load defailt certs
      path_to_certs = "/home/jakob/CLionProjects/quicly-chat/quicly/t/assets/";
    }
    load_certificate_chain(ctx.tls, (path_to_certs + "server.crt").c_str());
    load_private_key(ctx.tls, (path_to_certs + "server.key").c_str());

    key_exchanges_[0] = &ptls_openssl_secp256r1;

    char random_key[17];
    tlsctx_.random_bytes(random_key, sizeof(random_key) - 1);
    memcpy(cid_key_, random_key, sizeof(random_key)); // save cid_key

    ctx.cid_encryptor =
            quicly_new_default_cid_encryptor(&ptls_openssl_bfecb,
                                             &ptls_openssl_sha256,
                                             ptls_iovec_init(cid_key_,
                                                             strlen(cid_key_)));

    if (resolve_address(&sa_, &salen_, host, std::to_string(port).c_str(), AF_INET,
                        SOCK_DGRAM, IPPROTO_UDP) != 0) {
      CAF_LOG_ERROR("resolve address failed");
      return io::network::invalid_native_socket;
    }
    if ((fd_ = socket(sa_.sa_family, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
      CAF_LOG_ERROR("socket(2) failed");
      return io::network::invalid_native_socket;
    }
    int on = 1;
    if (setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) != 0) {
      CAF_LOG_ERROR("setsockopt(SO_REUSEADDR) failed");
      return io::network::invalid_native_socket;
    }
    if (bind(fd_, &sa_, salen_) != 0) {
      CAF_LOG_ERROR("bind(2) failed");
      return io::network::invalid_native_socket;
    }

    return fd_;
  }

  void accept_connection(quicly_conn_t* conn,
                         io::network::acceptor_base* base) {
    CAF_LOG_TRACE("");
    // create newb with new connection_transport_pol
    transport_ptr trans{new quicly_transport(base, fd_)};
    trans->prepare_next_read(nullptr);
    trans->prepare_next_write(nullptr);
    auto en = base->create_newb(fd_, std::move(trans), false);
    if (!en) {
      CAF_LOG_ERROR("could not create newb");
      return;
    }
    newbs_.insert(std::make_pair(conn, *en));
  }

  void read_event(io::network::acceptor_base* base) {
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
    while ((rret = recvmsg(fd_, &mess, 0)) <= 0);
    size_t off = 0;
    while (off != rret) {
      quicly_decoded_packet_t packet;
      size_t plen = quicly_decode_packet(&ctx, &packet, buf + off, rret - off);
      if (plen == SIZE_MAX)
        break;
      if (QUICLY_PACKET_IS_LONG_HEADER(packet.octets.base[0])) {
        if (packet.version != QUICLY_PROTOCOL_VERSION) {
          quicly_datagram_t* rp = quicly_send_version_negotiation(&ctx, &sa,
                  salen_, packet.cid.src, packet.cid.dest.encrypted);
          assert(rp != nullptr);
          if (send_one(fd_, rp) == -1)
            CAF_LOG_ERROR("sendmsg failed");
          break;
        }
      }
      quicly_conn_t *conn = nullptr;
      size_t i;
      for (i = 0; i != conns_.size(); ++i) {
        if (quicly_is_destination(conns_[i], &sa, salen_, &packet)) {
          conn = conns_[i];
          break;
        }
      }
      if (conn != nullptr) {
        /* existing connection */
        quicly_receive(conn, &packet);
      } else if (QUICLY_PACKET_IS_LONG_HEADER(packet.octets.base[0])) {
        /* new connection */
        int ret = quicly_accept(&conn, &ctx, &sa, mess.msg_namelen, &packet,
                                enforce_retry_ ? packet.token /* a production server should validate the token */
                                               : ptls_iovec_init(nullptr, 0),
                                &next_cid_, nullptr);
        if (ret == 0) {
          assert(conn != nullptr);
          ++next_cid_.master_id;
          conns_.emplace_back(conn);
          std::cout << "conns_.size() = " << conns_.size() << std::endl;
        } else {
          assert(conn == nullptr);
        }
      } else {
        /* short header packet; potentially a dead connection. No need to check the length of the incoming packet,
         * because loop is prevented by authenticating the CID (by checking node_id and thread_id). If the peer is also
         * sending a reset, then the next CID is highly likely to contain a non-authenticating CID, ... */
        if (packet.cid.dest.plaintext.node_id == 0 && packet.cid.dest.plaintext.thread_id == 0) {
          quicly_datagram_t *dgram = quicly_send_stateless_reset(&ctx, &sa, salen_, packet.cid.dest.encrypted.base);
          if (send_one(fd_, dgram) == -1)
            perror("sendmsg failed");
        }
      }
      off += plen;
    }
  }

  error write_event(io::network::acceptor_base* base) {
    CAF_LOG_TRACE("");
    for (const auto& pair : newbs_) {
      auto& act = pair.second;
      auto ptr = caf::actor_cast<caf::abstract_actor *>(act);
      CAF_ASSERT(ptr != nullptr);
      auto &ref = dynamic_cast<io::newb<Message> &>(*ptr);
      ref.write_event();
    }
    base->stop_writing();
    return none;
  }

  void init(io::network::acceptor_base*, io::newb<Message>& spawned) override {
    spawned.start();
  }

  void shutdown(io::network::acceptor_base*,
                io::network::native_socket sockfd) override {
    io::network::shutdown_both(sockfd);
  }

private:
  static quicly_stream_callbacks_t stream_callbacks;

  static int on_receive(quicly_stream_t *stream, size_t off, const void *src, size_t len) {
    return 0;
  }

  static int on_stream_open(struct st_quicly_stream_open_t* self, struct st_quicly_stream_t* stream) {
    return 0;
  }
};

template <class T>
using quicly_protocol = generic_protocol<T>;

} // namespace policy
} // namespace caf
