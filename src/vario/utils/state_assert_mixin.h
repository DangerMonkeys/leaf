#pragma once
#include <type_traits>
#include <utility>

/// @brief Apply to a class to provide an assertState(caller, ... expected states) method.
/// @tparam Derived Name of class being mixed into.
/// @details The class to which this mixin is applied must provide:
/// void onUnexpectedState(const char* action, TState actual)
///   Method defining what to do when an unexpected state is encountered while attempting to perform
///   the specified action.  This method must be marked with:
///     friend struct StateAssertMixin<Derived>;
/// void TState state()
///   Method returning current state of object.
/// @example As applied to a Foo class:
/// class Foo : StateAssertMixin<Foo> {
///  public:
///   enum class State : uint8_t { State1, State2, State3}
///   State state() const { ... }
///   void doThing() {
///     assertState("doThing", State2, State3);  // Ensures Foo is in State2 or State3 here
///     ...
///   }
///  private:
///   void onUnexpectedState(const char* action, State actual) {
///     fatalError("Attempted to %s while state was %d", action, actual);
///   }
///   friend struct StateAssertMixin<Ambient>;
/// };
template <class Derived>
struct StateAssertMixin {
 protected:
  template <typename... OK>
  void assertState(const char* caller, OK... ok_states) const {
    using TState = decltype(std::declval<const Derived&>().state());
    static_assert(std::is_enum_v<TState>, "Derived::state() must return an enum");
    static_assert(sizeof...(OK) > 0, "Provide at least one acceptable state");
    static_assert((std::is_same_v<OK, TState> && ...),
                  "All acceptable states must match Derived::state()'s enum type");

    // Optional signature check
    using Sig = void (Derived::*)(const char*, TState) const;
    (void)static_cast<Sig>(&Derived::onUnexpectedState);

    const auto& self = *static_cast<const Derived*>(this);
    TState actual = self.state();
    if (((actual == ok_states) || ...)) return;
    self.onUnexpectedState(caller, actual);
  }
};
