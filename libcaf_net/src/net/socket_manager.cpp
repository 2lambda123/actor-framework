// This file is part of CAF, the C++ Actor Framework. See the file LICENSE in
// the main distribution directory for license terms and copyright or visit
// https://github.com/actor-framework/actor-framework/blob/master/LICENSE.

#include "caf/net/socket_manager.hpp"

#include "caf/config.hpp"
#include "caf/make_counted.hpp"
#include "caf/net/actor_shell.hpp"
#include "caf/net/multiplexer.hpp"

namespace caf::net {

// -- constructors, destructors, and assignment operators ----------------------

socket_manager::socket_manager(multiplexer* mpx, socket fd,
                               event_handler_ptr handler)
  : fd_(fd), mpx_(mpx), handler_(std::move(handler)), disposed_(false) {
  CAF_ASSERT(fd_ != invalid_socket);
  CAF_ASSERT(mpx_ != nullptr);
  CAF_ASSERT(handler_ != nullptr);
  memset(&flags_, 0, sizeof(flags_t));
}

socket_manager::~socket_manager() {
  close(fd_);
}

// -- factories ----------------------------------------------------------------

socket_manager_ptr socket_manager::make(multiplexer* mpx, socket handle,
                                        event_handler_ptr handler) {
  CAF_ASSERT(mpx != nullptr);
  return make_counted<socket_manager>(std::move(mpx), handle,
                                      std::move(handler));
}

// -- properties ---------------------------------------------------------------

actor_system& socket_manager::system() noexcept {
  CAF_ASSERT(mpx_ != nullptr);
  return mpx_->system();
}

bool socket_manager::is_reading() const noexcept {
  return mpx_->is_reading(this);
}

bool socket_manager::is_writing() const noexcept {
  return mpx_->is_writing(this);
}

// -- event loop management ----------------------------------------------------

void socket_manager::register_reading() {
  if (!flags_.read_closed)
    mpx_->register_reading(this);
}

void socket_manager::register_writing() {
  if (!flags_.write_closed)
    mpx_->register_writing(this);
}

void socket_manager::deregister_reading() {
  mpx_->deregister_reading(this);
}

void socket_manager::deregister_writing() {
  mpx_->deregister_writing(this);
}

void socket_manager::deregister() {
  mpx_->deregister(this);
}

void socket_manager::shutdown_read() {
  deregister_reading();
  flags_.read_closed = true;
}

void socket_manager::shutdown_write() {
  deregister_writing();
  flags_.write_closed = true;
}

void socket_manager::shutdown() {
  flags_.read_closed = true;
  flags_.write_closed = true;
  disposed_ = true;
  deregister();
}

// -- callbacks for the handler ------------------------------------------------

void socket_manager::schedule_handover() {
  deregister();
  mpx_->schedule_fn([ptr = strong_this()] { //
    event_handler_ptr next;
    if (ptr->handler_->do_handover(next)) {
      ptr->handler_.swap(next);
    }
  });
}

void socket_manager::schedule(action what) {
  // Wrap the action to make sure the socket manager is still alive when running
  // the action later.
  mpx_->schedule_fn([ptr = strong_this(), f = std::move(what)]() mutable {
    CAF_IGNORE_UNUSED(ptr);
    f.run();
  });
}

// -- callbacks for the multiplexer --------------------------------------------

void socket_manager::close_read() noexcept {
  // TODO: extend transport API for closing read operations.
  flags_.read_closed = true;
  if (flags_.write_closed)
    disposed_ = true;
}

void socket_manager::close_write() noexcept {
  // TODO: extend transport API for closing write operations.
  flags_.write_closed = true;
  if (flags_.read_closed)
    disposed_ = true;
}

error socket_manager::init(const settings& cfg) {
  CAF_LOG_TRACE(CAF_ARG(cfg));
  if (auto err = nonblocking(fd_, true)) {
    CAF_LOG_ERROR("failed to set nonblocking flag in socket:" << err);
    return err;
  }
  return handler_->init(this, cfg);
}

void socket_manager::handle_read_event() {
  handler_->handle_read_event();
}

void socket_manager::handle_write_event() {
  handler_->handle_write_event();
}

void socket_manager::handle_error(sec code) {
  if (handler_) {
    handler_->abort(make_error(code));
    handler_ = nullptr;
    disposed_ = true;
  }
}

// -- implementation of disposable_impl ----------------------------------------

void socket_manager::dispose() {
  if (!disposed())
    mpx_->dispose(this);
}

bool socket_manager::disposed() const noexcept {
  return disposed_.load();
}

void socket_manager::ref_disposable() const noexcept {
  ref();
}

void socket_manager::deref_disposable() const noexcept {
  deref();
}

// -- utility functions --------------------------------------------------------

socket_manager_ptr socket_manager::strong_this() {
  return socket_manager_ptr{this};
}

} // namespace caf::net
