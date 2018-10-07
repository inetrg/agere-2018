
#include "caf/all.hpp"
#include "caf/binary_deserializer.hpp"
#include "caf/binary_serializer.hpp"
#include "caf/detail/call_cfun.hpp"
#include "caf/io/newb.hpp"
#include "caf/logger.hpp"
#include "caf/policy/newb_raw.hpp"
#include "../caf/policy/newb_quic.hpp"

using namespace caf;

using caf::io::network::default_multiplexer;
using caf::io::network::invalid_native_socket;
using caf::io::network::native_socket;
using caf::policy::accept_quic;
using caf::policy::quic_protocol;
using caf::policy::quic_transport;

namespace {

using ordering_atom = atom_constant<atom("ordering")>;
using send_atom = atom_constant<atom("send")>;
using quit_atom = atom_constant<atom("quit")>;
using responder_atom = atom_constant<atom("responder")>;

constexpr size_t chunk_size = 8192; //128; //8192; //1024;
/*
struct raw_newb : public io::newb<policy::new_raw_msg> {
  using message_type = policy::new_raw_msg;

  raw_newb(caf::actor_config& cfg, default_multiplexer& dm,
           native_socket sockfd)
          : newb<message_type>(cfg, dm, sockfd),
            running(true),
            interval_counter(0),
            received_messages(0),
            interval(5000) {
    // nop
    CAF_LOG_TRACE("");
  }

  behavior make_behavior() override {
    set_default_handler(print_and_drop);
    return {
      [=](message_type& msg) {
        CAF_LOG_TRACE("");
        // just print the received message
        std::cout << msg.payload << std::endl;
      },
      [=](send_atom, char c) {
        if (running) {
          delayed_send(this, interval, send_atom::value, char((c + 1) % 256));
          auto whdl = wr_buf(nullptr);
          CAF_ASSERT(whdl.buf != nullptr);
          CAF_ASSERT(whdl.protocol != nullptr);
          binary_serializer bs(&backend(), *whdl.buf);
          whdl.buf->resize(chunk_size);
          std::fill(whdl.buf->begin(), whdl.buf->end(), c);
        }
      },
      [=](send_atom, std::string s) {
          auto whdl = wr_buf(nullptr);
          CAF_ASSERT(whdl.buf != nullptr);
          CAF_ASSERT(whdl.protocol != nullptr);
          binary_serializer bs(&backend(), *whdl.buf);
          whdl.buf->resize(s.size());
          std::copy(s.begin(), s.end(), whdl.buf->data());
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
      }
    };
  }

  bool running;
  actor responder;
  uint32_t interval_counter;
  uint32_t received_messages;
  std::chrono::microseconds interval;
  // values: measurement point, current interval, messages sent in interval, offline receive_buffer size
  std::vector<std::tuple<std::chrono::microseconds, size_t, size_t>> data;
};

template <class ProtocolPolicy>
struct quic_acceptor
      : public io::network::newb_acceptor<typename ProtocolPolicy::message_type> {
  using super = io::network::newb_acceptor<typename ProtocolPolicy::message_type>;

  quic_acceptor(default_multiplexer& dm, native_socket sockfd)
          : super(dm, sockfd) {
    // nop
  }

  expected<actor> create_newb(native_socket sockfd,
                              io::network::transport_policy_ptr pol) override {
    CAF_LOG_TRACE(CAF_ARG(sockfd));
    std::cout << "quic_acceptor::creating newb" << std::endl;
    auto n = io::network::make_newb<raw_newb>(this->backend().system(), sockfd);
    auto ptr = caf::actor_cast<caf::abstract_actor*>(n);
    if (ptr == nullptr)
      return sec::runtime_error;
    auto& ref = dynamic_cast<raw_newb&>(*ptr);
    ref.transport = std::move(pol);
    ref.protocol.reset(new ProtocolPolicy(&ref));
    ref.responder = responder;
    ref.configure_read(io::receive_policy::exactly(chunk_size));
    anon_send(responder, n);
    return n;
  }

  actor responder;
};*/

class config : public actor_system_config {
public:
  uint16_t port = 44444;
  std::string host = "localhost";
  bool is_server = false;

  config() {
    opt_group{custom_options_, "global"}
            .add(port, "port,p", "set port")
            .add(host, "host,h", "set host")
            .add(is_server, "server,s", "set server");
  }
};

struct state {
  size_t count = 0;
};

void caf_main(actor_system&, const config&) {
      /*using acceptor_t = quic_acceptor<quic_protocol<policy::raw>>;
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

  auto dummy_broker = [](io::stateful_broker<state>* self) -> behavior {
    return {
      [=](io::new_connection_msg& msg) {
          std::cout << "got new connection" << std::endl;
          self->configure_read(msg.handle, io::receive_policy::exactly(chunk_size));
      },
      [=](io::new_data_msg& msg) {
          // just print the new messages
          self->state.count += 1;
          std::cout << msg.buf.data() << std::endl;
      }
    };
  };

  auto name = cfg.is_server ? "server" : "client";
  auto helper = sys.spawn(init, name, self);

  actor nb;
  auto await_done = [&]() {
    self->receive(
      [&](quit_atom) {
          std::cout << "done" << std::endl;
      }
    );
  };
  if (cfg.is_server) {
    std::cout << "creating new server" << std::endl;
    auto server_ptr = make_server_newb<acceptor_t, accept_quic>(sys, port, nullptr,
                                                               true);
    // If I don't do this, our newb acceptor will never get events ...
    auto b = sys.middleman().spawn_server(dummy_broker, port + 1);

    std::string dummy;
    std::cout << "press [enter] to quit" << std::endl;
    getline(std::cin, dummy);
    //self->send(server_ptr, quit_atom::value);
    //await_done();
  } else {
    std::cout << "creating new client" << std::endl;
    auto client = make client<raw_newb, quic_transport,
            quic_protocol<policy::raw>>(sys, host, port);
    self->send(client, responder_atom::value, helper);

    std::string msg;
    while (getline(std::cin, msg)) {
      if(msg == "/quit") break;
      self->send(client, send_atom::value, msg);
    }
    self->send(client, quit_atom::value);
    await_done();
  }
  */
}

} // namespace anonymous

CAF_MAIN(io::middleman);
