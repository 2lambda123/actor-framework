// This file is part of CAF, the C++ Actor Framework. See the file LICENSE in
// the main distribution directory for license terms and copyright or visit
// https://github.com/actor-framework/actor-framework/blob/master/LICENSE.

#pragma once

#include "caf/detail/type_traits.hpp"
#include "caf/event_based_actor.hpp"
#include "caf/typed_event_based_actor.hpp"

#include <new>
#include <utility>

namespace caf::detail {

/// An event-based actor with managed state. The state is constructed with the
/// actor, but destroyed when the actor calls `quit`. This state management
/// brakes cycles and allows actors to automatically release resources as soon
/// as possible.
template <class State, class Base>
class stateful_impl : public event_based_actor {
public:
  using super = Base;

  template <class>
  friend struct stateful_t;

  explicit stateful_impl(actor_config& cfg) : super(cfg) {
    // nop
  }

  void on_exit() override {
    if (has_state)
      state.~State();
  }

  union {
    State state;
  };

  bool has_state = false;
};

template <class T>
struct stateful_impl_base;

template <>
struct stateful_impl_base<behavior> {
  using type = event_based_actor;
};

template <class... Ts>
struct stateful_impl_base<typed_behavior<Ts...>> {
  using type = typed_event_based_actor<Ts...>;
};

template <class T>
using stateful_impl_base_t = typename stateful_impl_base<T>::type;

} // namespace caf::detail

namespace caf {

template <class State>
struct stateful_t {
  using behavior_type = decltype(std::declval<State*>()->make_behavior());

  static_assert(detail::is_behavior_v<behavior_type>,
                "State::make_behavior() must return a behavior");

  using base_type = detail::stateful_impl_base_t<behavior_type>;

  using impl_type = detail::stateful_impl<State, base_type>;

  template <class... Args>
  auto operator()(impl_type* self, Args&&... args) const {
    new (&self->state) State(std::forward<Args>(args)...);
    self->has_state = true;
    return self->state.make_behavior();
  }
};

template <class State>
constexpr stateful_t<State> stateful() {
  return {};
}

} // namespace caf
