// This file is part of CAF, the C++ Actor Framework. See the file LICENSE in
// the main distribution directory for license terms and copyright or visit
// https://github.com/actor-framework/actor-framework/blob/master/LICENSE.

#define CAF_SUITE net.producer_adapter

#include "caf/net/producer_adapter.hpp"

#include "net-test.hpp"

#include "caf/detail/network_order.hpp"
#include "caf/net/length_prefix_framing.hpp"
#include "caf/net/message_oriented.hpp"
#include "caf/net/middleman.hpp"
#include "caf/net/socket_guard.hpp"
#include "caf/net/stream_socket.hpp"
#include "caf/net/stream_transport.hpp"
#include "caf/scheduled_actor/flow.hpp"

using namespace caf;

namespace {

class writer {
public:
  explicit writer(net::stream_socket fd) : sg_(fd) {
    // nop
  }

  auto fd() {
    return sg_.socket();
  }

  byte_buffer encode(std::string_view msg) {
    using detail::to_network_order;
    auto prefix = to_network_order(static_cast<uint32_t>(msg.size()));
    auto prefix_bytes = as_bytes(make_span(&prefix, 1));
    byte_buffer buf;
    buf.insert(buf.end(), prefix_bytes.begin(), prefix_bytes.end());
    auto bytes = as_bytes(make_span(msg));
    buf.insert(buf.end(), bytes.begin(), bytes.end());
    return buf;
  }

  void write(std::string_view msg) {
    auto buf = encode(msg);
    if (net::write(fd(), buf) < 0)
      FAIL("failed to write: " << net::last_socket_error_as_string());
  }

private:
  net::socket_guard<net::stream_socket> sg_;
};

class app_t : public net::message_oriented::upper_layer {
public:
  using resource_type = async::producer_resource<int32_t>;

  using buffer_type = resource_type::buffer_type;

  using adapter_ptr = net::producer_adapter_ptr<buffer_type>;

  using adapter_type = adapter_ptr::element_type;

  explicit app_t(resource_type output) : output_(std::move(output)) {
    // nop
  }

  static auto make(resource_type output) {
    return std::make_unique<app_t>(std::move(output));
  }

  error init(net::socket_manager* mgr,
             net::message_oriented::lower_layer* down_ptr,
             const settings&) override {
    down = down_ptr;
    down->request_messages();
    if (auto buf = output_.try_open()) {
      // Note: the execution unit is the owner of this object. As as long as the
      // execution unit lives, accessing `this` is safe.
      auto do_resume = make_action([this] { down->request_messages(); });
      auto do_cancel = make_action([this] { down->shutdown(); });
      adapter_ = adapter_type::make(std::move(buf), mgr, std::move(do_resume),
                                    std::move(do_cancel));
      return none;
    } else {
      FAIL("unable to open the resource");
    }
  }

  void prepare_send() override {
    // nop
  }

  bool done_sending() override {
    return true;
  }

  void abort(const error& reason) override {
    if (reason == caf::sec::socket_disconnected || reason == caf::sec::disposed)
      adapter_->close();
    else
      adapter_->abort(reason);
  }

  ptrdiff_t consume(byte_span buf) override {
    auto val = int32_t{0};
    auto str = std::string_view{reinterpret_cast<char*>(buf.data()),
                                buf.size()};
    if (auto err = detail::parse(str, val))
      FAIL("unable to parse input: " << err);
    ++received_messages;
    if (auto capacity_left = adapter_->push(val); capacity_left == 0) {
      down->suspend_reading();
    }
    return static_cast<ptrdiff_t>(buf.size());
  }

  net::message_oriented::lower_layer* down = nullptr;
  size_t received_messages = 0;
  adapter_ptr adapter_;
  resource_type output_;
};

struct fixture : test_coordinator_fixture<> {
  fixture() : mm(sys) {
    if (auto err = mm.mpx().init())
      CAF_FAIL("mpx.init() failed: " << err);
  }

  bool handle_io_event() override {
    mm.mpx().apply_updates();
    return mm.mpx().poll_once(false);
  }

  auto mpx() {
    return mm.mpx_ptr();
  }

  net::middleman mm;
};

} // namespace

BEGIN_FIXTURE_SCOPE(fixture)

SCENARIO("publisher adapters suspend reads if the buffer becomes full") {
  GIVEN("an actor reading from a buffer resource") {
    static constexpr size_t num_items = 13;
    std::vector<int32_t> outputs;
    auto [rd, wr] = async::make_spsc_buffer_resource<int32_t>(8, 2);
    sys.spawn([rd{rd}, &outputs](event_based_actor* self) {
      self //
        ->make_observable()
        .from_resource(rd)
        .for_each([&outputs](int32_t x) { outputs.emplace_back(x); });
    });
    WHEN("a producer reads from a socket and publishes to the buffer") {
      auto [fd1, fd2] = unbox(net::make_stream_socket_pair());
      auto writer_thread = std::thread{[fd1{fd1}] {
        writer out{fd1};
        for (size_t i = 0; i < num_items; ++i)
          out.write(std::to_string(i));
      }};
      if (auto err = nonblocking(fd2, true))
        FAIL("nonblocking(fd2) returned an error: " << err);
      auto app = app_t::make(std::move(wr));
      auto framing = net::length_prefix_framing::make(std::move(app));
      auto transport = net::stream_transport::make(fd2, std::move(framing));
      auto mgr = net::socket_manager::make(mpx(), fd2, std::move(transport));
      if (auto err = mgr->init(content(cfg)))
        FAIL("mgr->init() failed: " << err);
      THEN("the actor receives all items from the writer (socket)") {
        while (outputs.size() < num_items)
          run();
        auto ls = [](auto... xs) { return std::vector<int32_t>{xs...}; };
        CHECK_EQ(outputs, ls(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12));
      }
      writer_thread.join();
    }
  }
}

END_FIXTURE_SCOPE()
