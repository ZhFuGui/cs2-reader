#pragma once
#include "memory/read_error.hpp"
#include "memory/session.hpp"
#include <stdexcept>
namespace cs2::game {
class ContextPending : public memory::ReadUnavailable {
  public:
    using memory::ReadUnavailable::ReadUnavailable;
};
struct Context {
    memory::Module client, engine;
    std::uint32_t build = 0;
};
Context open_context(memory::IReadChannel&, const memory::ISession&);
} // namespace cs2::game
