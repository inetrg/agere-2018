#include "caf/all.hpp"
#include "caf/binary_deserializer.hpp"
#include "caf/binary_serializer.hpp"
#include "caf/detail/call_cfun.hpp"
#include "caf/io/network/newb.hpp"
#include "caf/logger.hpp"
#include "caf/policy/newb_tcp.hpp"
#include "caf/policy/newb_raw.hpp"
#include "caf/io/broker.hpp"

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

struct basp_newb : public io::network::newb<policy::raw_data_message> {
  using message_type = policy::raw_data_message;

  basp_newb(caf::actor_config& cfg, default_multiplexer& dm,
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

  behavior make_behavior() override {
    set_default_handler(print_and_drop);
    return {
      [=](message_type& msg) {
        CAF_LOG_TRACE("");
        uint32_t counter;
        binary_deserializer bd(&backend(), msg.payload, msg.payload_len);
        bd(counter);
        if (is_client) {
          received_messages += 1;
          if (received_messages % 100 == 0)
            std::cerr << "got " << received_messages << std::endl;
          if (received_messages >= messages) {
            std::cout << "got all messages!" << std::endl;
            send(this, quit_atom::value);
          } else {
            send_message(counter + 1);
          }
        } else {
          send_message(counter);
        }
      },
      [=](send_atom, uint32_t value) {
        send_message(value);
      },
      [=](config_atom, size_t m) {
        messages = m;
      },
      [=](responder_atom, actor r) {
        std::cerr << "got responder assigned" << std::endl;
        responder = r;
        send(r, this);
      },
      [=](quit_atom) {
        std::cerr << "got quit message" << std::endl;
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
};

template <class ProtocolPolicy>
struct tcp_acceptor
    : public io::network::newb_acceptor<typename ProtocolPolicy::message_type> {
  using super = io::network::newb_acceptor<typename ProtocolPolicy::message_type>;

  tcp_acceptor(default_multiplexer& dm, native_socket sockfd)
      : super(dm, sockfd) {
    // nop
  }

  expected<actor> create_newb(native_socket sockfd,
                              io::network::transport_policy_ptr pol) override {
    CAF_LOG_TRACE(CAF_ARG(sockfd));
    std::cerr << "tcp_acceptor::creating newb" << std::endl;
    auto n = io::network::make_newb<basp_newb>(this->backend().system(), sockfd);
    auto ptr = caf::actor_cast<caf::abstract_actor*>(n);
    if (ptr == nullptr)
      return sec::runtime_error;
    auto& ref = dynamic_cast<basp_newb&>(*ptr);
    ref.transport = std::move(pol);
    ref.protocol.reset(new ProtocolPolicy(&ref));
    ref.responder = responder;
    ref.configure_read(io::receive_policy::exactly(sizeof(uint32_t)));
    ref.is_client = false;
    anon_send(responder, n);
    return n;
  }

  actor responder;
};

struct state {
  actor responder;
  caf::io::connection_handle other;
  size_t messages;
  uint32_t received_messages;
};

behavior tcp_server(stateful_broker<state>* self) {
  return {
    [=](actor responder) {
      self->state.responder = responder;
    },
    [=](const new_connection_msg& msg) {
      self->configure_read(msg.handle, io::receive_policy::exactly(sizeof(uint32_t)));
      self->state.other = msg.handle;
    },
    [=](new_data_msg& msg) {
      /*
      uint32_t counter;
      binary_deserializer bd(self->system(), msg.buf);
      bd(counter);
      */
      self->write(msg.handle, msg.buf.size(), msg.buf.data());
      self->flush(msg.handle);
    },
    [=](const connection_closed_msg&) {
      self->quit();
      self->send(self->state.responder, quit_atom::value);
    }
  };
}

behavior tcp_client(stateful_broker<state>* self, connection_handle hdl) {
  self->state.other = hdl;
  return {
    [=](start_atom, size_t messages, actor responder) {
      auto& s = self->state;
      s.responder = responder;
      s.messages = messages;
      self->configure_read(s.other, io::receive_policy::exactly(sizeof(uint32_t)));
      std::vector<char> buf;
      binary_serializer bs(self->system(), buf);
      bs(uint32_t(1));
      self->write(s.other, buf.size(), buf.data());
      self->flush(s.other);
    },
    [=](new_data_msg& msg) {
      auto& s = self->state;
      uint32_t counter;
      binary_deserializer bd(self->system(), msg.buf);
      bd(counter);
      s.received_messages += 1;
      if (s.received_messages % 100 == 0)
        std::cerr << "got " << s.received_messages << std::endl;
      if (s.received_messages >= s.messages) {
        std::cout << "got all messages!" << std::endl;
        self->send(s.responder, quit_atom::value);
        self->quit();
      } else {
        std::vector<char> buf;
        binary_serializer bs(self->system(), buf);
        bs(counter + 1);
        self->write(msg.handle, buf.size(), buf.data());
        self->flush(msg.handle);
      }
    },
    [=](const connection_closed_msg&) {
      self->quit();
      self->send(self->state.responder, quit_atom::value);
    }
  };
}

class config : public actor_system_config {
public:
  uint16_t port = 12345;
  std::string host = "127.0.0.1";
  bool is_server = false;
  size_t messages = 10000;
  bool traditional = false;

  config() {
    opt_group{custom_options_, "global"}
    .add(port, "port,P", "set port")
    .add(host, "host,H", "set host")
    .add(is_server, "server,s", "set server")
    .add(messages, "messages,m", "set number of exchanged messages")
    .add(traditional, "traditional,t", "use traditional style brokers");
  }
};

void caf_main(actor_system& sys, const config& cfg) {
  using namespace std::chrono;
  using proto_t = tcp_protocol<raw>;
  using acceptor_t = tcp_acceptor<proto_t>;
  const char* host = cfg.host.c_str();
  const uint16_t port = cfg.port;
  scoped_actor self{sys};

  auto running = [=](event_based_actor* self, std::string,
                     actor m, actor) -> behavior {
    return {
      [=](quit_atom) {
        self->send(m, quit_atom::value);
      }
    };
  };
  auto init = [=](event_based_actor* self, std::string name,
                  actor m) -> behavior {
    self->set_default_handler(skip);
    return {
      [=](actor b) {
        std::cerr << "[" << name << "] got broker, let's do this" << std::endl;
        self->become(running(self, name, m, b));
        self->set_default_handler(print_and_drop);
      }
    };
  };

  auto dummy_broker = [](io::broker*) -> behavior {
    return {
      [](io::new_connection_msg&) {
        std::cerr << "got new connection" << std::endl;
      }
    };
  };

  auto name = cfg.is_server ? "server" : "client";
  auto helper = sys.spawn(init, name, self);

  actor nb;
  auto await_done = [&](std::string msg) {
    self->receive(
      [&](quit_atom) {
        std::cerr << msg << std::endl;
      }
    );
  };
  if (!cfg.traditional) {
    if (cfg.is_server) {
      std::cerr << "creating new server" << std::endl;
      auto server_ptr = make_server_newb<acceptor_t, accept_tcp>(sys, port,
                                                                 nullptr, true);
      server_ptr->responder = self;
      // If I don't do this, our newb acceptor will never get events ...
      auto b = sys.middleman().spawn_server(dummy_broker, port + 1);
      await_done("done");
    } else {
      std::cerr << "creating new client" << std::endl;
      auto client = make_client_newb<basp_newb, tcp_transport, proto_t>(sys, host,
                                                                        port);
      self->send(client, responder_atom::value, helper);
      self->send(client, config_atom::value, size_t(cfg.messages));
      auto start = system_clock::now();
      self->send(client, send_atom::value, uint32_t(0));
      await_done("done");
      auto end = system_clock::now();
      std::cout << duration_cast<milliseconds>(end - start).count() << "ms" << std::endl;
    }
    std::abort();
  } else {
    if (cfg.is_server) {
      std::cerr << "creating traditional server" << std::endl;
      auto es = sys.middleman().spawn_server(tcp_server, port);
      self->send(*es, actor_cast<actor>(self));
      await_done("done");
    } else {
      std::cerr << "creating traditional client" << std::endl;
      auto ec = sys.middleman().spawn_client(tcp_client, host, port);
      auto start = system_clock::now();
      self->send(*ec, start_atom::value, size_t(cfg.messages), actor_cast<actor>(self));
      await_done("done");
      auto end = system_clock::now();
      std::cout << duration_cast<milliseconds>(end - start).count() << "ms" << std::endl;
    }
  }
}

} // namespace anonymous

CAF_MAIN(io::middleman);
