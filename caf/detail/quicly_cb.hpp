//
// Created by Jakob Otto on 31.03.19.
//

#ifndef PICOQUIC_TEST_QUICLY_STUFF_HPP
#define PICOQUIC_TEST_QUICLY_STUFF_HPP

#include <memory>

extern "C" {
#include "quicly.h"
#include "quicly/defaults.h"
#include "quicly/streambuf.h"
#include "detail/util.h"
}

int on_stop_sending(quicly_stream_t *stream, int err);
int on_receive_reset(quicly_stream_t *stream, int err);
void on_closed_by_peer(quicly_closed_by_peer_t *self, quicly_conn_t *conn,
                       int err, uint64_t frame_type, const char *reason,
                       size_t reason_len);
int send_one(int fd, quicly_datagram_t *p);
int send_pending(int fd, quicly_conn_t *conn);
int save_ticket_cb(ptls_save_ticket_t *_self, ptls_t *tls, ptls_iovec_t src);
void load_ticket(ptls_handshake_properties_t* hs_properties,
                 quicly_transport_parameters_t* resumed_transport_params);

#endif //PICOQUIC_TEST_QUICLY_STUFF_HPP
