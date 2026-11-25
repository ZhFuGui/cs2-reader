#pragma once
#include "game/cs2/entity/directory.hpp"
#include "model/local.hpp"
namespace cs2::game {
model::Local sample_local(memory::IReadChannel&, const Context&, const Directory&);
}
