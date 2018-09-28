//
// Created by Jakob on 9/27/18.
//

#ifndef NEWB_MEASUREMENTS_MOZQUIC_CB_H
#define NEWB_MEASUREMENTS_MOZQUIC_CB_H

#include "../caf/policy/MozQuic.h"
#include "caf/policy/transport.hpp"
#include <cstdint>

namespace caf {
namespace policy {

struct client_closure {
  client_closure(byte_buffer &wr_buf, byte_buffer &rec_buf) :
          write_buffer(wr_buf),
          receive_buffer(rec_buf) {
  };

  bool is_server = false;
  bool connected = false;
  int amount_read = 0;
  byte_buffer &write_buffer;
  byte_buffer &receive_buffer;
};

struct server_closure {
  mozquic_connection_t *new_connection = nullptr;
};

int connEventCB(void *closure, uint32_t event, void *param) {
  switch (event) {
    case MOZQUIC_EVENT_CONNECTED: {
      auto clo = static_cast<client_closure *>(closure);
      std::cout << "connected" << std::endl;
      clo->connected = true;
      break;
    }

    case MOZQUIC_EVENT_NEW_STREAM_DATA: {
      auto clo = static_cast<client_closure *>(closure);
      mozquic_stream_t *stream = param;
      if (mozquic_get_streamid(stream) & 0x3)
        break;
      uint32_t received = 0;
      int fin = 0;
      clo->receive_buffer.resize(1024); // allocate enough space for reading into
      // the receive_buffer
      do {
        int code = mozquic_recv(stream, clo->receive_buffer.data(),
                                1024,
                                &received,
                                &fin);
        if (code != MOZQUIC_OK)
          return code;
        clo->amount_read += received; // gather amount that was read

      } while (received > 0 && !fin);
      break;
    }

    case MOZQUIC_EVENT_CLOSE_CONNECTION:
    case MOZQUIC_EVENT_ERROR:
      return mozquic_destroy_connection(param);

    case MOZQUIC_EVENT_ACCEPT_NEW_CONNECTION: {
      auto clo = static_cast<server_closure *>(closure);
      mozquic_set_event_callback(param, connEventCB);
      mozquic_set_event_callback_closure(param, closure);
      clo->new_connection = param;
      break;
    }

    default:
      break;
  }

  return MOZQUIC_OK;
}

}
}

#endif //NEWB_MEASUREMENTS_MOZQUIC_CB_H
