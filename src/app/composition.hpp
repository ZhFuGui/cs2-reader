#pragma once
#include "memory/backend.hpp"
namespace cs2::app {
std::unique_ptr<memory::IBackend> create_backend(memory::BackendKind);
}
