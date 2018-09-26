#include "caf/all.hpp"
#include "caf/binary_deserializer.hpp"
#include "caf/binary_serializer.hpp"
#include "caf/detail/call_cfun.hpp"
#include "caf/io/newb.hpp"
#include "caf/logger.hpp"
#include "caf/policy/newb_ordering.hpp"
#include "caf/policy/newb_raw.hpp"
#include "caf/policy/newb_reliability.hpp"
#include "caf/policy/newb_udp.hpp"

using namespace caf;
using namespace caf::io;
using namespace caf::io::network;
using namespace caf::policy;

namespace {

using start_atom = atom_constant<atom("start")>;
using send_atom = atom_constant<atom("send")>;
using quit_atom = atom_constant<atom("quit")>;
using responder_atom = atom_constant<atom("responder")>;
using config_atom = atom_constant<atom("config")>;

struct state {
  actor responder;
  caf::io::connection_handle other;
  size_t messages = 0;
  uint32_t received_messages = 0;
};

behavior raw_server(stateful_newb<new_raw_msg, state>* self, actor responder) {
  self->state.responder = responder;
  return {
    [=](atom_value atm, uint32_t id) {
      self->proto->timeout(atm, id);
    },
    [=](new_raw_msg& msg) {
      uint32_t counter;
      binary_deserializer bd(self->system(), msg.payload, msg.payload_len);
      bd(counter);
      if (counter != self->state.received_messages) {
        //std::cerr << "dropping msg: " << counter
        //          << " (was expecting: " << self->state.received_messages << ")"
        //          << std::endl;
        return;
      }
      {
        auto whdl = self->wr_buf(nullptr);
        binary_serializer bs(&self->backend(), *whdl.buf);
        bs(counter);
      }
      self->state.received_messages += 1;
    },
    [=](io_error_msg& msg) {
      std::cerr << "server got io error: " << to_string(msg.op) << std::endl;
      self->send(self, quit_atom::value);
    },
    [=](quit_atom) {
      self->quit();
      self->stop();
      self->send(self->state.responder, quit_atom::value);
    }
  };
}

behavior raw_client(stateful_newb<new_raw_msg, state>* self) {
  return {
    [=](atom_value atm, uint32_t id) {
      self->proto->timeout(atm, id);
    },
    [=](start_atom, size_t messages, actor responder) {
      auto& s = self->state;
      s.responder = responder;
      s.messages = messages;
      auto whdl = self->wr_buf(nullptr);
      binary_serializer bs(&self->backend(), *whdl.buf);
      bs(uint32_t(0));
    },
    [=](new_raw_msg& msg) {
      auto& s = self->state;
      uint32_t counter;
      binary_deserializer bd(self->system(), msg.payload, msg.payload_len);
      bd(counter);
      if (counter != s.received_messages) {
        //std::cerr << "dropping message: " << counter
        //          << " (was expecting: " << s.received_messages << ")"
        //          << std::endl;
        return;
      }
      s.received_messages += 1;
      if (s.received_messages % 100 == 0)
        std::cerr << "got " << s.received_messages << std::endl;
      if (s.received_messages >= s.messages) {
        std::cerr << "got all messages!" << std::endl;
        self->delayed_send(self, std::chrono::milliseconds(500),
                           quit_atom::value);
        self->send(self->state.responder, quit_atom::value);
      } else {
        auto whdl = self->wr_buf(nullptr);
        binary_serializer bs(&self->backend(), *whdl.buf);
        counter += 1;
        bs(counter);
      }
    },
    [=](io_error_msg& msg) {
      std::cerr << "client got io error: " << to_string(msg.op) << std::endl;
      self->send(self, quit_atom::value);
    },
    [=](quit_atom) {
      self->quit();
      self->stop();
      self->send(self->state.responder, quit_atom::value);
    }
  };
}

class config : public actor_system_config {
public:
  size_t messages = 2000;
  std::string host = "127.0.0.1";
  uint16_t port = 12345;
  bool is_server = false;
  bool is_ordered = false;

  config() {
    opt_group{custom_options_, "global"}
    .add(messages,   "messages,m", "set number of exchanged messages")
    .add(host,       "host,H",     "set host")
    .add(port,       "port,P",     "set port")
    .add(is_ordered, "ordered,o",  "use ordered UDP")
    .add(is_server,  "server,s",   "set server");
  }
};

void caf_main(actor_system& sys, const config& cfg) {
  using namespace std::chrono;
  using proto_t = udp_protocol<reliability<policy::raw>>;
  using ordered_proto_t = udp_protocol<reliability<ordering<policy::raw>>>;
  const char* host = cfg.host.c_str();
  const uint16_t port = cfg.port;
  scoped_actor self{sys};
  auto await_done = [&](std::string msg) {
    self->receive([&](quit_atom) { std::cerr << msg << std::endl; });
  };
  if (cfg.is_server) {
    std::cerr << "creating server" << std::endl;
    accept_ptr<policy::new_raw_msg> pol{new accept_udp<policy::new_raw_msg>};
    if (cfg.is_ordered) {
      auto eserver = make_server<ordered_proto_t>(sys, raw_server, std::move(pol),
                                                  port, nullptr, true, self);
      if (!eserver) {
        std::cerr << "failed to start server on port " << port << std::endl;
        return;
      }
      auto server = std::move(*eserver);
      await_done("done");
      std::cerr << "stopping server" << std::endl;
      server->stop();
    } else {
      auto eserver = make_server<proto_t>(sys, raw_server, std::move(pol), port,
                                          nullptr, true, self);
      if (!eserver) {
        std::cerr << "failed to start server on port " << port << std::endl;
        return;
      }
      auto server = std::move(*eserver);
      await_done("done");
      std::cerr << "stopping server" << std::endl;
      server->stop();
    }
  } else {
    std::cerr << "creating client" << std::endl;
    transport_ptr pol{new udp_transport};
    if (cfg.is_ordered) {
      auto eclient = spawn_client<ordered_proto_t>(sys, raw_client, std::move(pol),
                                                   host, port);
      if (!eclient) {
        std::cerr << "failed to start client for " << host << ":" << port
                  << std::endl;
        return;
      }
      auto client = std::move(*eclient);
      auto start = system_clock::now();
      self->send(client, start_atom::value, size_t(cfg.messages),
                 actor_cast<actor>(self));
      await_done("done");
      auto end = system_clock::now();
      std::cout << duration_cast<milliseconds>(end - start).count() << "ms"
                << std::endl;
      //self->send(client, exit_reason::user_shutdown);
    } else {
      auto eclient = spawn_client<proto_t>(sys, raw_client, std::move(pol), host,
                                           port);
      if (!eclient) {
        std::cerr << "failed to start client for " << host << ":" << port
                  << std::endl;
        return;
      }
      auto client = std::move(*eclient);
      auto start = system_clock::now();
      self->send(client, start_atom::value, size_t(cfg.messages),
                 actor_cast<actor>(self));
      await_done("done");
      auto end = system_clock::now();
      std::cout << duration_cast<milliseconds>(end - start).count() << "ms"
                << std::endl;
      await_done("done");
      //self->send(client, exit_reason::user_shutdown);
    }
  }
  std::abort();
}

} // namespace anonymous

CAF_MAIN(io::middleman);
