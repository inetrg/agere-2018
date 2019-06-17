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
  uint64_t received;
  uint64_t file_length;
  connection_handle other;
  uint64_t sent;
};


enum bench_type {
  ONE,
  TEN,
  HUNDRED,
  THOUSAND
};

behavior raw_server(stateful_newb<new_raw_msg, state>* self, actor responder, uint64_t length) {
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
      std::cerr << "received " << self->state.received << " Bytes" << std::endl;
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

behavior raw_client(stateful_newb<new_raw_msg, state>* self, actor responder) {
  self->state.responder = responder;
  self->state.sent = 0;
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

      self->state.sent += buf.size();
      std::cerr << "wrote " << self->state.sent << " Bytes" << std::endl;
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
  size_t messages = 12345;
  std::string host = "127.0.0.1";
  uint16_t port = 12345;
  bool is_server = false;
  bool is_ordered = false;
  std::string bench_type = "1M";

  config() {
    opt_group{custom_options_, "global"}
    .add(messages,   "messages,m", "set number of exchanged messages")
    .add(host,       "host,H",     "set host")
    .add(port,       "port,P",     "set port")
    .add(is_ordered, "ordered,o",  "use ordered UDP")
    .add(is_server,  "server,s",   "set server")
    .add(bench_type, "bench_type,b", "type of benchmark [1M|10M|100M|1G]");
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
    std::cerr << "creating server" << std::endl;
    accept_ptr<policy::new_raw_msg> pol{new accept_udp<policy::new_raw_msg>};
    if (cfg.is_ordered) {
      std::cerr << "ordered" << std::endl;
      auto eserver = spawn_server<ordered_proto_t>(sys, raw_server, std::move(pol),
                                                  port, nullptr, true, self, file_length);
      if (!eserver) {
        std::cerr << "failed to start server on port " << port << std::endl;
        return;
      }
      auto server = std::move(*eserver);
      await_done("done");
      std::cerr << "stopping server" << std::endl;
      self->send(server, exit_reason::user_shutdown);

    } else {
      auto eserver = spawn_server<proto_t>(sys, raw_server, std::move(pol), port,
                                          nullptr, true, self, file_length);
      if (!eserver) {
        std::cerr << "failed to start server on port " << port << std::endl;
        return;
      }
      auto server = std::move(*eserver);
      await_done("done");
      std::cerr << "stopping server" << std::endl;
      self->send(server, exit_reason::user_shutdown);

    }
  } else {
    std::cerr << "creating client" << std::endl;
    transport_ptr pol{new udp_transport};
    if (cfg.is_ordered) {
      auto eclient = spawn_client<ordered_proto_t>(sys, raw_client, std::move(pol),
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
    } else {
      auto eclient = spawn_client<proto_t>(sys, raw_client, std::move(pol), host,
                                           port, self);
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
    }
  }
  std::cout << "I bims hier" << std::endl;
}

} // namespace anonymous

CAF_MAIN(io::middleman);
