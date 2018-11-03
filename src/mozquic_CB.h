//
// Created by Jakob on 9/27/18.
//

#pragma once

#include "MozQuic.h"
#include "caf/policy/transport.hpp"
#include <cstdint>

int connectionCB_accept(void *closure, uint32_t event, void *param);
int connectionCB_transport(void *closure, uint32_t event, void *param);
int connectionCB_connect(void *closure, uint32_t event, void *param);
int connectionCB_send(void* closure, uint32_t event, void* param);

struct transport_closure {
  bool connected = false;
  size_t len = 0;
  size_t amount_read = 0;
  void* receive_buffer = nullptr;
};

struct accept_closure {
  mozquic_connection_t* new_connection = nullptr;
};
