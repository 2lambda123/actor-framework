// This file is part of CAF, the C++ Actor Framework. See the file LICENSE in
// the main distribution directory for license terms and copyright or visit
// https://github.com/actor-framework/actor-framework/blob/master/LICENSE.

#include "caf/net/http/serve.hpp"

namespace caf::detail {

// TODO: there is currently no back-pressure from the worker to the server.

// -- http_request_producer ----------------------------------------------------

void http_request_producer::on_consumer_ready() {
  // nop
}

void http_request_producer::on_consumer_cancel() {
}

void http_request_producer::on_consumer_demand(size_t) {
  // nop
}

void http_request_producer::ref_producer() const noexcept {
  ref();
}

void http_request_producer::deref_producer() const noexcept {
  deref();
}

bool http_request_producer::push(const net::http::request& item) {
  return buf_->push(item);
}

// -- http_flow_adapter --------------------------------------------------------

bool http_flow_adapter::prepare_send() {
  return true;
}

bool http_flow_adapter::done_sending() {
  return true;
}

void http_flow_adapter::abort(const error&) {
  for (auto& pending : pending_)
    pending.dispose();
}

error http_flow_adapter::init(net::socket_manager* owner,
                              net::http::lower_layer* down, const settings&) {
  parent_ = owner->mpx_ptr();
  down_ = down;
  down_->request_messages();
  return none;
}

ptrdiff_t http_flow_adapter::consume(const net::http::header& hdr,
                                     const_byte_span payload) {
  using namespace net::http;
  auto prom = async::promise<response>();
  auto fut = prom.get_future();
  auto buf = std::vector<std::byte>{payload.begin(), payload.end()};
  auto impl = request::impl{hdr, std::move(buf), std::move(prom)};
  producer_->push(request{std::make_shared<request::impl>(std::move(impl))});
  auto hdl = fut.bind_to(parent_).then(
    [this](const response& res) {
      down_->begin_header(res.code());
      for (auto& [key, val] : res.header_fields())
        down_->add_header_field(key, val);
      std::ignore = down_->end_header();
      down_->send_payload(res.body());
      // TODO: we should close the connection unless indicated otherwise
      //       (keepalive flag?). Also, we should clean up pending_.
    },
    [this](const error& err) {
      auto description = to_string(err);
      down_->send_response(status::internal_server_error, "text/plain",
                           description);
      // TODO: see above.
    });
  pending_.emplace_back(std::move(hdl));
  return static_cast<ptrdiff_t>(payload.size());
}

} // namespace caf::detail
