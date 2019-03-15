//
// Created by boss on 9/28/18.
//

#include <detail/mozquic_CB.h>

#include "mozquic_CB.h"

int mozquic_connection_CB_server(void* closure, uint32_t event, void* param) {
  CAF_LOG_TRACE("");
  auto clo = static_cast<mozquic_closure*>(closure);
  switch (event) {
    // save new stream_ for
    case MOZQUIC_EVENT_NEW_STREAM_DATA: {
      mozquic_stream_t* stream = param;
      int id = mozquic_get_streamid(stream);
      if (id >= 128) {
        return MOZQUIC_ERR_GENERAL;
      }
      CAF_LOG_DEBUG("MOZQUIC_EVENT_NEW_STREAM_DATA");
      clo->new_data_streams.emplace_back(stream);
      break;
    }

    // only server side should use this.
    case MOZQUIC_EVENT_ACCEPT_NEW_CONNECTION:
      CAF_LOG_DEBUG("MOZQUIC_EVENT_ACCEPT_NEW_CONNECTION");
      mozquic_set_event_callback(param, mozquic_connection_CB_server);
      mozquic_set_event_callback_closure(param, closure);
      break;

    case MOZQUIC_EVENT_ERROR:
      CAF_LOG_DEBUG("MOZQUIC_EVENT_ERROR");
      [[clang::fallthrough]];
    case MOZQUIC_EVENT_CLOSE_CONNECTION:
      CAF_LOG_DEBUG("MOZQUIC_EVENT_CLOSE_CONNECTION");
      mozquic_destroy_connection(param);
      return MOZQUIC_ERR_GENERAL;

    default:
      break;
  }
  return MOZQUIC_OK;
}

int mozquic_connectionCB_client(void* closure, uint32_t event, void*) {
  CAF_LOG_TRACE("");
  auto clo = static_cast<mozquic_closure*>(closure);
  switch (event) {
    case MOZQUIC_EVENT_0RTT_POSSIBLE:
      CAF_LOG_DEBUG("MOZQUIC_EVENT_0RTT_POSSIBLE");
      [[clang::fallthrough]];
    case MOZQUIC_EVENT_CONNECTED:
      CAF_LOG_DEBUG("MOZQUIC_EVENT_CONNECTED");
      clo->connected = true;
      break;

    default:
      break;
  }
  return MOZQUIC_OK;
}

int trigger_IO(mozquic_connection_t* conn) {
  CAF_LOG_TRACE("");
  for (int i = 0; i < trigger_threshold; ++i) {
    auto ret = mozquic_IO(conn);
    if (MOZQUIC_OK != ret) {
      return ret;
    }
  }
  return MOZQUIC_OK;
}
