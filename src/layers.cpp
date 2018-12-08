#include <caf/all.hpp>
#include <caf/binary_deserializer.hpp>
#include <caf/binary_serializer.hpp>
#include <caf/detail/call_cfun.hpp>
#include <caf/io/newb.hpp>
#include <caf/logger.hpp>
#include <caf/policy/newb_basp.hpp>
#include <caf/policy/newb_ordering.hpp>
#include <caf/policy/newb_raw.hpp>
#include <caf/policy/newb_tcp.hpp>
#include <caf/policy/newb_udp.hpp>

#include <benchmark/benchmark.h>

using namespace caf;
using namespace caf::io;
using namespace caf::io::network;
using namespace caf::policy;

namespace {

using ordering_atom = atom_constant<atom("ordering")>;

constexpr auto from = 6;
constexpr auto to = 13;

// Receiving is currently datagram only.
struct dummy_transport : public transport {
  dummy_transport(size_t payload_len)
    : maximum(std::numeric_limits<uint16_t>::max()),
      writing(false),
      written(0),
      offline_sum(0),
      write_seq(false),
      write_size(false),
      next(0),
      payload_len(payload_len),
      upayload_len(static_cast<uint32_t>(payload_len)) {
    max_consecutive_reads = 1;
  }

  inline rw_state read_some(newb_base* parent) override {
    received_bytes = payload_len;
    stream_serializer<charbuf> out{&parent->backend(),
                                   receive_buffer.data(),
                                   sizeof(next) + sizeof(upayload_len)};
    if (write_seq) {
      out(next);
      next += 1;
      received_bytes += caf::policy::ordering_header_len;
    }
    if (write_size) {
      out(upayload_len);
      received_bytes += caf::policy::basp_header_len;
    }
    return rw_state::success;
  }

  inline bool should_deliver() override {
    return true;
  }

  void prepare_next_read(newb_base*) override {
    received_bytes = 0;
    receive_buffer.resize(maximum);
  }

  inline void configure_read(io::receive_policy::config) override {
    // nop
  }

  inline rw_state write_some(newb_base* parent) override {
    written += send_sizes.front();
    send_sizes.pop_front();
    auto remaining = send_buffer.size() - written;
    if (remaining == 0)
      prepare_next_write(parent);
    return rw_state::success;
  }

