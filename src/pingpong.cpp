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
  using namespace std::chrono;
  using proto_t = udp_protocol<reliability<policy::raw>>;
  //using proto_t = udp_protocol<reliability<ordering<policy::raw>>>;
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
        // std::cerr << "[" << name << "] got broker, let's do this" << std::endl;
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
  if (cfg.is_server) {
    std::cerr << "creating server" << std::endl;
    auto server_ptr = make_server_newb<acceptor_t, accept_udp>(sys, port,
                                                               nullptr, true);
    server_ptr->responder = self;
    // If I don't do this, our newb acceptor will never get events ...
    auto b = sys.middleman().spawn_server(dummy_broker, port + 1);
    await_done("done");
  } else {
    std::cerr << "creating client" << std::endl;
    auto client = make_client_newb<raw_newb, udp_transport, proto_t>(sys, host,
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
}

} // namespace anonymous

CAF_MAIN(io::middleman);
