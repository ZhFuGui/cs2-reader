#pragma once
#include "memory/session.hpp"
namespace cs2::memory {
class IBackend {
  public:
    virtual ~IBackend() = default;
    virtual std::unique_ptr<ISession> attach(ProcessIdentity process, AccessMode access) = 0;
};
} // namespace cs2::memory