  void prepare_next_write(newb_base*) override {
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

  void flush(newb_base* parent) override {
    if (!offline_buffer.empty() && !writing) {
      writing = true;
      prepare_next_write(parent);
    }
  }

  expected<native_socket>
  connect(const std::string&, uint16_t,
          optional<network::protocol::network> = none) override {
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

  // Some moocks for receiving packets.
  bool write_seq;
  bool write_size;
  sequence_type next;
  size_t payload_len;
  uint32_t upayload_len;
};

struct dummy_state {
  bool received;
};

template <class Message>
behavior dummy_newb(stateful_newb<Message, dummy_state>* self) {
  self->set_default_handler(print_and_drop);
  self->state.received = false;
  self->set_timeout_handler([&](timeout_msg&) {
    // Drop timeouts.
  });
  return {
    [=](const Message&) {
      self->state.received = true;
    }
  };
}

class config : public actor_system_config {
public:
  config() {
    load<io::middleman>();
    set("scheduler.policy", atom("testing"));
    set("scheduler.max-threads", 1);
    set("logger.inline-output", true);
    set("middleman.manual-multiplexing", true);
    set("middleman.attach-utility-actors", true);
    set("middleman.max-pending-messages", 5);
  }
};

// -- sending ------------------------------------------------------------------

template <class Message, class Protocol>
static void BM_send(benchmark::State& state) {
  config cfg;
  actor_system sys{cfg};
  size_t packet_size = static_cast<size_t>(state.range(0));
  transport_ptr trans{new dummy_transport(packet_size)};
  caf::io::network::native_socket sock(1337);
  auto n = spawn_newb<Protocol, hidden>(sys, dummy_newb<Message>,
                                        std::move(trans), sock, false);
  auto ptr = caf::actor_cast<caf::abstract_actor*>(n);
  auto& ref = dynamic_cast<newb<Message>&>(*ptr);
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
      whdl.buf->resize(start + packet_size);
      std::fill(whdl.buf->begin() + start, whdl.buf->end(), 'a');
    }
    ref.write_event();
  }
  ref.stop();
}

BENCHMARK_TEMPLATE(BM_send, new_raw_msg, tcp_protocol<raw>)
  ->RangeMultiplier(2)->Range(1<<from,1<<to);
BENCHMARK_TEMPLATE(BM_send, new_basp_msg, tcp_protocol<stream_basp>)
  ->RangeMultiplier(2)->Range(1<<from,1<<to);

BENCHMARK_TEMPLATE(BM_send, new_raw_msg, udp_protocol<raw>)
  ->RangeMultiplier(2)->Range(1<<from,1<<to);
BENCHMARK_TEMPLATE(BM_send, new_raw_msg, udp_protocol<ordering<raw>>)
  ->RangeMultiplier(2)->Range(1<<from,1<<to);
BENCHMARK_TEMPLATE(BM_send, new_basp_msg, udp_protocol<datagram_basp>)
  ->RangeMultiplier(2)->Range(1<<from,1<<to);
BENCHMARK_TEMPLATE(BM_send, new_basp_msg, udp_protocol<ordering<datagram_basp>>)
  ->RangeMultiplier(2)->Range(1<<from,1<<to);

// -- receiving ----------------------------------------------------------------

template <class Message, class Protocol>
static void BM_receive_impl(benchmark::State& state, bool wseq, bool wsize) {
  config cfg;
  actor_system sys{cfg};
  caf::io::network::native_socket sock(1337);
  auto tptr = new dummy_transport(state.range(0));
  transport_ptr trans{tptr};
  auto n = spawn_newb<Protocol, hidden>(sys, dummy_newb<Message>,
                                        std::move(trans), sock, false);
  auto ptr = caf::actor_cast<caf::abstract_actor*>(n);
  auto& ref = dynamic_cast<stateful_newb<Message, dummy_state>&>(*ptr);
  tptr->write_seq = wseq;
  tptr->write_size = wsize;
  // prepare receive buffer
  size_t packet_size = state.range(0);
  auto hw = caf::make_callback([&](byte_buffer& buf) -> error {
    binary_serializer bs(sys, buf);
    bs(basp_header{0, actor_id{}, actor_id{}});
    return none;
  });
  {
    auto whdl = ref.wr_buf(&hw);
    auto start = whdl.buf->size();
    whdl.buf->resize(start + packet_size);
    std::fill(whdl.buf->begin() + start, whdl.buf->end(), 'a');
  }
  ref.trans->receive_buffer = ref.trans->send_buffer;
  for (auto _ : state) {
    ref.state.received = false;
    while (!ref.state.received)
      ref.read_event();
  }
  ref.stop();
}

static void BM_receive_udp_raw(benchmark::State& state) {
  BM_receive_impl<new_raw_msg, udp_protocol<raw>>(state, false, false);
}

static void BM_receive_udp_ordering_raw(benchmark::State& state) {
  BM_receive_impl<new_raw_msg, udp_protocol<ordering<raw>>>(state, true, false);
}

static void BM_receive_udp_basp(benchmark::State& state) {
  BM_receive_impl<new_basp_msg, udp_protocol<datagram_basp>>(state, false, true);
}

static void BM_receive_udp_ordering_basp(benchmark::State& state) {
  BM_receive_impl<new_basp_msg, udp_protocol<ordering<datagram_basp>>>(state, true, true);
}

static void BM_receive_tcp_raw(benchmark::State& state) {
  BM_receive_impl<new_raw_msg, tcp_protocol<raw>>(state, false, false);
}

static void BM_receive_tcp_basp(benchmark::State& state) {
  BM_receive_impl<new_basp_msg, tcp_protocol<stream_basp>>(state, false, true);
}

BENCHMARK(BM_receive_udp_raw)->RangeMultiplier(2)->Range(1<<from, 1<<to);
BENCHMARK(BM_receive_udp_ordering_raw)->RangeMultiplier(2)->Range(1<<from, 1<<to);
BENCHMARK(BM_receive_udp_basp)->RangeMultiplier(2)->Range(1<<from, 1<<to);
BENCHMARK(BM_receive_udp_ordering_basp)->RangeMultiplier(2)->Range(1<<from, 1<<to);

BENCHMARK(BM_receive_tcp_raw)->RangeMultiplier(2)->Range(1<<from, 1<<to);
BENCHMARK(BM_receive_tcp_basp)->RangeMultiplier(2)->Range(1<<from, 1<<to);

// -- ordering -----------------------------------------------------------------

enum instruction : int {
  next,
  skip,
  recover,
};

// For the ordering test
struct dummy_ordering_transport : public transport {
  dummy_ordering_transport()
    : maximum(std::numeric_limits<uint16_t>::max()),
      writing(false),
      written(0),
      offline_sum(0),
      index(0),
      next_seq(0) {
    max_consecutive_reads = 1;
  }

