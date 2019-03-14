/******************************************************************************
 *                       ____    _    _____                                   *
 *                      / ___|  / \  |  ___|    C++                           *
 *                     | |     / _ \ | |_       Actor                         *
 *                     | |___ / ___ \|  _|      Framework                     *
 *                      \____/_/   \_|_|                                      *
 *                                                                            *
 * Copyright 2011-2018 Dominik Charousset                                     *
 *                                                                            *
 * Distributed under the terms and conditions of the BSD 3-Clause License or  *
 * (at your option) under the terms and conditions of the Boost Software      *
 * License 1.0. See accompanying files LICENSE and LICENSE_ALTERNATIVE.       *
 *                                                                            *
 * If you did not receive a copy of the license files, see                    *
 * http://opensource.org/licenses/BSD-3-Clause and                            *
 * http://www.boost.org/LICENSE_1_0.txt.                                      *
 ******************************************************************************/

#pragma once

#include <set>
#include <zconf.h>
#include "caf/io/newb.hpp"
#include "caf/config.hpp"
#include "caf/io/network/default_multiplexer.hpp"
#include "caf/io/network/native_socket.hpp"
#include "caf/policy/accept.hpp"
#include "caf/policy/transport.hpp"
#include "policy/quic_transport.hpp"
#include "MozQuic.h"
#include "detail/mozquic_CB.h"

namespace caf {
namespace policy {

// only server uses acceptors
template <class Message>
class accept_quic : public accept<Message> {
private:
  // connection_ip4 state
  mozquic_closure closure_;
  mozquic_connection_t* connection_accept_pol_;
  std::map<mozquic_stream_t*, actor> newbs_;

public:
  accept_quic();

  ~accept_quic() override;

  expected<io::network::native_socket>
  create_socket(uint16_t port, const char*, bool) override;

  void accept_connection(mozquic_stream_t* stream, io::network::acceptor_base* base);

  void read_event(io::network::acceptor_base* base) override;

  error write_event(io::network::acceptor_base* base) override;

  void shutdown(io::network::acceptor_base*, io::network::native_socket) override;
};

// was a generic protocol before - but written out!
template <class T>
using quic_protocol = generic_protocol<T>;

} // namespace policy
} // namespace caf
