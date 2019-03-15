//
// Created by Jakob on 9/27/18.
//

#pragma once

#include "MozQuic.h"
#include "caf/policy/transport.hpp"
#include <cstdint>

constexpr char nss_config_path[] =
                "/home/jakob/CLionProjects/agere-2018/nss-config/";
constexpr int trigger_threshold = 10;

int mozquic_connection_CB_server(void* closure, uint32_t event, void* param);
int mozquic_connectionCB_client(void* closure, uint32_t event, void* param);

struct mozquic_closure {
  std::vector<mozquic_stream_t*> new_data_streams;
  bool connected = false;
};