  inline rw_state read_some(newb_base* parent) override {
    stream_serializer<charbuf> out{&parent->backend(),
                                   receive_buffer.data(),
                                   sizeof(next_seq)};
    switch (instructions[index]) {
      case skip:
        //std::cerr << " skip (" << next_seq << ")" << std::endl;
        skipped.push_back(next_seq);
        next_seq += 1;
        // fall through
      case next:
        //std::cerr << " next (" << next_seq << ")" << std::endl;
        out(next_seq);
        next_seq += 1;
        break;
      case recover:
        //std::cerr << " recover (" << skipped.front() << ")" << std::endl;
        out(skipped.front());
        skipped.pop_front();
        break;
    }
    received_bytes = payload_len + caf::policy::ordering_header_len;
    index += 1;
    if (index >= instructions.size())
      index = 0;
    return rw_state::success;
  }

  inline bool should_deliver() override {
    return true;
  }

  void prepare_next_read(newb_base*) override {
    //received_bytes = 0;
    //receive_buffer.resize(maximum);
  }

  inline void configure_read(io::receive_policy::config) override {
    // nop
  }

  inline rw_state write_some(newb_base* parent) override {
    written += send_sizes.front();
    send_sizes.pop_front();
    auto remaining = send_buffer.size() - written;
    if (remaining == 0)
      prepare_next_write(parent);
    return rw_state::success;
  }

