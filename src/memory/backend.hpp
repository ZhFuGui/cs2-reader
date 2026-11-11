#pragma once
#include "memory/session.hpp"
#include <string_view>
namespace cs2::memory {
enum class BackendKind { R3, HyperV };
class IBackend {
  public:
    virtual ~IBackend() = default;
    virtual BackendKind kind() const noexcept = 0;
    virtual std::unique_ptr<ISession> attach(ProcessIdentity process, AccessMode access) = 0;
};
} // namespace cs2::memory
