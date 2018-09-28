//
// Created by boss on 8/11/18.
//
#include <iostream>
#include "../caf/policy/MozQuic.h"

#ifndef MOZQUIC_EXAMPLE_MOZQUIC_HELPER_H
#define MOZQUIC_EXAMPLE_MOZQUIC_HELPER_H

static bool check_flag = false;

#define CHECK_MOZQUIC_ERR(err, msg) \
if (err && check_flag) {\
  switch(err) { \
    case MOZQUIC_ERR_GENERAL: \
      std::cerr << msg << ": MOZQUIC_ERR_GENERAL" << std::endl;\
      break; \
    case MOZQUIC_ERR_INVALID: \
      std::cerr << msg << ": MOZQUIC_ERR_INVALID" << std::endl;\
      break; \
    case MOZQUIC_ERR_MEMORY:\
      std::cerr << msg << ": MOZQUIC_ERR_MEMORY" << std::endl;\
      break; \
    case MOZQUIC_ERR_IO:\
      std::cerr << msg << ": MOZQUIC_ERR_IO" << std::endl;\
      break; \
    case MOZQUIC_ERR_CRYPTO:\
      std::cerr << msg << ": MOZQUIC_ERR_CRYPTO" << std::endl;\
      break; \
    case MOZQUIC_ERR_VERSION:\
      std::cerr << msg << ": MOZQUIC_ERR_VERSION" << std::endl;\
      break; \
    case MOZQUIC_ERR_ALREADY_FINISHED:\
      std::cerr << msg << ": MOZQUIC_ERR_ALREADY_FINISHED" << std::endl;\
      break; \
    case MOZQUIC_ERR_DEFERRED:\
      std::cerr << msg << ": MOZQUIC_ERR_DEFERRED" << std::endl;\
      break; \
    default: \
      std::cerr << msg << ": UNRECOGNIZED MOZQUIC_ERR" << std::endl;\
      break; \
  }\
  exit(err);\
}

#define CHECK_MOZQUIC_EVENT(event) \
switch (event) {\
  case MOZQUIC_EVENT_NEW_STREAM_DATA:\
    cout << "MOZQUIC_EVENT_NEW_STREAM_DATA" << endl;\
    break;\
  case MOZQUIC_EVENT_RESET_STREAM:\
    cout << "MOZQUIC_EVENT_RESET_STREAM" << endl;\
    break;\
  case MOZQUIC_EVENT_CONNECTED:\
    cout << "MOZQUIC_EVENT_CONNECTED" << endl;\
    break;\
  case MOZQUIC_EVENT_ACCEPT_NEW_CONNECTION:\
    cout << "MOZQUIC_EVENT_ACCEPT_NEW_CONNECTION" << endl;\
    break;\
  case MOZQUIC_EVENT_CLOSE_CONNECTION:\
    cout << "MOZQUIC_EVENT_CLOSE_CONNECTION" << endl;\
    break;\
  case MOZQUIC_EVENT_IO:\
    cout << "MOZQUIC_EVENT_IO" << endl;\
    break;\
  case MOZQUIC_EVENT_ERROR:\
    cout << "MOZQUIC_EVENT_ERROR" << endl;\
    break;\
  case MOZQUIC_EVENT_LOG:\
    cout << "MOZQUIC_EVENT_LOG" << endl;\
    break;\
  case MOZQUIC_EVENT_TRANSMIT:\
    cout << "MOZQUIC_EVENT_TRANSMIT" << endl;\
    break;\
  case MOZQUIC_EVENT_RECV:\
    cout << "MOZQUIC_EVENT_RECV" << endl;\
    break;\
  case MOZQUIC_EVENT_TLSINPUT:\
    cout << "MOZQUIC_EVENT_TLSINPUT" << endl;\
    break;\
  case MOZQUIC_EVENT_PING_OK:\
    cout << "MOZQUIC_EVENT_PING_OK" << endl;\
    break;\
  case MOZQUIC_EVENT_TLS_CLIENT_TPARAMS:\
    cout << "MOZQUIC_EVENT_TLS_CLIENT_TPARAMS" << endl;\
    break;\
  case MOZQUIC_EVENT_CLOSE_APPLICATION:\
    cout << "MOZQUIC_EVENT_CLOSE_APPLICATION" << endl;\
    break;\
  case MOZQUIC_EVENT_0RTT_POSSIBLE:\
    cout << "MOZQUIC_EVENT_0RTT_POSSIBLE" << endl;\
    break;\
  case MOZQUIC_EVENT_STREAM_NO_REPLAY_ERROR:\
    cout << "MOZQUIC_EVENT_STREAM_NO_REPLAY_ERROR" << endl;\
    break;\
  default:\
    cout << "UNRECOGNIZED MOZQUIC_EVENT" << endl;\
    break;\
}

#endif //MOZQUIC_EXAMPLE_MOZQUIC_HELPER_H
