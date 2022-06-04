// This file is part of CAF, the C++ Actor Framework. See the file LICENSE in
// the main distribution directory for license terms and copyright or visit
// https://github.com/actor-framework/actor-framework/blob/master/LICENSE.

#pragma once

#include "caf/net/socket_event_layer.hpp"
#include "caf/net/socket_manager.hpp"
#include "caf/settings.hpp"

namespace caf::net {

/// A connection_acceptor accepts connections from an accept socket and creates
/// socket managers to handle them via its factory.
template <class Socket, class Factory>
class connection_acceptor : public socket_event_layer {
public:
  // -- member types -----------------------------------------------------------

  using socket_type = Socket;

  using factory_type = Factory;

  // -- constructors, destructors, and assignment operators --------------------

  template <class... Ts>
  explicit connection_acceptor(Socket fd, size_t limit, Ts&&... xs)
    : fd_(fd), limit_(limit), factory_(std::forward<Ts>(xs)...) {
    // nop
  }

  // -- factories --------------------------------------------------------------

  template <class... Ts>
  static std::unique_ptr<connection_acceptor>
  make(Socket fd, size_t limit, Ts&&... xs) {
    return std::make_unique<connection_acceptor>(fd, limit,
                                                 std::forward<Ts>(xs)...);
  }

  // -- implementation of socket_event_layer -----------------------------------

  error start(socket_manager* owner, const settings& cfg) override {
    CAF_LOG_TRACE("");
    owner_ = owner;
    cfg_ = cfg;
    if (auto err = factory_.start(owner, cfg))
      return err;
    owner->register_reading();
    return none;
  }

  socket handle() const override {
    return fd_;
  }

  void handle_read_event() override {
    CAF_LOG_TRACE("");
    CAF_ASSERT(owner_ != nullptr);
    if (auto x = accept(fd_)) {
      socket_manager_ptr child = factory_.make(owner_->mpx_ptr(), *x);
      if (!child) {
        CAF_LOG_ERROR("factory failed to create a new child");
        owner_->deregister();
        return;
      }
      if (auto err = child->start(cfg_)) {
        CAF_LOG_ERROR("failed to initialize new child:" << err);
        owner_->deregister();
        return;
      }
      if (limit_ != 0 && ++accepted_ == limit_) {
        // TODO: ask owner to close socket.
        owner_->deregister();
        return;
      }
    } else {
      CAF_LOG_ERROR("accept failed:" << x.error());
      owner_->deregister();
    }
  }

  void handle_write_event() override {
    CAF_LOG_ERROR("connection_acceptor received write event");
    owner_->deregister_writing();
  }

  void abort(const error& reason) override {
    CAF_LOG_ERROR("connection_acceptor aborts due to an error: " << reason);
    factory_.abort(reason);
  }

private:
  Socket fd_;

  size_t limit_;

  factory_type factory_;

  socket_manager* owner_ = nullptr;

  size_t accepted_ = 0;

  settings cfg_;
};

} // namespace caf::net
