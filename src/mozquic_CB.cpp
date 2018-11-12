//
// Created by boss on 9/28/18.
//

#include "mozquic_CB.h"

int connectionCB(void* closure, uint32_t event, void* param) {
  auto clo = static_cast<mozquic_closure*>(closure);
  switch (event) {
    // save new stream for
    case MOZQUIC_EVENT_NEW_STREAM_DATA:
      clo->new_streams.insert(param);
      break;

    // only server side should use this.
    case MOZQUIC_EVENT_ACCEPT_NEW_CONNECTION:
      mozquic_set_event_callback(param, connectionCB);
      mozquic_set_event_callback_closure(param, closure);
      break;


    case MOZQUIC_EVENT_CLOSE_CONNECTION:
    case MOZQUIC_EVENT_ERROR:
      return mozquic_destroy_connection(param);

    default:
      break;
  }
  return MOZQUIC_OK;
}

int connectionCB_connect(void* closure, uint32_t event, void*) {
  auto clo = static_cast<mozquic_closure*>(closure);
  switch (event) {
    case MOZQUIC_EVENT_0RTT_POSSIBLE:
      std::cout << "0RTT possible" << std::endl;
      [[clang::fallthrough]];
    case MOZQUIC_EVENT_CONNECTED:
      std::cout << "client connected" << std::endl;
      clo->connected = true;
      break;

    default:
      break;
  }
  return MOZQUIC_OK;
}