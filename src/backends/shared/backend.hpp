#pragma once
#include "memory/backend.hpp"
#include <string_view>

namespace cs2::backends {
std::unique_ptr<memory::IBackend> make_shared_backend(memory::ProcessNameKey, std::string_view image_name);
} // namespace cs2::backends
