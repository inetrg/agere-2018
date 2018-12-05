//
// Created by Jakob on 9/27/18.
//

#pragma once

#include "MozQuic.h"
#include "caf/policy/transport.hpp"
#include <cstdint>

constexpr char nss_config_path[] =
                "/home/jakob/CLionProjects/agere-2018/nss-config/";


int connectionCB(void* closure, uint32_t event, void* param);
int connectionCB_connect(void *closure, uint32_t event, void *param);

struct mozquic_closure {
  std::set<mozquic_stream_t*> new_streams;
  bool connected = false;
};