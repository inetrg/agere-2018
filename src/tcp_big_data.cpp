#include "caf/all.hpp"
#include "caf/binary_deserializer.hpp"
#include "caf/binary_serializer.hpp"
#include "caf/detail/call_cfun.hpp"
#include "caf/io/newb.hpp"
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
using init_atom = atom_constant<atom("init")>;

struct state {
  actor responder;
  uint64_t received;
  uint64_t file_length;
  connection_handle other;
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


behavior tcp_server(stateful_broker<state>* self) {
  auto stop = [=](std::string msg) {
    std::cerr << msg << std::endl;
    self->quit();
    self->send(self->state.responder, quit_atom::value);
  };

  return {
    [=](init_atom, actor responder, uint64_t length) {
      std::cerr << "init" << std::endl;
      self->state.responder = responder;
      self->state.file_length = length;
      self->state.received = 0;
    },
    [=](const new_connection_msg& msg) {
      std::cerr << "new connection" << std::endl;
      self->state.other = msg.handle;
      self->configure_read(msg.handle,
                           io::receive_policy::at_most(1024));
    },
    [=](new_data_msg& msg) {
      std::cerr << "received " << msg.buf.size() << " Bytes" << std::endl;
      self->state.received += msg.buf.size();
      std::cerr << "state.received = " << self->state.received << std::endl;
      if (self->state.received >= self->state.file_length) {
        char response = 'k';
        self->write(msg.handle, 1, &response);
        self->flush(msg.handle);
        stop("bench done");    
      }
    },
    [=](const connection_closed_msg&) {
      stop("server got io error");
    }
  };
}

behavior tcp_client(stateful_broker<state>* self, connection_handle hdl) {
  self->state.other = hdl;
  self->configure_read(hdl,
                           io::receive_policy::at_most(1024));
  auto stop = [=](std::string msg) {
    std::cerr << msg << std::endl;
    self->quit();
    self->send(self->state.responder, quit_atom::value);
  };

  return {
    [=](init_atom, actor responder) {
      self->state.responder = responder;
      return 0;
    },
    [=](send_atom, std::vector<char>& buf) {
      std::cerr << "sending " << buf.size() << " bytes" << std::endl;
      auto& s = self->state;
      self->write(s.other, buf.size(), buf.data());
      self->flush(s.other);
      return 0;
    },
    [=](new_data_msg& msg) {
      if (msg.buf[0] == 'k') {
        stop("test done");
      }
      return 0;
    },
    [=](const connection_closed_msg&) {
      stop("client got io error");
    }
  };
}



class config : public actor_system_config {
public:
  uint16_t port = 12345;
  std::string host = "localhost";
  bool is_server = false;
  bool prbs = false;
  std::string bench_type = "1M";
  bool traditional = false;

  config() {
    add_message_type<std::vector<char>>("std::vector<char>");
    opt_group{custom_options_, "global"}
            .add(port, "port,P", "set port")
            .add(host, "host,H", "set host")
            .add(is_server, "server,s", "set server")
            .add(bench_type, "bench_type,b", "type of benchmark [1M|10M|100M|1G]")
            .add(traditional, "traditional,t", "use traditional style brokers");  
  }
};

void caf_main(actor_system& sys, const config& cfg) {
  using namespace std::chrono;
  using proto_t = tcp_protocol<raw>;
  //using acceptor_t = tcp_acceptor<proto_t>;
  const char* host = cfg.host.c_str();
  const uint16_t port = cfg.port;
  scoped_actor self{sys};

  auto await_done = [&](std::string msg) {
    self->receive(
      [&](quit_atom) {
        std::cerr << msg << std::endl;
      }
    );
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

  if (!cfg.traditional) {
    if (cfg.is_server) {
      std::cerr << "creating server" << std::endl;
      accept_ptr<policy::new_raw_msg> pol{new accept_tcp<policy::new_raw_msg>};
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
      std::this_thread::sleep_for(std::chrono::seconds(1));
    } else {
      std::cerr << "creating client" << std::endl;
      transport_ptr pol{new tcp_transport};
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
      //self->send(client, exit_reason::user_shutdown);
    }
  } else {
    if (cfg.is_server) {
      std::cerr << "creating traditional server" << std::endl;
      auto es = sys.middleman().spawn_server(tcp_server, port);
      self->send(*es, init_atom::value, self, file_length);
      await_done("done");
    } else {
      std::cerr << "creating traditional client" << std::endl;
      auto ec = sys.middleman().spawn_client(tcp_client, host, port);
      std::cerr << "created traditional client" << std::endl;
      auto start = system_clock::now();
      
      std::cerr << "sending init atom" << std::endl;
      self->send(*ec, init_atom::value, actor_cast<actor>(self));
      self->receive([=](int i) {});
      std::cerr << "sent init atom" << std::endl;

      std::ifstream file(file_name);
      if (!file.is_open()) {
        std::cerr << "could not open file!!" << std::endl;
        return;
      }
      std::cerr << "file opened" << std::endl;
            
      std::vector<char> buf;
      buf.resize(1024);
      while(file.read(buf.data(), buf.size())) {
        self->send(*ec, send_atom::value, buf);
        self->receive([&](int x) {});
      }
      
      await_done("done");
      auto end = system_clock::now();
      std::cout << duration_cast<milliseconds>(end - start).count() << "ms"
                << std::endl;
    }
  }
}

} // namespace anonymous

CAF_MAIN(io::middleman);
