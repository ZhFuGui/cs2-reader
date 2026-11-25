#include "app/composition.hpp"
#include "backends/shared/backend.hpp"
namespace cs2::app {
std::unique_ptr<memory::IBackend> create_backend(memory::ProcessNameKey name, std::string_view image_name) {
    return backends::make_shared_backend(name, image_name);
}
} // namespace cs2::app
