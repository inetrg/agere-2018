//
// Created by Jakob on 9/27/18.
//

#pragma once

#include "../caf/policy/MozQuic.h"
#include "caf/policy/transport.hpp"
#include <cstdint>

int connectionCB_accept(void *closure, uint32_t event, void *param);
int connectionCB_transport(void *closure, uint32_t event, void *param);
int connectionCB_connect(void *closure, uint32_t event, void *param);


struct transport_closure {
  transport_closure(caf::policy::byte_buffer* wr_buf, caf::policy::byte_buffer*
                    rec_buf) :
          connected{false},
          amount_read{0},
          write_buffer{wr_buf},
          receive_buffer{rec_buf},
          message(""){
  };

  bool connected;
  int amount_read;
  caf::policy::byte_buffer* write_buffer;
  caf::policy::byte_buffer* receive_buffer;
  std::string message;
};

struct accept_closure {
  mozquic_connection_t* new_connection = nullptr;
};
