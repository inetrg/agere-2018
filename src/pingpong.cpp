#include "caf/all.hpp"
#include "caf/binary_deserializer.hpp"
#include "caf/binary_serializer.hpp"
#include "caf/detail/call_cfun.hpp"
#include "caf/io/network/newb.hpp"
#include "caf/logger.hpp"
#include "caf/policy/newb_ordering.hpp"
#include "caf/policy/newb_raw.hpp"
#include "caf/policy/newb_reliability.hpp"
#include "caf/policy/newb_udp.hpp"

using namespace caf;
using namespace caf::io::network;
using namespace caf::policy;

namespace {

using send_atom = atom_constant<atom("send")>;
using quit_atom = atom_constant<atom("quit")>;
using responder_atom = atom_constant<atom("responder")>;
using config_atom = atom_constant<atom("config")>;

struct raw_newb : public io::network::newb<policy::raw_data_message> {
  using message_type = policy::raw_data_message;

  raw_newb(caf::actor_config& cfg, default_multiplexer& dm,
            native_socket sockfd)
      : newb<message_type>(cfg, dm, sockfd),
        is_client(true),
        received_messages(0) {
    // nop
  }

  void handle(message_type& msg) override {
    CAF_PUSH_AID_FROM_PTR(this);
    CAF_LOG_TRACE("");
    uint32_t counter;
    binary_deserializer bd(&backend(), msg.payload, msg.payload_len);
    bd(counter);
    if (is_client) {
      received_messages += 1;
      if (received_messages % 100 == 0)
        std::cout << "got " << received_messages << std::endl;
      if (received_messages >= messages)
        send(this, quit_atom::value);
      else
        send_message(counter + 1);
    } else {
      send_message(counter);
    }
  }

  void send_message(uint32_t value) {
    auto whdl = wr_buf(nullptr);
    binary_serializer bs(&backend(), *whdl.buf);
    bs(value);
  }

  behavior make_behavior() override {
    set_default_handler(print_and_drop);
    return {
      [=](send_atom, uint32_t value) {
        send_message(value);
      },
      [=](config_atom, size_t m) {
        messages = m;
      },
      [=](responder_atom, actor r) {
        std::cout << "got responder assigned" << std::endl;
        responder = r;
        send(r, this);
      },
      [=](quit_atom) {
        std::cout << "got quit message" << std::endl;
        // Remove from multiplexer loop.
        stop();
        // Quit actor.
        quit();
        send(responder, quit_atom::value);
      },
      // Must be implemented at the moment, will be cought by the broker in a
      // later implementation.
      [=](atom_value atm, uint32_t id) {
        protocol->timeout(atm, id);
      },
    };
  }

  bool is_client;
  actor responder;
  size_t messages;
  uint32_t received_messages;
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
    std::cout << "terminating udp acceptor" << std::endl;
  }

  expected<actor> create_newb(native_socket sockfd,
                              io::network::transport_policy_ptr pol) override {
    CAF_LOG_TRACE(CAF_ARG(sockfd));
    std::cout << "creating newb" << std::endl;
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

class config : public actor_system_config {
public:
  uint16_t port = 12345;
  std::string host = "127.0.0.1";
  bool is_server = false;
  size_t messages = 10000;

  config() {
    opt_group{custom_options_, "global"}
    .add(port, "port,P", "set port")
    .add(host, "host,H", "set host")
    .add(is_server, "server,s", "set server")
    .add(messages, "messages,m", "set number of exchanged messages");
  }
};

void caf_main(actor_system& sys, const config& cfg) {
  using proto_t = udp_protocol<reliability<ordering<policy::raw>>>;
  //using proto_t = udp_protocol<ordering<policy::raw>>;
  using acceptor_t = udp_acceptor<proto_t>;
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
        std::cout << "[" << name << "] got broker, let's do this" << std::endl;
        self->become(running(self, name, m, b));
        self->set_default_handler(print_and_drop);
      }
    };
  };

  auto dummy_broker = [](io::broker*) -> behavior {
    return {
      [](io::new_connection_msg&) {
        std::cout << "got new connection" << std::endl;
      }
    };
  };

  auto name = cfg.is_server ? "server" : "client";
  auto helper = sys.spawn(init, name, self);

  actor nb;
  auto await_done = [&](std::string msg) {
    self->receive(
      [&](quit_atom) {
        std::cout << msg << std::endl;
      }
    );
  };
  if (cfg.is_server) {
    std::cout << "creating new server" << std::endl;
    auto server_ptr = make_server_newb<acceptor_t, accept_udp>(sys, port,
                                                               nullptr, true);
    // If I don't do this, our newb acceptor will never get events ...
    auto b = sys.middleman().spawn_server(dummy_broker, port + 1);
    await_done("done");
  } else {
    std::cout << "creating new client" << std::endl;
    auto client = make_client_newb<raw_newb, udp_transport, proto_t>(sys, host,
                                                                     port);
    self->send(client, responder_atom::value, helper);
    self->send(client, config_atom::value, size_t(cfg.messages));
    self->send(client, send_atom::value, uint32_t(0));
    await_done("done");
    std::abort();
  }
}

} // namespace anonymous

CAF_MAIN(io::middleman);
