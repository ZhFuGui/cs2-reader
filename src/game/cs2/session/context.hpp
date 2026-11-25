#pragma once
#include "memory/read_error.hpp"
#include "memory/modules.hpp"
#include "memory/session.hpp"
#include "game/cs2/session/static_data.hpp"
#include <stdexcept>
namespace cs2::game {
class ContextPending : public memory::ReadUnavailable {
  public:
    using memory::ReadUnavailable::ReadUnavailable;
};
struct Context {
    memory::Module client, engine;
    std::uint32_t build = 0;
    // 每个 worker 使用独立副本；这里只读共享模块描述，不共享惰性写入记录。
    StaticData static_data;
};
Context open_context(memory::IReadChannel&, const memory::ISession&);
} // namespace cs2::game
