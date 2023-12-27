// This file is part of CAF, the C++ Actor Framework. See the file LICENSE in
// the main distribution directory for license terms and copyright or visit
// https://github.com/actor-framework/actor-framework/blob/master/LICENSE.

#include "caf/stateful.hpp"

#include "caf/test/fixture/deterministic.hpp"
#include "caf/test/test.hpp"

using namespace caf;

namespace {

struct cell_state {
  cell_state() = default;

  explicit cell_state(int init) : value(init) {
    // nop
  }

  behavior make_behavior() {
    return {
      [this](get_atom) { return value; },
      [this](put_atom, int new_value) { value = new_value; },
    };
  }

  int value = 0;
};

constexpr auto cell_impl = stateful<cell_state>();

behavior dummy_impl() {
  return {
    [](int) { return; },
  };
}

WITH_FIXTURE(test::fixture::deterministic) {

TEST("a default-constructed cell has value 0") {
  auto dummy = sys.spawn(dummy_impl);
  auto uut = sys.spawn(cell_impl);
  inject().with(get_atom_v).from(dummy).to(uut);
  expect<int>().with(0).from(uut).to(dummy);
}

TEST("a cell starts with a value given to the constructor") {
  auto dummy = sys.spawn(dummy_impl);
  auto uut = sys.spawn(cell_impl, 42);
  inject().with(get_atom_v).from(dummy).to(uut);
  expect<int>().with(42).from(uut).to(dummy);
}

} // WITH_FIXTURE(test::fixture::deterministic)

} // namespace
