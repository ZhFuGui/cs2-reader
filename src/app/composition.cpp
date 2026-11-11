#include "app/composition.hpp"
#include "backends/hyperv/backend.hpp"
#include "backends/r3/backend.hpp"
#include <stdexcept>
namespace cs2::app {
std::unique_ptr<memory::IBackend> create_backend(memory::BackendKind kind) {
    switch (kind) {
    case memory::BackendKind::R3:
        return backends::make_r3_backend();
    case memory::BackendKind::HyperV:
        return backends::make_hyperv_backend();
    }
    throw std::runtime_error("Unsupported backend");
}
} // namespace cs2::app
