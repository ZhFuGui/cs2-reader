#pragma once
#include "memory/backend.hpp"
namespace cs2::backends {
std::unique_ptr<memory::IBackend> make_r3_backend();
}
