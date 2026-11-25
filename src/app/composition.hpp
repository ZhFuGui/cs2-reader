#pragma once
#include "memory/backend.hpp"
#include <string_view>
namespace cs2::app {
std::unique_ptr<memory::IBackend> create_backend(memory::ProcessNameKey name, std::string_view image_name);
}
