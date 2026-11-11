#pragma once
#include "memory/session.hpp"
#include <stdexcept>
namespace cs2::game {
class ContextPending : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};
struct Context {
    memory::Module client, engine;
    std::uint32_t build = 0;
};
Context open_context(memory::IReadChannel&, const memory::ISession&);
} // namespace cs2::game
