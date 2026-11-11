#include "app/composition.hpp"
#include "backends/hyperv/backend.hpp"
namespace cs2::app {
std::unique_ptr<memory::IBackend> create_backend() {
    return backends::make_hyperv_backend();
}
} // namespace cs2::app
