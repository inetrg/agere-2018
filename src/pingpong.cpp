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

constexpr uint32_t shut = 0x73687574;
constexpr uint32_t down = 0x646f776e;

struct state {
  actor responder;
  caf::io::connection_handle other;
  size_t messages = 0;
  uint32_t received_messages = 0;
};

behavior raw_server(stateful_newb<new_raw_msg, state>* self, actor responder) {
  std::cout << "server is running" << std::endl;
  self->state.responder = responder;
  return {
    [=](new_raw_msg& msg) {
      uint32_t counter;
      binary_deserializer bd(self->system(), msg.payload, msg.payload_len);
      bd(counter);
      if (msg.payload_len == 4) {
        auto whdl = self->wr_buf(nullptr);
        binary_serializer bs(&self->backend(), *whdl.buf);
        bs(counter);
      } else if (msg.payload_len == 8) {
        uint32_t rest;
        bd(rest);
        if (counter == shut && rest == down)
          self->send(self, quit_atom::value);
      }
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
  std::cout << "client is running" << std::endl;
  return {
    [=](start_atom, size_t messages, actor responder) {
      std::cout << "starting" << std::endl;
      auto& s = self->state;
      s.responder = responder;
      s.messages = messages;
      auto whdl = self->wr_buf(nullptr);
      binary_serializer bs(&self->backend(), *whdl.buf);
      bs(uint32_t(1));
    },
    [=](new_raw_msg& msg) {
      auto& s = self->state;
      uint32_t counter;
      binary_deserializer bd(self->system(), msg.payload, msg.payload_len);
      bd(counter);
      s.received_messages += 1;
      if (s.received_messages % 100 == 0)
        std::cerr << "got " << s.received_messages << std::endl;
      if (s.received_messages >= s.messages) {
        std::cerr << "got all messages!" << std::endl;
        auto whdl = self->wr_buf(nullptr);
        binary_serializer bs(&self->backend(), *whdl.buf);
        bs(shut);
        bs(down);
        self->send(self, quit_atom::value);
      } else {
        auto whdl = self->wr_buf(nullptr);
        binary_serializer bs(&self->backend(), *whdl.buf);
        bs(counter + 1);
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

/*
struct raw_newb : public io::network::newb<policy::new_raw_msg> {
  using message_type = policy::new_raw_msg;

  raw_newb(caf::actor_config& cfg, default_multiplexer& dm,
            native_socket sockfd)
      : newb<message_type>(cfg, dm, sockfd),
        is_client(true),
        received_messages(0) {
    // nop
  }

  void send_message(uint32_t value) {
    auto whdl = wr_buf(nullptr);
    binary_serializer bs(&backend(), *whdl.buf);
    bs(value);
  }

  void send_shutdown() {
    auto whdl = wr_buf(nullptr);
    binary_serializer bs(&backend(), *whdl.buf);
    bs(shut);
    bs(down);
  }

  behavior make_behavior() override {
    set_default_handler(print_and_drop);
    return {
      [=](message_type& msg) {
        CAF_LOG_TRACE("");
        uint32_t counter;
        binary_deserializer bd(&backend(), msg.payload, msg.payload_len);
        bd(counter);
        if (is_client) {
          if (counter != received_messages)
            return;
          received_messages += 1;
          if (received_messages % 100 == 0)
            std::cerr << "got " << received_messages << std::endl;
          if (received_messages >= messages) {
            send_shutdown();
            send(this, quit_atom::value);
          } else {
            send_message(counter + 1);
          }
        } else {
          if (msg.payload_len == 4) {
            send_message(counter);
          } else if (msg.payload_len == 8) {
            uint32_t rest;
            bd(rest);
            if (counter == shut && rest == down)
              send(this, quit_atom::value);
          }
        }
      },
      [=](send_atom, uint32_t value) {
        send_message(value);
      },
      [=](config_atom, size_t m) {
        messages = m;
      },
      [=](responder_atom, actor r) {
        // std::cerr << "got responder assigned" << std::endl;
        responder = r;
        send(r, this);
      },
      [=](quit_atom) {
        // std::cerr << "got quit message" << std::endl;
        // Remove from multiplexer loop.
        stop();
        // Quit actor.
        quit();
        send(responder, quit_atom::value);
      },
      [=](const io_error_msg& msg) {
        std::cerr << "io_error: " << to_string(msg.op) << std::endl;
        quit();
        send(responder, quit_atom::value);
      }
    };
  }

  bool is_client;
  actor responder;
  size_t messages;
  uint32_t received_messages;
  uint32_t shut = 0x73687574;
  uint32_t down = 0x646f776e;
};

template <class ProtocolPolicy>
struct udp_acceptor
    : public io::network::newb_acceptor<typename ProtocolPolicy::message_type> {
  using super = io::network::newb_acceptor<typename ProtocolPolicy::message_type>;

  udp_acceptor(default_multiplexer& dm, native_socket sockfd)
      : super(dm, sockfd) {
    // nop
  }

  ~udp_acceptor() {
    std::cerr << "terminating udp acceptor" << std::endl;
  }

  expected<actor> create_newb(native_socket sockfd,
                              io::network::transport_policy_ptr pol) override {
    CAF_LOG_TRACE(CAF_ARG(sockfd));
    // std::cerr << "creating newb" << std::endl;
    auto n = io::network::make_newb<raw_newb>(this->backend().system(), sockfd);
    auto ptr = caf::actor_cast<caf::abstract_actor*>(n);
    if (ptr == nullptr)
      return sec::runtime_error;
    auto& ref = dynamic_cast<raw_newb&>(*ptr);
    ref.transport = std::move(pol);
    ref.protocol.reset(new ProtocolPolicy(&ref));
    ref.responder = responder;
    // Read first message from this socket
    ref.is_client = false;
    ref.transport->prepare_next_read(this);
    ref.transport->read_some(this, *ref.protocol.get());
    // TODO: Just a workaround.
    anon_send(responder, n);
    return n;
  }

  actor responder;
};
*/

class config : public actor_system_config {
public:
  size_t messages = 2000;
  std::string host = "127.0.0.1";
  uint16_t port = 12345;
  bool is_server = false;

  config() {
    opt_group{custom_options_, "global"}
    .add(messages, "messages,m", "set number of exchanged messages")
    .add(host, "host,H", "set host")
    .add(port, "port,P", "set port")
    .add(is_server, "server,s", "set server");
  }
};

void caf_main(actor_system& sys, const config& cfg) {
  using namespace std::chrono;
  using proto_t = udp_protocol<reliability<policy::raw>>;
  //using proto_t = udp_protocol<reliability<ordering<policy::raw>>>;
  const char* host = cfg.host.c_str();
  const uint16_t port = cfg.port;
  scoped_actor self{sys};
  auto await_done = [&](std::string msg) {
    self->receive([&](quit_atom) { std::cerr << msg << std::endl; });
  };
  if (cfg.is_server) {
    std::cerr << "creating server" << std::endl;
    accept_ptr pol{new accept_udp};
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
    await_done("done");
  } else {
    std::cerr << "creating client" << std::endl;
    transport_ptr pol{new udp_transport};
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
    //self->send(client, exit_reason::user_shutdown);
  }
  std::abort();
}

} // namespace anonymous

CAF_MAIN(io::middleman);
