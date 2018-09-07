#include "caf/all.hpp"
#include "caf/binary_deserializer.hpp"
#include "caf/binary_serializer.hpp"
#include "caf/detail/call_cfun.hpp"
#include "caf/io/network/newb.hpp"
#include "caf/logger.hpp"
#include "caf/policy/newb_basp.hpp"
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

    struct basp_newb : public io::network::newb<policy::new_basp_msg> {
        using message_type = policy::new_basp_msg;

        basp_newb(caf::actor_config& cfg, default_multiplexer& dm,
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
                      if (msg.payload_len == 1) {
                        // nop
                      } else {
                        received_messages += 1;
                        if (received_messages % 1000 == 0)
                          std::cout << "received " << received_messages << " messages" << std::endl;
                        // nop
                      }
                  },
                  [=](send_atom, std::string s) {
                      if (running) {
                        auto hw = caf::make_callback([&](io::network::byte_buffer& buf) -> error {
                            binary_serializer bs(&backend(), buf);
                            bs(policy::basp_header{0, id(), actor_id{}});
                            return none;
                        });
                        auto whdl = wr_buf(&hw);
                        CAF_ASSERT(whdl.buf != nullptr);
                        CAF_ASSERT(whdl.protocol != nullptr);
                        binary_serializer bs(&backend(), *whdl.buf);
                        whdl.buf->resize(s.size());
                        memcpy(whdl.buf->data(), s.c_str(), s.size());
                        std::cout << "debug info:\n"
                                  << s << "\n" << whdl.buf->data() << std::endl;
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
          std::cout << "creating newb" << std::endl;
          auto n = io::network::make_newb<basp_newb>(this->backend().system(), sockfd);
          auto ptr = caf::actor_cast<caf::abstract_actor*>(n);
          if (ptr == nullptr)
            return sec::runtime_error;
          auto& ref = dynamic_cast<basp_newb&>(*ptr);
          ref.transport = std::move(pol);
          ref.protocol.reset(new ProtocolPolicy(&ref));
          ref.responder = responder;
          ref.configure_read(io::receive_policy::exactly(policy::basp_header_len));
          anon_send(responder, n);
          return n;
        }

        actor responder;
    };

    class config : public actor_system_config {
    public:
        uint16_t port = 4434;
        std::string host = "127.0.0.1";
        bool is_server = false;

        config() {
          opt_group{custom_options_, "global"}
                  .add(port, "port,P", "set port")
                  .add(host, "host,H", "set host")
                  .add(is_server, "server,s", "set server");
        }
    };

    void caf_main(actor_system& sys, const config& cfg) {
      const char* host = cfg.host.c_str();
      const uint16_t port = cfg.port;
      scoped_actor self{sys};

      actor nb;
      std::cout << "creating new client" << std::endl;
      auto client = make_client_newb<basp_newb, quic_transport,
              quic_protocol<policy::stream_basp>>(sys, host, port);

      std::string msg;
      while (getline(std::cin, msg)) {
        if (msg == "/quit")
          break;
        self->send(client, send_atom::value, msg);
      }
      self->send(client, quit_atom::value);
    }

} // namespace anonymous

CAF_MAIN(io::middleman);
