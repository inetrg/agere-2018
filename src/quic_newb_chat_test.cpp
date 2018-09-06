
#include "caf/all.hpp"
#include "caf/binary_deserializer.hpp"
#include "caf/binary_serializer.hpp"
#include "caf/detail/call_cfun.hpp"
#include "caf/io/network/newb.hpp"
#include "caf/logger.hpp"
#include "caf/policy/newb_raw.hpp"
#include "../caf/policy/newb_quic.hpp"

using namespace caf;

using caf::io::network::default_multiplexer;
using caf::io::network::invalid_native_socket;
using caf::io::network::make_client_newb;
using caf::io::network::make_server_newb;
using caf::io::network::native_socket;
using caf::policy::accept_quic;
using caf::policy::quic_protocol;
using caf::policy::quic_transport;

namespace {

    using send_atom = atom_constant<atom("send")>;
    using quit_atom = atom_constant<atom("quit")>;

    constexpr size_t chunk_size = 8192; //128; //8192; //1024;

    struct raw_newb : public io::network::newb<policy::raw_data_message> {
        using message_type = policy::raw_data_message;

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
                  [=](send_atom, std::string s) {
                    if (running) {
                      auto whdl = wr_buf(nullptr);
                      CAF_ASSERT(whdl.buf != nullptr);
                      CAF_ASSERT(whdl.protocol != nullptr);
                      binary_serializer bs(&backend(), *whdl.buf);
                      whdl.buf->resize(s.size());
                      memcpy(whdl.buf->data(), s.c_str(), s.size());
                    }
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
        // values: measurement point, current interval, messages sent in interval, offline buffer size
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
    };

    class config : public actor_system_config {
    public:
        uint16_t port = 44444;
        std::string host = "127.0.0.1";
        bool is_server = false;

        config() {
          opt_group{custom_options_, "global"}
                  .add(port, "port,P", "set port")
                  .add(host, "host,H", "set host")
                  .add(is_server, "server,s", "set server");
        }
    };

    struct state {
        size_t count = 0;
    };

    void caf_main(actor_system& sys, const config& cfg) {
      const char *host = cfg.host.c_str();
      const uint16_t port = cfg.port;
      scoped_actor self{sys};

      actor nb;
      auto await_done = [&]() {
          self->receive(
                  [&](quit_atom) {
                      std::cout << "done" << std::endl;
                  }
          );
      };

      std::cout << "creating new client" << std::endl;
      auto client = make_client_newb<raw_newb, quic_transport,
              quic_protocol<policy::raw>>(sys, host, port);

      std::cout << "give some input" << std::endl;
      std::string msg;
      while (getline(std::cin, msg)) {
        if (msg == "/quit")
          break;
        self->send(client, send_atom::value, msg);
      }
      self->send(client, quit_atom::value);
      await_done();
    }
} // namespace anonymous

CAF_MAIN(io::middleman);
