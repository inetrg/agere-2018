//
// Created by Jakob Otto on 31.03.19.
//

#include "detail/quicly_stuff.hpp"
#include <iostream>

constexpr char ticket_file[] = "ticket.bin";
quicly_context_t ctx;

int on_stop_sending(quicly_stream_t *stream, int err) {
  assert(QUICLY_ERROR_IS_QUIC_APPLICATION(err));
  fprintf(stderr, "received STOP_SENDING: %" PRIu16 "\n", QUICLY_ERROR_GET_ERROR_CODE(err));
  return 0;
}

int on_receive_reset(quicly_stream_t *stream, int err) {
  assert(QUICLY_ERROR_IS_QUIC_APPLICATION(err));
  fprintf(stderr, "received RESET_STREAM: %" PRIu16 "\n", QUICLY_ERROR_GET_ERROR_CODE(err));
  return 0;
}

void on_closed_by_peer(quicly_closed_by_peer_t *self, quicly_conn_t *conn, int err, uint64_t frame_type, const char *reason,
                       size_t reason_len) {
  if (QUICLY_ERROR_IS_QUIC_TRANSPORT(err)) {
    fprintf(stderr, "transport close:code=0x%" PRIx16 ";frame=%" PRIu64 ";reason=%.*s\n", QUICLY_ERROR_GET_ERROR_CODE(err),
            frame_type, (int)reason_len, reason);
  } else if (QUICLY_ERROR_IS_QUIC_APPLICATION(err)) {
    fprintf(stderr, "application close:code=0x%" PRIx16 ";reason=%.*s\n", QUICLY_ERROR_GET_ERROR_CODE(err), (int)reason_len,
            reason);
  } else if (err == QUICLY_ERROR_RECEIVED_STATELESS_RESET) {
    fprintf(stderr, "stateless reset\n");
  } else {
    fprintf(stderr, "unexpected close:code=%d\n", err);
  }
}

int send_one(int fd, quicly_datagram_t *p) {
  int ret;
  msghdr mess = {};
  iovec vec = {};
  memset(&mess, 0, sizeof(mess));
  mess.msg_name = &p->sa;
  mess.msg_namelen = p->salen;
  vec.iov_base = p->data.base;
  vec.iov_len = p->data.len;
  mess.msg_iov = &vec;
  mess.msg_iovlen = 1;
  while ((ret = (int)sendmsg(fd, &mess, 0)) == -1 && errno == EINTR) {
  };
  return ret;
}

int send_pending(int fd, quicly_conn_t *conn) {
  quicly_datagram_t *packets[16];
  size_t num_packets, i;
  int ret;

  do {
    num_packets = sizeof(packets) / sizeof(packets[0]);
    if ((ret = quicly_send(conn, packets, &num_packets)) == 0) {
      for (i = 0; i != num_packets; ++i) {
        if ((send_one(fd, packets[i])) == -1) {
          perror("sendmsg failed");
        } else {
        }
        ret = 0;
        quicly_packet_allocator_t *pa = quicly_get_context(conn)->packet_allocator;
        pa->free_packet(pa, packets[i]);
      }
    } else {
    }
  } while (ret == 0 && num_packets == sizeof(packets) / sizeof(packets[0]));

  return ret;
}

void set_alpn(ptls_handshake_properties_t *pro, const char *alpn_str) {
  const char *start, *cur;
  //std::vector<ptls_iovec_t> list;
  ptls_iovec_t *list = nullptr;
  size_t entries = 0;
  start = cur = alpn_str;
#define ADD_ONE()                                                          \
    if ((cur - start) > 0) {                                               \
      list = (ptls_iovec_t*) realloc(list, sizeof(*list) * (entries + 1)); \
      list[entries].base = (uint8_t*) strndup(start, cur - start);         \
      list[entries++].len = cur - start;                                   \
}

  while (*cur) {
    if (*cur == ',') {
      ADD_ONE();
      start = cur + 1;
    }
    cur++;
  }
  if (start != cur)
    ADD_ONE();

  pro->client.negotiated_protocols.list = list;
  pro->client.negotiated_protocols.count = entries;
}

int save_ticket_cb(ptls_save_ticket_t *_self, ptls_t *tls, ptls_iovec_t src) {
  auto conn = static_cast<quicly_conn_t*>(*ptls_get_data_ptr(tls));
  ptls_buffer_t buf;
  char smallbuff[512];
  FILE *fp = nullptr;
  int ret;

  if (ticket_file == nullptr)
    return 0;

  ptls_buffer_init(&buf, smallbuff, 0);

  /* build data (session ticket and transport parameters) */
  ptls_buffer_push_block(&buf, 2, { ptls_buffer_pushv(&buf, src.base, src.len); });
  ptls_buffer_push_block(&buf, 2, {
          if ((ret = quicly_encode_transport_parameter_list(&buf, 1, quicly_get_peer_transport_parameters(conn), nullptr, nullptr)) != 0)
          goto Exit;
  });

  /* write file */
  if ((fp = fopen(ticket_file, "wb")) == nullptr) {
    fprintf(stderr, "failed to open file:%s:%s\n", ticket_file, strerror(errno));
    ret = PTLS_ERROR_LIBRARY;
    goto Exit;
  }
  fwrite(buf.base, 1, buf.off, fp);

  ret = 0;
  Exit:
  if (fp != nullptr)
    fclose(fp);
  ptls_buffer_dispose(&buf);
  return 0;
}

void load_ticket(ptls_handshake_properties_t* hs_properties,
                 quicly_transport_parameters_t* resumed_transport_params) {
  static uint8_t buf[65536];
  size_t len;
  int ret;


  FILE *fp;
  if ((fp = fopen(ticket_file, "rb")) == nullptr)
    return;
  len = fread(buf, 1, sizeof(buf), fp);
  if (len == 0 || !feof(fp)) {
    fprintf(stderr, "failed to load ticket from file:%s\n", ticket_file);
    exit(1);
  }
  fclose(fp);

  const uint8_t *src = buf, *end = buf + len;
  ptls_iovec_t ticket;
  ptls_decode_open_block(src, end, 2, {
          ticket = ptls_iovec_init(src, end - src);
          src = end;
  });
  ptls_decode_block(src, end, 2,
  if ((ret = quicly_decode_transport_parameter_list(resumed_transport_params, nullptr, nullptr, 1, src, end)) != 0)
    goto Exit;
  src = end;
  );
  hs_properties->client.session_ticket = ticket;

  Exit:;
}
