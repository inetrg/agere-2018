//
// Created by Jakob on 9/27/18.
//

#pragma once

#include "../caf/policy/MozQuic.h"
#include "caf/policy/transport.hpp"
#include <cstdint>

int connEventCB(void *closure, uint32_t event, void *param);

struct client_closure {
  client_closure(caf::policy::byte_buffer &wr_buf, caf::policy::byte_buffer &rec_buf) :
          write_buffer(wr_buf),
          receive_buffer(rec_buf) {
  };

  bool connected = false;
  int amount_read = 0;
  caf::policy::byte_buffer &write_buffer;
  caf::policy::byte_buffer &receive_buffer;
};

struct server_closure {
  mozquic_connection_t *new_connection = nullptr;
};
