#include <caf/all.hpp>
#include <caf/binary_deserializer.hpp>
#include <caf/binary_serializer.hpp>
#include <caf/detail/call_cfun.hpp>
#include <caf/io/network/newb.hpp>
#include <caf/logger.hpp>
#include <caf/policy/newb_basp.hpp>
#include <caf/policy/newb_ordering.hpp>
#include <caf/policy/newb_udp.hpp>

#include <benchmark/benchmark.h>

using namespace caf;
using namespace caf::policy;
using namespace caf::io::network;

namespace {

using ordering_atom = atom_constant<atom("ordering")>;

constexpr size_t chunk_size = 1024; //8192; //128; //1024;

struct dummy_transport : public transport_policy {
  dummy_transport()
    : maximum(std::numeric_limits<uint16_t>::max()),
      writing(false),
      written(0),
      offline_sum(0) {
    // nop
  }

  inline error read_some(event_handler*) override {
    return none;
  }

  inline bool should_deliver() override {
    return true;
  }

  void prepare_next_read(event_handler*) override {
    received_bytes = 0;
    receive_buffer.resize(maximum);
  }

  inline void configure_read(io::receive_policy::config) override {
    // nop
  }

  inline error write_some(event_handler* parent) override {
    written += send_sizes.front();
    send_sizes.pop_front();
    auto remaining = send_buffer.size() - written;
    count += 1;
    if (remaining == 0)
      prepare_next_write(parent);
    return none;
  }

  void prepare_next_write(event_handler*) override {
    written = 0;
    send_buffer.clear();
    send_sizes.clear();
    if (offline_buffer.empty()) {
      writing = false;
    } else {
      offline_sizes.push_back(offline_buffer.size() - offline_sum);
       // Switch buffers.
      send_buffer.swap(offline_buffer);
      send_sizes.swap(offline_sizes);
      // Reset sum.
      offline_sum = 0;
    }
  }

  byte_buffer& wr_buf() override {
    if (!offline_buffer.empty()) {
      auto chunk_size = offline_buffer.size() - offline_sum;
      offline_sizes.push_back(chunk_size);
      offline_sum += chunk_size;
    }
    return offline_buffer;
  }

  void flush(event_handler* parent) override {
    if (!offline_buffer.empty() && !writing) {
      writing = true;
      prepare_next_write(parent);
    }
  }

  expected<native_socket>
  connect(const std::string&, uint16_t,
          optional<protocol::network> = none) override {
    return invalid_native_socket;
  }

  // State for reading.
  size_t maximum;

  // State for writing.
  bool writing;
  size_t written;
  size_t offline_sum;
  std::deque<size_t> send_sizes;
  std::deque<size_t> offline_sizes;
};


struct raw_newb : public newb<new_basp_message> {
  using message_type = new_basp_message;

  raw_newb(caf::actor_config& cfg, default_multiplexer& dm,
            native_socket sockfd)
      : newb<message_type>(cfg, dm, sockfd) {
    // nop
    CAF_LOG_TRACE("");
  }

  void handle(message_type&) override {
    CAF_PUSH_AID_FROM_PTR(this);
    CAF_LOG_TRACE("");
  }

  behavior make_behavior() override {
    set_default_handler(print_and_drop);
    return {
      // Must be implemented at the moment, will be cought by the broker in a
      // later implementation.
      [=](atom_value atm, uint32_t id) {
        protocol->timeout(atm, id);
      }
    };
  }
};

class config : public actor_system_config {
public:
  int iterations = 1;

  config() {
    load<io::middleman>();
    opt_group{custom_options_, "global"}
    .add(iterations, "iterations,i", "set iterations");
  }
};



static void BM_policy_send_cost(benchmark::State& state) {
  config cfg;
  actor_system sys{cfg};
  auto n = make_newb<raw_newb>(sys, invalid_native_socket);
  auto ptr = caf::actor_cast<caf::abstract_actor*>(n);
  auto& ref = dynamic_cast<raw_newb&>(*ptr);
  ref.transport.reset(new dummy_transport);
  ref.protocol.reset(new udp_protocol<ordering<datagram_basp>>(&ref));
  for (auto _ : state) {
    auto hw = caf::make_callback([&](byte_buffer& buf) -> error {
      binary_serializer bs(sys, buf);
      bs(basp_header{0, actor_id{}, actor_id{}});
      return none;
    });
    {
      auto whdl = ref.wr_buf(&hw);
      CAF_ASSERT(whdl.buf != nullptr);
      CAF_ASSERT(whdl.protocol != nullptr);
      binary_serializer bs(sys, *whdl.buf);
      auto start = whdl.buf->size();
      whdl.buf->resize(start + chunk_size);
      std::fill(whdl.buf->begin() + start, whdl.buf->end(), 'a');
    }
    ref.write_event();
  }
}

BENCHMARK(BM_policy_send_cost);

/*
void caf_main(actor_system& sys, const config& cfg) {
  using clock = std::chrono::system_clock;
  using resolution = std::chrono::milliseconds;
  auto n = make_newb<raw_newb>(sys, invalid_native_socket);
  auto ptr = caf::actor_cast<caf::abstract_actor*>(n);
  auto& ref = dynamic_cast<raw_newb&>(*ptr);
  ref.transport.reset(new dummy_transport);
  ref.protocol.reset(new udp_protocol<ordering<datagram_basp>>(&ref));
  auto start = clock::now();
  for (int i = 0; i < cfg.iterations; ++i) {
    auto hw = caf::make_callback([&](byte_buffer& buf) -> error {
      binary_serializer bs(sys, buf);
      bs(basp_header{0, actor_id{}, actor_id{}});
      return none;
    });
    {
      auto whdl = ref.wr_buf(&hw);
      CAF_ASSERT(whdl.buf != nullptr);
      CAF_ASSERT(whdl.protocol != nullptr);
      binary_serializer bs(sys, *whdl.buf);
      auto start = whdl.buf->size();
      whdl.buf->resize(start + chunk_size);
      std::fill(whdl.buf->begin() + start, whdl.buf->end(), 'a');
    }
    ref.write_event();
  }
  auto end = clock::now();
  auto ticks = std::chrono::duration_cast<resolution>(end - start).count();
  std::cout << cfg.iterations << ", " << ticks << std::endl;
}
*/

} // namespace anonymous

BENCHMARK_MAIN();
//CAF_MAIN(io::middleman);
