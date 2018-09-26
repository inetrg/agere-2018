#include <chrono>
#include <iostream>

#include <caf/all.hpp>

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <netinet/tcp.h>

using namespace caf;
using namespace std;
using namespace std::chrono;

namespace {

class config : public actor_system_config {
public:
  uint16_t port = 12345;
  std::string host = "127.0.0.1";
  bool is_server = false;
  uint32_t messages = 10000;
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

void tcp_nodelay(int fd, bool new_value) {
  int flag = new_value ? 1 : 0;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
             reinterpret_cast<const void*>(&flag),
             static_cast<unsigned>(sizeof(flag)));
}

void caf_main(actor_system& sys, const config& cfg) {
  const size_t buf_size = 256;
  if (!cfg.is_server) {
    const char* host = cfg.host.c_str();
    const uint16_t port = cfg.port;
    uint32_t received_messages = 0;
    int sockfd, n;
    struct sockaddr_in serveraddr;
    struct hostent *server;
    std::vector<char> send_buf;
    std::vector<char> recv_buf(buf_size);
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
      std::cerr << "ERROR opening socket" << std::endl;
      return;
    }
    server = gethostbyname(host);
    if (server == NULL) {
      std::cerr << "ERROR, no such host as " << host << std::endl;
      return;
    }
    bzero((char*) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    bcopy((char*)server->h_addr,
    (char*)&serveraddr.sin_addr.s_addr, server->h_length);
    serveraddr.sin_port = htons(port);
    if (connect(sockfd, (struct sockaddr*)&serveraddr, sizeof(serveraddr)) < 0) {
      std::cerr << "ERROR connecting" << std::endl;
      return;
    }
    tcp_nodelay(sockfd, true);
    auto start = system_clock::now();
    while (received_messages < cfg.messages) {
      send_buf.clear();
      binary_serializer bs(sys, send_buf);
      bs(received_messages);
      n = write(sockfd, send_buf.data(), send_buf.size());
      if (n < 0) {
        std::cerr << "ERROR writing to socket: " << strerror(errno) << std::endl;
        return;
      }
      n = read(sockfd, recv_buf.data(), recv_buf.size());
      if (n < 0) {
        std::cerr << "ERROR reading from socket: " << strerror(errno) << std::endl;
        return;
      }
      received_messages += 1;
      if (received_messages % 100 == 0)
        std::cerr << "got " << received_messages << std::endl;
    }
    std::cout << "got all messages!" << std::endl;
    auto end = system_clock::now();
    std::cout << duration_cast<milliseconds>(end - start).count() << "ms" << std::endl;
    close(sockfd);
  } else {
    const char* host = "0.0.0.0";
    const uint16_t port = cfg.port;
    int num_bytes = 0;
    unsigned addr_size;
    int socket_fd, accept_fd;
    char data_buffer[buf_size];
    struct sockaddr_in sa, isa;
    socket_fd = socket(PF_INET, SOCK_STREAM, 0);
    tcp_nodelay(socket_fd, true);
    if (socket_fd < 0)  {
      std::cerr << "socket call failed" << std::endl;
      exit(0);
    }
    memset(&sa, 0, sizeof(struct sockaddr_in));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = inet_addr(host);
    sa.sin_port = htons(port);
    if (::bind(socket_fd, (struct sockaddr*)&sa, sizeof(sa)) == -1) {
      std::cerr << "bind to port '" << port << "', IP address '" << host << "' failed" << std::endl;
      close(socket_fd);
      exit(1);
    }
    listen(socket_fd, 5);
    for (;;) {
      addr_size = sizeof(isa);
      std::cerr << "awaiting new client" << std::endl;
      accept_fd = accept(socket_fd, (struct sockaddr*) &isa, &addr_size);
      tcp_nodelay(accept_fd, true);
      if (accept_fd < 0) {
        std::cerr << "accept_event failed" << std::endl;
        close(socket_fd);
        exit(2);
      }
      for (;;) {
        num_bytes = recv(accept_fd, data_buffer, buf_size, 0);
        if (num_bytes == 0) {
          std::cerr << "client shut down" << std::endl;
          break;
        } else if (num_bytes < 0) {
          std::cerr << "recv error: "  << strerror(errno) << std::endl;
          break;
        }
        num_bytes = send(accept_fd, data_buffer, num_bytes, 0);
        if (num_bytes < 0) {
          std::cerr << "send error: " << strerror(errno) << std::endl;
          break;
        }
      }
      close(accept_fd);
    }
    close(socket_fd);
  }
}

} // namespace anonymous

CAF_MAIN()
