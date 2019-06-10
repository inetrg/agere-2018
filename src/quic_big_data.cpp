#include "caf/all.hpp"
#include "caf/binary_deserializer.hpp"
#include "caf/binary_serializer.hpp"
#include "caf/detail/call_cfun.hpp"
#include "caf/io/newb.hpp"
#include "caf/logger.hpp"
#include "newb_quicly.hpp"
#include "caf/policy/newb_raw.hpp"
#include "caf/io/broker.hpp"
#include <unordered_map>

using namespace caf;
using namespace caf::io;
using namespace caf::io::network;
using namespace caf::policy;

namespace {

using start_atom = atom_constant<atom("start")>;
using prbs_atom = atom_constant<atom("prbs")>;
using send_atom = atom_constant<atom("send")>;
using quit_atom = atom_constant<atom("quit")>;
using responder_atom = atom_constant<atom("responder")>;
using config_atom = atom_constant<atom("config")>;

struct state {
  actor responder;
  uint64_t received;
};


enum bench_type {
  ONE,
  TEN,
  HUNDRED,
  THOUSAND
};


behavior server(stateful_newb<new_raw_msg, state>* self, actor responder, uint64_t length) {
  self->state.responder = responder;
  self->state.received = 0;
  auto stop = [=](std::string msg) {
    std::cerr << msg << std::endl;
    self->send(self->state.responder, quit_atom::value);
    self->stop();
    self->quit();
  };

  return {
    [=](new_raw_msg& msg) {
      self->state.received += msg.payload_len;
      if (self->state.received >= length) {
        auto whdl = self->wr_buf(nullptr);      
        whdl.buf->push_back(char('k'));
        stop("bench done");    
      }
    },
    [=](io_error_msg& msg) {
      stop("server got io error: " + to_string(msg.op));
    }
  };
}


behavior client(stateful_newb<new_raw_msg, state>* self, actor responder) {
  self->state.responder = responder;
  auto n = actor_clock::clock_type::now();
  auto timeout = std::chrono::milliseconds(25);
  self->clock().set_multi_timeout(n+timeout, self, io::transport_atom::value, 0);
  auto stop = [=](std::string msg) {
    std::cerr << msg << std::endl;
    self->send(self->state.responder, quit_atom::value);
    self->stop();
    self->quit();
  };

  return {
    [=](send_atom, std::vector<char>& buf) {
      auto whdl = self->wr_buf(nullptr);
      auto& wr_buf = *whdl.buf;

      wr_buf.insert(wr_buf.end(), buf.begin(), buf.end());
      return 0;
    },
    [=](new_raw_msg& msg) {
      auto& s = self->state;
      char recv = 0;
      binary_deserializer bd(self->system(), msg.payload, msg.payload_len);
      bd(recv);
      if (recv == 'k') {
        stop("test done");
      }
      return 0;
    },
    [=](io_error_msg& msg) {
      stop("client got io error");
      return 0;
    }
  };
}

class config : public actor_system_config {
public:
  uint16_t port = 4433;
  std::string host = "localhost";
  bool is_server = false;
  bool prbs = false;
  std::string bench_type = "1M";

  config() {
    add_message_type<std::vector<char>>("std::vector<char>");
    opt_group{custom_options_, "global"}
            .add(port, "port,P", "set port")
            .add(host, "host,H", "set host")
            .add(is_server, "server,s", "set server")
            .add(bench_type, "bench_type,b", "type of benchmark [1M|10M|100M|1G]");
  }
};

void caf_main(actor_system& sys, const config& cfg) {
  using namespace std::chrono;
  using proto_t = quicly_protocol<raw>;
  const char* host = cfg.host.c_str();
  const uint16_t port = cfg.port;
  scoped_actor self{sys};

  auto await_done = [&](std::string msg) {
    self->receive([&](quit_atom) {
      std::cerr << msg << std::endl;
    });
  };

  std::unordered_map<std::string, bench_type> types {
    {"1M", ONE},
    {"10M", TEN},
    {"100M", HUNDRED},
    {"1G", THOUSAND}
  };

  std::string file_name = "";
  char* path = getenv("DATA_PATH");
  if(!path) {
    std::cerr << "data_path env var not specified" << std::endl;
    return;
  }
  std::string data_path(path);

  uint64_t file_length = 0;
  switch (types.at(cfg.bench_type)) {
    case ONE:
      file_name = data_path + "/1M-file";
      file_length = 1048576;
      break;
    case TEN:
      file_name = data_path + "/10M-file";
      file_length = 10485760;
      break;
    case HUNDRED:
      file_name = data_path + "/100M-file";
      file_length = 104857600;
      break;
    case THOUSAND:
      file_name = data_path + "/1G-file";
      file_length = 1073741824;
      break;
    default:
      std::cerr << "specify a known input." << std::endl;
      return;
  }

  if (cfg.is_server) {
    std::cerr << "creating server on port " << cfg.port << std::endl;
    accept_ptr<policy::new_raw_msg> pol{new accept_quicly<policy::new_raw_msg>};
    auto eserver = spawn_server<proto_t>(sys, server, std::move(pol), port,
                                          nullptr, true, self, file_length);
    if (!eserver) {
      std::cerr << "failed to start server on port " << port << std::endl;
      return;
    }
    auto server = std::move(*eserver);
    await_done("done");
    std::cerr << "stopping server" << std::endl;
    self->send_exit(server, caf::exit_reason::user_shutdown);
  } else {
    std::cerr << "creating client" << std::endl;
    transport_ptr pol{new quicly_transport};
    auto eclient = spawn_client<proto_t>(sys, client, std::move(pol),
                                          host, port, self);
    if (!eclient) {
      std::cerr << "failed to start client for " << host << ":" << port
                << std::endl;
      return;
    }
    auto client = std::move(*eclient);
    auto start = system_clock::now();

    std::ifstream file(file_name);
    if (!file.is_open()) {
      std::cerr << "could not open file!!" << std::endl;
      return;
    }
          
    std::vector<char> buf;
    buf.resize(1024);
    while(file.read(buf.data(), buf.size())) {
      self->send(client, send_atom::value, buf);
      self->receive([&](int x) {});
    }

    await_done("done");
    auto end = system_clock::now();
    std::cout << duration_cast<milliseconds>(end - start).count() << "ms"
              << std::endl;
    self->send(client, exit_reason::user_shutdown);
  }
}

} // namespace anonymous

CAF_MAIN(io::middleman);