  void prepare_next_write(newb_base*) override {
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

  void flush(newb_base* parent) override {
    if (!offline_buffer.empty() && !writing) {
      writing = true;
      prepare_next_write(parent);
    }
  }

  expected<native_socket>
  connect(const std::string&, uint16_t,
          optional<network::protocol::network> = none) override {
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

  // Some moocks for receiving packets.
  size_t index;
  size_t payload_len;
  sequence_type next_seq;
  std::vector<instruction> instructions;
  std::deque<sequence_type> skipped;
};

// Deliver a sequence of message all inorder.
static void BM_receive_udp_raw_sequence_inorder(benchmark::State& state) {
  using message_t = new_raw_msg;
  using proto_t = udp_protocol<ordering<raw>>;
  config cfg;
  actor_system sys{cfg};
  auto tptr = new dummy_ordering_transport;
  caf::io::network::native_socket sock(1337);
  transport_ptr trans{tptr};
  actor n = spawn_newb<proto_t, hidden>(sys, dummy_newb<message_t>,
                                        std::move(trans), sock, false);
  auto ptr = caf::actor_cast<caf::abstract_actor*>(n);
  //auto& ref = dynamic_cast<newb<message_t>&>(*ptr);
  auto& ref = dynamic_cast<stateful_newb<message_t, dummy_state>&>(*ptr);
  // Prepare packets to receive.
  size_t packet_size = state.range(0);
  tptr->payload_len = packet_size;
  {
    auto whdl = ref.wr_buf(nullptr);
    auto start = whdl.buf->size();
    whdl.buf->resize(start + packet_size);
    std::fill(whdl.buf->begin() + start, whdl.buf->end(), 'a');
  }
  ref.trans->receive_buffer = ref.trans->send_buffer;
  // Add instructions.
  tptr->instructions.emplace_back(instruction::next);
  tptr->instructions.emplace_back(instruction::next);
  tptr->instructions.emplace_back(instruction::next);
  tptr->instructions.emplace_back(instruction::next);
  tptr->instructions.emplace_back(instruction::next);
  // 5
  tptr->instructions.emplace_back(instruction::next);
  tptr->instructions.emplace_back(instruction::next);
  tptr->instructions.emplace_back(instruction::next);
  tptr->instructions.emplace_back(instruction::next);
  tptr->instructions.emplace_back(instruction::next);
  auto msg_expected = [&] {
    //std::cerr << "expected" << std::endl;
    ref.state.received = false;
    ref.read_event();
    if (!ref.state.received) {
      std::cerr << "expected message did not arrive" << std::endl;
      std::abort();
    }
  };
  for (auto _ : state) {
    msg_expected();
    msg_expected();
    msg_expected();
    msg_expected();
    msg_expected();
    // 5
    msg_expected();
    msg_expected();
    msg_expected();
    msg_expected();
    msg_expected();
    sys.clock().cancel_all();
  }
  ref.stop();
}

BENCHMARK(BM_receive_udp_raw_sequence_inorder)->RangeMultiplier(2)->Range(1<<from, 1<<to);

// Deliver a sequence of messages with one message missing.
static void BM_receive_udp_raw_sequence_dropped(benchmark::State& state) {
  using message_t = new_raw_msg;
  using proto_t = udp_protocol<ordering<raw>>;
  config cfg;
  actor_system sys{cfg};
  caf::io::network::native_socket sock(1337);
  auto tptr = new dummy_ordering_transport;
  transport_ptr trans{tptr};
  actor n = spawn_newb<proto_t, hidden>(sys, dummy_newb<message_t >,
                                        std::move(trans), sock, false);
  auto ptr = caf::actor_cast<caf::abstract_actor*>(n);
  auto& ref = dynamic_cast<stateful_newb<message_t, dummy_state>&>(*ptr);
  // Prepare packets to receive.
  size_t packet_size = state.range(0);
  tptr->payload_len = packet_size;
  {
    auto whdl = ref.wr_buf(nullptr);
    auto start = whdl.buf->size();
    whdl.buf->resize(start + packet_size);
    std::fill(whdl.buf->begin() + start, whdl.buf->end(), 'a');
  }
  ref.trans->receive_buffer = ref.trans->send_buffer;
  // Add instructions.
  tptr->instructions.emplace_back(instruction::next);
  tptr->instructions.emplace_back(instruction::skip);
  tptr->instructions.emplace_back(instruction::next);
  tptr->instructions.emplace_back(instruction::next);
  tptr->instructions.emplace_back(instruction::next);
  // 5
  tptr->instructions.emplace_back(instruction::next);
  tptr->instructions.emplace_back(instruction::next);
  tptr->instructions.emplace_back(instruction::next);
  tptr->instructions.emplace_back(instruction::next);
  tptr->instructions.emplace_back(instruction::next);
  auto msg_expected = [&] {
    ref.state.received = false;
    ref.read_event();
    if (!ref.state.received) {
      std::cerr << "expected message did not arrive" << std::endl;
      std::abort();
    }
  };
  auto msg_unexpected = [&] {
    ref.state.received = false;
    ref.read_event();
    if (ref.state.received) {
      std::cerr << "message arrived unexpectedly" << std::endl;
      std::abort();
    }
  };
  for (auto _ : state) {
    msg_expected();
    msg_unexpected();
    msg_unexpected();
    msg_unexpected();
    msg_unexpected();
    msg_unexpected();
    msg_expected();
    msg_expected();
    msg_expected();
    msg_expected();
    sys.clock().cancel_all();
  }
  ref.stop();
}

BENCHMARK(BM_receive_udp_raw_sequence_dropped)->RangeMultiplier(2)->Range(1<<from, 1<<to);

// Deliver a sequence of messages with one delivered out of order.
static void BM_receive_udp_raw_sequence_late(benchmark::State& state) {
  using message_t = new_raw_msg;
  using proto_t = udp_protocol<ordering<raw>>;
  config cfg;
  actor_system sys{cfg};
  caf::io::network::native_socket sock(1337);
  auto tptr = new dummy_ordering_transport;
  transport_ptr trans{tptr};
  actor n = spawn_newb<proto_t, hidden>(sys, dummy_newb<message_t>,
                                        std::move(trans), sock, false);
  auto ptr = caf::actor_cast<caf::abstract_actor*>(n);
  auto& ref = dynamic_cast<stateful_newb<message_t, dummy_state>&>(*ptr);
  // Prepare packets to receive.
  size_t packet_size = state.range(0);
  tptr->payload_len = packet_size;
  {
    auto whdl = ref.wr_buf(nullptr);
    auto start = whdl.buf->size();
    whdl.buf->resize(start + packet_size);
    std::fill(whdl.buf->begin() + start, whdl.buf->end(), 'a');
  }
  ref.trans->receive_buffer = ref.trans->send_buffer;
  // Add instructions.
  tptr->instructions.emplace_back(instruction::next);
  tptr->instructions.emplace_back(instruction::skip);
  tptr->instructions.emplace_back(instruction::recover);
  tptr->instructions.emplace_back(instruction::next);
  tptr->instructions.emplace_back(instruction::next);
  // 5
  tptr->instructions.emplace_back(instruction::next);
  tptr->instructions.emplace_back(instruction::next);
  tptr->instructions.emplace_back(instruction::next);
  tptr->instructions.emplace_back(instruction::next);
  tptr->instructions.emplace_back(instruction::next);
  auto msg_expected = [&] {
    //std::cerr << "expected" << std::endl;
    ref.state.received = false;
    ref.read_event();
    if (!ref.state.received) {
      std::cerr << "expected message did not arrive" << std::endl;
      std::abort();
    }
  };
  auto msg_unexpected = [&] {
    //std::cerr << "unexpected" << std::endl;
    ref.state.received = false;
    ref.read_event();
    if (ref.state.received) {
      std::cerr << "message arrive unexpectedly" << std::endl;
      std::abort();
    }
  };
  for (auto _ : state) {
    msg_expected();
    msg_unexpected();
    msg_expected();
    msg_expected();
    msg_expected();
    // 5
    msg_expected();
    msg_expected();
    msg_expected();
    msg_expected();
    msg_expected();
    sys.clock().cancel_all();
  }
  ref.stop();
}

BENCHMARK(BM_receive_udp_raw_sequence_late)->RangeMultiplier(2)->Range(1<<from, 1<<to);


} // namespace anonymous

BENCHMARK_MAIN();
